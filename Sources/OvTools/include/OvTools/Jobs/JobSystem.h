/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <span>

#include <OvTools/Jobs/EAffinity.h>
#include <OvTools/Jobs/JobHandle.h>

namespace OvTools::Jobs::Detail
{
	class Scheduler;
}

namespace OvTools::Jobs
{
	/**
	* Dependency-based job system: a work-stealing worker pool where each
	* submitted job becomes ready only after all of its dependencies have
	* completed. Threads waiting inside Wait participate in executing ready
	* jobs and lets the main thread contribute to the workload. Jobs must not
	* wait on themselves, their successors, or work requiring a blocked thread
	*/
	class JobSystem
	{
	public:
		/**
		* Sentinel for p_workerCount: detect the hardware thread count and
		* spawn hardware_concurrency - 1 workers (the main thread participates
		* while waiting)
		*/
		static constexpr uint32_t AutoDetectWorkerCount = 0;

		/**
		* Creates and starts the worker pool
		* @param p_workerCount Number of workers, or AutoDetectWorkerCount
		*/
		explicit JobSystem(uint32_t p_workerCount = AutoDetectWorkerCount);

		/**
		* Stops the workers. Queued-but-unstarted jobs are dropped (marked as
		* retired, so IsCompleted returns true without implying execution). The
		* caller must ensure no external Submit, Wait, or Pump call overlaps
		* destruction; running worker jobs must be able to finish without queued work
		*/
		~JobSystem();

		JobSystem(const JobSystem&) = delete;
		JobSystem& operator=(const JobSystem&) = delete;

		/**
		* Returns the number of worker threads (main thread excluded)
		*/
		uint32_t GetWorkerCount() const;

		/**
		* Submits a job that becomes ready once every valid handle in
		* p_dependencies has completed. Thread-safe; callable from any thread,
		* including from within a running job. Valid dependencies must belong to
		* this system; foreign handles throw std::invalid_argument
		* @param p_job Function to execute
		* @param p_dependencies Handles this job depends on (invalid handles are ignored)
		* @param p_affinity Threads allowed to execute the job
		*/
		JobHandle Submit(
			std::function<void()> p_job,
			std::span<const JobHandle> p_dependencies = {},
			EAffinity p_affinity = EAffinity::Any
		);

		/**
		* Waits for a job while executing ready jobs on the calling thread.
		* Invalid and already-completed handles return immediately. Foreign handles
		* throw std::invalid_argument. A worker must not wait on work that directly
		* or transitively requires the constructing thread
		* @param p_handle
		*/
		void Wait(JobHandle p_handle);

		/**
		* Waits for every handle while executing ready jobs on the calling
		* thread. The same ownership and affinity constraints as Wait apply
		* @param p_handles
		*/
		void WaitAll(std::span<const JobHandle> p_handles);

		/**
		* Waits for terminal states without executing any queued work on the caller.
		* Only the constructing thread may call this; handles must belong to this
		* system and have Any affinity. Their dependencies must not require the
		* constructing thread. Throws invalid_argument for a violated direct contract.
		* Use this when helping could re-enter code that mutates borrowed job inputs.
		*/
		void WaitAllPassive(std::span<const JobHandle> p_handles);

		/** Returns whether this is the thread that constructed the pool. */
		bool IsMainThread() const;

		/**
		* Executes at most one MainThread-affinity job; returns true when a
		* job ran. Reserved for the future render thread topology; only the
		* thread that constructed the system may call this. A wrong-thread call
		* asserts in debug builds and returns false in release builds
		*/
		bool PumpMainThread();

		/**
		* Splits [0, p_count) into contiguous ranges and runs p_range(begin, end)
		* over them, one job per range. The callable must be safe to invoke
		* concurrently. The returned handle completes once every range and
		* every dependency has completed
		* @param p_count Number of elements
		* @param p_grainSize Minimum elements per job (clamped to at least 1)
		* @param p_range Callable invoked as p_range(begin, end)
		* @param p_dependencies Handles the ranges depend on
		*/
		template<typename F>
		JobHandle ParallelFor(
			uint64_t p_count,
			uint64_t p_grainSize,
			F&& p_range,
			std::span<const JobHandle> p_dependencies = {}
		);

		/**
		* Installs the callback invoked (from the executing thread) whenever a
		* job throws. Exceptions never interrupt the dependency graph: failed
		* jobs are marked terminal and their successors still run. The callback and
		* job captures are released before completion is published. Callbacks may
		* run concurrently and must not wait on the failed job or its successors.
		* Callback exceptions are swallowed; an empty handler reports nothing
		* @param p_handler
		*/
		void SetErrorHandler(std::function<void(std::exception_ptr)> p_handler);

	private:
		std::unique_ptr<Detail::Scheduler> m_scheduler;
	};
}

#include <OvTools/Jobs/JobSystem.inl>
