/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvTools/Jobs/JobSystem.h>

#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <tracy/Tracy.hpp>

#include "WorkStealingDeque.h"

namespace OvTools::Jobs::Detail
{
	/**
	* Deque implementation used for per-worker queues. The mutex version is the
	* correctness baseline; the Chase-Lev version is the production default.
	* Both are differentially exercised by the same test suite
	*/
#if defined(OVTOOLS_JOBS_USE_MUTEX_DEQUE)
	using WorkerDeque = MutexDeque;
#else
	using WorkerDeque = ChaseLevDeque;
#endif

	constexpr uint32_t kInvalidWorkerIndex = std::numeric_limits<uint32_t>::max();

	/**
	* Mutex-protected FIFO used for the global queue (external submissions) and
	* the main-thread queue (MainThread-affinity jobs)
	*/
	class LockedQueue
	{
	public:
		/**
		* Enqueues an item
		* @param p_item
		*/
		void Push(ControlBlock* p_item)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_items.push_back(p_item);
		}

		/**
		* Dequeues the oldest item, or returns nullptr when empty
		*/
		ControlBlock* TryPop()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_items.empty())
			{
				return nullptr;
			}
			ControlBlock* item = m_items.front();
			m_items.pop_front();
			return item;
		}

		/**
		* Removes every item; single-threaded use only (shutdown)
		*/
		std::vector<ControlBlock*> DrainAll()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			std::vector<ControlBlock*> items(m_items.begin(), m_items.end());
			m_items.clear();
			return items;
		}

	private:
		mutable std::mutex m_mutex;
		std::deque<ControlBlock*> m_items;
	};

	class Scheduler;

	// Identity of the current thread relative to its scheduler; set by worker
	// loops, used for submit routing and wait participation.
	thread_local Scheduler* t_schedulerOwner = nullptr;
	thread_local uint32_t t_workerIndex = kInvalidWorkerIndex;

	/**
	* Scheduler core owning the worker threads and every queue. Implemented
	* entirely inside this translation unit; the public JobSystem class only
	* holds an opaque pointer
	*/
	class Scheduler
	{
	public:
		explicit Scheduler(uint32_t p_workerCount)
			: m_mainThreadId(std::this_thread::get_id())
		{
			uint32_t workerCount = p_workerCount;
			if (workerCount == JobSystem::AutoDetectWorkerCount)
			{
				const unsigned int hardwareThreads = std::thread::hardware_concurrency();
				workerCount = hardwareThreads > 1u ? hardwareThreads - 1u : 1u;
			}
			workerCount = std::max(workerCount, 1u);

			m_workers.reserve(workerCount);
			for (uint32_t index = 0; index < workerCount; ++index)
			{
				m_workers.emplace_back(std::make_unique<Worker>());
			}
			try
			{
				for (uint32_t index = 0; index < workerCount; ++index)
				{
					m_workers[index]->thread = std::thread(&Scheduler::WorkerLoop, this, index);
				}
			}
			catch (...)
			{
				// A partially constructed pool must not destroy joinable threads.
				StopWorkers();
				throw;
			}
		}

		~Scheduler()
		{
			StopWorkers();

			// Drop queued jobs: retire them and their whole dependency closure so
			// nothing keeps itself alive through the ControlBlock self reference.
			std::vector<ControlBlock*> dropped;
			AppendAll(dropped, m_globalQueue.DrainAll());
			AppendAll(dropped, m_mainQueue.DrainAll());
			for (const std::unique_ptr<Worker>& worker : m_workers)
			{
				AppendAll(dropped, worker->deque.Drain());
			}
			RetireClosure(std::move(dropped));
		}

		uint32_t GetWorkerCount() const
		{
			return static_cast<uint32_t>(m_workers.size());
		}

		/**
		* Dependency attachment protocol:
		* 1. store the pending count before any attach (a racing completion must
		*    decrement an initialized counter);
		* 2. attach under the dependency's spinlock, either pushing into its
		*    successor list or, when the dependency is already terminal,
		*    decrementing immediately. The lock is what makes "attach" and
		*    "complete + detach successors" mutually exclusive, so a successor is
		*    never lost;
		* 3. a submission sentinel prevents execution until every dependency
		*    has been attached; releasing it performs the final zero crossing
		*/
		JobHandle Submit(
			std::function<void()> p_job,
			std::span<const JobHandle> p_dependencies,
			EAffinity p_affinity
		)
		{
			if (!p_job)
			{
				p_job = [] {};
			}

			for (const JobHandle& dependency : p_dependencies)
			{
				ValidateOwner(dependency);
			}

			auto block = std::make_shared<ControlBlock>(std::move(p_job), p_affinity, m_identity);
			// A submission sentinel prevents a partially attached job from running.
			if (p_dependencies.size() >= std::numeric_limits<uint32_t>::max())
			{
				throw std::length_error("Too many job dependencies");
			}
			uint32_t pendingCount = 1;
			for (const JobHandle& dependency : p_dependencies)
			{
				if (dependency.IsValid()) ++pendingCount;
			}
			block->m_pendingDependencies.store(pendingCount, std::memory_order_relaxed);
			block->m_self = block;
			try
			{
				for (const JobHandle& dependency : p_dependencies)
				{
					if (!dependency.IsValid()) continue;
					ControlBlock& dependencyBlock = *dependency.m_block;
					dependencyBlock.Lock();
					if (dependencyBlock.IsTerminal())
					{
						dependencyBlock.Unlock();
						block->m_pendingDependencies.fetch_sub(1, std::memory_order_acq_rel);
					}
					else
					{
						try
						{
							dependencyBlock.m_successors.push_back(block);
						}
						catch (...)
						{
							dependencyBlock.Unlock();
							throw;
						}
						dependencyBlock.Unlock();
					}
				}
				if (block->m_pendingDependencies.fetch_sub(1, std::memory_order_acq_rel) == 1)
				{
					TryMarkReady(*block);
				}
			}
			catch (...)
			{
				// Prior dependency edges may retain the block, but no job may run.
				block->m_status.store(EJobStatus::Retired, std::memory_order_release);
				block->m_job = nullptr;
				block->m_self.reset();
				throw;
			}

			return JobHandle(std::move(block));
		}

		void Wait(JobHandle p_handle)
		{
			ValidateOwner(p_handle);
			if (!p_handle.IsValid() || p_handle.IsCompleted())
			{
				return;
			}

			ControlBlock& block = *p_handle.m_block;

			// Debug-only contract: a worker waiting on a MainThread-affinity job
			// would deadlock, since workers never execute such jobs.
			assert(!IsCurrentWorker() || block.m_affinity != EAffinity::MainThread);

			uint32_t idleSpins = 0;
			for (;;)
			{
				const uint64_t observedEpoch = ObserveProgress();
				if (block.IsTerminal()) return;
				// Wait-as-participation: run acquired jobs on this thread instead
				// of idling; the acquired job may even be the one being waited on.
				if (ControlBlock* job = TryExecuteOne())
				{
					Execute(*job);
					idleSpins = 0;
					continue;
				}
				IdlePause(idleSpins, observedEpoch);
			}
		}

		void WaitAll(std::span<const JobHandle> p_handles)
		{
			for (const JobHandle& handle : p_handles)
			{
				ValidateOwner(handle);
				assert(!IsCurrentWorker() || !handle.IsValid() || handle.IsCompleted() ||
					handle.m_block->m_affinity != EAffinity::MainThread);
			}
			uint32_t idleSpins = 0;
			for (;;)
			{
				const uint64_t observedEpoch = ObserveProgress();
				bool allTerminal = true;
				for (const JobHandle& handle : p_handles)
				{
					if (handle.IsValid() && !handle.IsCompleted())
					{
						allTerminal = false;
						break;
					}
				}
				if (allTerminal)
				{
					return;
				}
				if (ControlBlock* job = TryExecuteOne())
				{
					Execute(*job);
					idleSpins = 0;
					continue;
				}
				IdlePause(idleSpins, observedEpoch);
			}
		}

		void WaitAllPassive(std::span<const JobHandle> p_handles)
		{
			if (!IsMainThread())
			{
				throw std::invalid_argument("Passive waits require the constructing thread");
			}
			for (const auto& handle : p_handles)
			{
				ValidateOwner(handle);
				if (handle.IsValid() && handle.m_block->m_affinity != EAffinity::Any)
				{
					throw std::invalid_argument("Passive waits require Any-affinity jobs");
				}
			}
			uint32_t idleSpins = 0;
			for (;;)
			{
				const auto epoch = ObserveProgress();
				if (std::all_of(p_handles.begin(), p_handles.end(), [](const auto& handle) { return handle.IsCompleted(); }))
				{
					return;
				}
				IdlePause(idleSpins, epoch);
			}
		}

		bool IsMainThread() const
		{
			return std::this_thread::get_id() == m_mainThreadId;
		}

		bool PumpMainThread()
		{
			// Only the constructing thread may pump, including in release builds.
			assert(IsMainThread());
			if (!IsMainThread())
			{
				return false;
			}

			ControlBlock* job = m_mainQueue.TryPop();
			if (job == nullptr)
			{
				return false;
			}
			Execute(*job);
			return true;
		}

		void SetErrorHandler(std::function<void(std::exception_ptr)> p_handler)
		{
			std::lock_guard<std::mutex> lock(m_errorMutex);
			m_errorHandler = std::move(p_handler);
		}

	private:
		void StopWorkers()
		{
			m_shutdown.store(true, std::memory_order_release);
			{
				// Pair with the wait predicate so nobody sleeps through the shutdown.
				std::lock_guard<std::mutex> wakeLock(m_wakeMutex);
			}
			m_wakeCV.notify_all();

			for (const std::unique_ptr<Worker>& worker : m_workers)
			{
				if (worker->thread.joinable())
				{
					worker->thread.join();
				}
			}

		}

		struct Worker
		{
			WorkerDeque deque;
			std::thread thread;
		};

		static constexpr uint32_t kBusySpinCount = 64;


		static void AppendAll(std::vector<ControlBlock*>& p_target, std::vector<ControlBlock*>&& p_source)
		{
			for (ControlBlock* item : p_source)
			{
				p_target.push_back(item);
			}
		}

		uint64_t ObserveProgress()
		{
			std::lock_guard<std::mutex> lock(m_wakeMutex);
			return m_wakeEpoch;
		}

		void IdlePause(uint32_t& p_idleSpins, uint64_t p_observedEpoch)
		{
			if (++p_idleSpins < kBusySpinCount)
			{
				std::this_thread::yield();
			}
			else
			{
				// Completion wakes waiters immediately; OS sleep granularity must
				// not turn a sub-millisecond job into a full timer-tick stall.
				std::unique_lock<std::mutex> lock(m_wakeMutex);
				m_progressCV.wait(lock, [this, p_observedEpoch]
				{
					return m_wakeEpoch != p_observedEpoch;
				});
				p_idleSpins = 0;
			}
		}

		void ValidateOwner(const JobHandle& p_handle) const
		{
			if (p_handle.IsValid() && p_handle.m_block->m_owner != m_identity)
			{
				throw std::invalid_argument("JobHandle belongs to another JobSystem");
			}
		}

		bool IsCurrentWorker() const
		{
			return t_schedulerOwner == this;
		}

		void WorkerLoop(uint32_t p_index)
		{
#if defined(TRACY_ENABLE)
			tracy::SetThreadName(("JobWorker-" + std::to_string(p_index)).c_str());
#endif

			t_schedulerOwner = this;
			t_workerIndex = p_index;

			while (!m_shutdown.load(std::memory_order_acquire))
			{
				uint64_t observedEpoch;
				{
					std::lock_guard<std::mutex> wakeLock(m_wakeMutex);
					observedEpoch = m_wakeEpoch;
				}
				if (ControlBlock* job = TryAcquireJobAsWorker(p_index))
				{
					Execute(*job);
					continue;
				}

				// Observe the epoch before searching every queue. Publication and
				// parking share a mutex, including work published to local deques.
				std::unique_lock<std::mutex> wakeLock(m_wakeMutex);
				m_wakeCV.wait(wakeLock, [this, observedEpoch]
				{
					return m_shutdown.load(std::memory_order_acquire) || m_wakeEpoch != observedEpoch;
				});
			}

			t_workerIndex = kInvalidWorkerIndex;
			t_schedulerOwner = nullptr;
		}

		ControlBlock* TryAcquireJobAsWorker(uint32_t p_index)
		{
			if (ControlBlock* job = m_workers[p_index]->deque.Pop())
			{
				return job;
			}
			if (ControlBlock* job = m_globalQueue.TryPop())
			{
				return job;
			}
			return TrySteal(p_index);
		}

		/**
		* Acquisition path for non-worker threads (main or foreign), used by
		* wait-as-participation. The main thread may additionally execute
		* MainThread-affinity jobs
		*/
		ControlBlock* TryExecuteOne()
		{
			if (IsCurrentWorker())
			{
				return TryAcquireJobAsWorker(t_workerIndex);
			}

			if (IsMainThread())
			{
				if (ControlBlock* job = m_mainQueue.TryPop())
				{
					return job;
				}
			}
			if (ControlBlock* job = m_globalQueue.TryPop())
			{
				return job;
			}
			return TrySteal(kInvalidWorkerIndex);
		}

		/**
		* Steal pass over the sibling deques, starting at a rotating offset so
		* thieves do not stampede the first worker
		*/
		ControlBlock* TrySteal(uint32_t p_excludeIndex)
		{
			const size_t workerCount = m_workers.size();
			if (workerCount == 0)
			{
				return nullptr;
			}
			const size_t start = static_cast<size_t>(
				m_stealCursor.fetch_add(1, std::memory_order_relaxed) % workerCount
			);
			for (size_t offset = 0; offset < workerCount; ++offset)
			{
				const size_t index = (start + offset) % workerCount;
				if (static_cast<uint32_t>(index) == p_excludeIndex)
				{
					continue;
				}
				if (ControlBlock* job = m_workers[index]->deque.Steal())
				{
					return job;
				}
			}
			return nullptr;
		}

		void Execute(ControlBlock& p_block)
		{
			ZoneScoped;

			EJobStatus expected = EJobStatus::Ready;
			if (!p_block.m_status.compare_exchange_strong(
				expected, EJobStatus::Running, std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return; // Defensive: a block can only be acquired once.
			}

			bool failed = false;
			try
			{
				p_block.m_job();
			}
			catch (...)
			{
				p_block.m_exception = std::current_exception();
				failed = true;
			}

			Finish(p_block, failed);
		}

		/**
		* Terminal bookkeeping: flip the status under the spinlock (synchronizing
		* with concurrent attaches), snapshot the successors, then decrement them
		* outside the lock. Runs identically for success and failure, which is
		* what keeps a throwing job from stranding the dependency graph
		*/
		void Finish(ControlBlock& p_block, bool p_failed)
		{
			// Finish callbacks and release captures before publishing completion.
			if (p_failed)
			{
				// The exception is deliberately swallowed: the graph must keep
				// flowing. It is reported through the optional error handler,
				// the only channel OvTools has (no logging dependency).
				try
				{
					std::function<void(std::exception_ptr)> handler;
					{
						std::lock_guard<std::mutex> lock(m_errorMutex);
						handler = m_errorHandler;
					}
					if (handler)
					{
						handler(p_block.m_exception);
					}
				}
				catch (...)
				{
					// Error reporting must never stop dependency completion.
				}
			}

			p_block.m_job = nullptr;

			std::vector<std::shared_ptr<ControlBlock>> successors;
			{
				p_block.Lock();
				p_block.m_status.store(
					p_failed ? EJobStatus::Failed : EJobStatus::Completed,
					std::memory_order_release
				);
				successors = std::move(p_block.m_successors);
				p_block.Unlock();
			}

			NotifyProgress(false);

			for (const std::shared_ptr<ControlBlock>& successor : successors)
			{
				if (successor->m_pendingDependencies.fetch_sub(1, std::memory_order_acq_rel) == 1)
				{
					TryMarkReady(*successor);
				}
			}

			// Release the scheduler liveness reference; this may destroy the
			// block, so nothing may touch it afterwards.
			p_block.m_self.reset();
		}

		/**
		* Flips WaitingDeps to Ready exactly once (the CAS arbitrates the racing
		* zero-crossing observers) and routes the block to the right queue
		*/
		void TryMarkReady(ControlBlock& p_block)
		{
			EJobStatus expected = EJobStatus::WaitingDeps;
			if (!p_block.m_status.compare_exchange_strong(
				expected, EJobStatus::Ready, std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return; // Already readied exactly once by another zero-crossing observer.
			}

			// Even during shutdown, enqueue successors. Only the destructor retires
			// the graph, after all executing workers have joined.
			if (p_block.m_affinity == EAffinity::MainThread)
			{
				m_mainQueue.Push(&p_block);
				NotifyProgress(false);
				return;
			}

			// Jobs readied by a worker go to that worker's own deque (LIFO
			// locality: successors usually consume the predecessor's data);
			// anything else goes to the global queue, as do spills from full deques.
			if (IsCurrentWorker() && m_workers[t_workerIndex]->deque.Push(&p_block))
			{
				NotifyProgress(true);
				return;
			}
			m_globalQueue.Push(&p_block);
			NotifyProgress(true);
		}

		void NotifyProgress(bool p_wakeWorker)
		{
			{
				std::lock_guard<std::mutex> wakeLock(m_wakeMutex);
				++m_wakeEpoch;
			}
			m_progressCV.notify_all();
			if (p_wakeWorker) m_wakeCV.notify_one();
		}

		/**
		* Retires every block reachable through successor edges, releasing each
		* block's self reference. Blocks only reachable through successor lists
		* of dropped jobs are never queued, so the closure must be walked explicitly
		*/
		void RetireClosure(std::vector<ControlBlock*> p_roots)
		{
			std::vector<std::shared_ptr<ControlBlock>> worklist;
			worklist.reserve(p_roots.size());
			for (ControlBlock* root : p_roots)
			{
				worklist.push_back(root->m_self);
			}
			while (!worklist.empty())
			{
				auto block = std::move(worklist.back());
				worklist.pop_back();
				Retire(*block, worklist);
			}
		}

		void Retire(ControlBlock& p_block, std::vector<std::shared_ptr<ControlBlock>>& p_worklist)
		{
			std::vector<std::shared_ptr<ControlBlock>> successors;
			{
				p_block.Lock();
				if (p_block.IsTerminal())
				{
					p_block.Unlock();
					return;
				}
				p_block.m_status.store(EJobStatus::Retired, std::memory_order_release);
				successors = std::move(p_block.m_successors);
				p_block.Unlock();
			}

			for (const std::shared_ptr<ControlBlock>& successor : successors)
			{
				p_worklist.push_back(successor);
			}

			p_block.m_job = nullptr;

			// Shared worklist entries keep repeated edges alive until inspected.
			p_block.m_self.reset();
		}

		std::vector<std::unique_ptr<Worker>> m_workers;
		LockedQueue m_globalQueue;
		LockedQueue m_mainQueue;

		std::mutex m_wakeMutex;
		std::condition_variable m_wakeCV;
		std::condition_variable m_progressCV;
		uint64_t m_wakeEpoch = 0;

		std::atomic<bool> m_shutdown{ false };
		std::atomic<uint64_t> m_stealCursor{ 0 };

		std::mutex m_errorMutex;
		std::function<void(std::exception_ptr)> m_errorHandler;

		const std::thread::id m_mainThreadId;
		const std::shared_ptr<const char> m_identity = std::make_shared<const char>();
	};
}

namespace OvTools::Jobs
{
	JobSystem::JobSystem(uint32_t p_workerCount)
		: m_scheduler(std::make_unique<Detail::Scheduler>(p_workerCount))
	{
	}

	JobSystem::~JobSystem() = default;

	uint32_t JobSystem::GetWorkerCount() const
	{
		return m_scheduler->GetWorkerCount();
	}

	JobHandle JobSystem::Submit(
		std::function<void()> p_job,
		std::span<const JobHandle> p_dependencies,
		EAffinity p_affinity
	)
	{
		return m_scheduler->Submit(std::move(p_job), p_dependencies, p_affinity);
	}

	void JobSystem::Wait(JobHandle p_handle)
	{
		m_scheduler->Wait(std::move(p_handle));
	}

	void JobSystem::WaitAll(std::span<const JobHandle> p_handles)
	{
		m_scheduler->WaitAll(p_handles);
	}

	void JobSystem::WaitAllPassive(std::span<const JobHandle> p_handles)
	{
		m_scheduler->WaitAllPassive(p_handles);
	}

	bool JobSystem::IsMainThread() const
	{
		return m_scheduler->IsMainThread();
	}

	bool JobSystem::PumpMainThread()
	{
		return m_scheduler->PumpMainThread();
	}

	void JobSystem::SetErrorHandler(std::function<void(std::exception_ptr)> p_handler)
	{
		m_scheduler->SetErrorHandler(std::move(p_handler));
	}
}
