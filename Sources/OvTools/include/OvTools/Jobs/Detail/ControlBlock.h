/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <new>
#include <vector>

#include <OvTools/Jobs/EAffinity.h>

namespace OvTools::Jobs::Detail
{
	/**
	* Job lifecycle states. Legal transitions:
	*   WaitingDeps -> Ready      (last dependency satisfied; happens exactly once, via CAS)
	*   Ready       -> Running    (an executor popped the job; exactly once, via CAS)
	*   Running     -> Completed  (user function returned normally)
	*   Running     -> Failed     (user function threw; exception stored)
	*   Ready       -> Retired    (dropped at shutdown without executing)
	*   WaitingDeps -> Retired    (dropped at shutdown without executing)
	*/
	enum class EJobStatus : uint8_t
	{
		WaitingDeps,
		Ready,
		Running,
		Completed,
		Failed,
		Retired
	};

#ifdef __cpp_lib_hardware_interference_size
	/**
	* Cache line size used to keep concurrently modified data on separate lines
	*/
	constexpr std::size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
	constexpr std::size_t kCacheLineSize = 64;
#endif

	/**
	* Internal task node shared by the scheduler, by dependency edges and by user handles
	*
	* Lifetime model: queues only ever store raw pointers; liveness is guaranteed by a
	* self reference (m_self) held from submission until the job completes, fails or is
	* retired at shutdown. Additional references are kept by user JobHandles and by the
	* successor lists of not-yet-terminal dependencies.
	*
	* The m_lock spinlock makes "check completion / attach as successor" (Submit) atomic
	* against "mark terminal / detach successors" (job completion), which is what
	* prevents lost wake-ups in the dependency protocol
	*/
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // Intentional cache-line padding.
#endif
	class alignas(kCacheLineSize) ControlBlock
	{
	public:
		/**
		* Creates a block for a job with the given affinity
		* @param p_job
		* @param p_affinity
		* @param p_owner
		*/
		ControlBlock(std::function<void()> p_job, EAffinity p_affinity, std::shared_ptr<const char> p_owner)
			: m_job(std::move(p_job)),
			m_affinity(p_affinity),
			m_owner(std::move(p_owner))
		{
		}

		/**
		* Returns true when the job reached a terminal state: completed,
		* failed, or dropped at shutdown
		*/
		bool IsTerminal() const
		{
			switch (m_status.load(std::memory_order_acquire))
			{
			case EJobStatus::Completed:
			case EJobStatus::Failed:
			case EJobStatus::Retired:
				return true;
			default:
				return false;
			}
		}

		/**
		* Acquires the spinlock protecting the status-flip + successor-list pair
		*/
		void Lock()
		{
			while (m_lock.test_and_set(std::memory_order_acquire))
			{
			}
		}

		/**
		* Releases the spinlock protecting the status-flip + successor-list pair
		*/
		void Unlock()
		{
			m_lock.clear(std::memory_order_release);
		}

		std::function<void()> m_job;

		/** Exception thrown by the user function; written by the executing thread before the status flip */
		std::exception_ptr m_exception;

		const EAffinity m_affinity;

		/** Scheduler identity retained by handles, even after its destruction */
		const std::shared_ptr<const char> m_owner;

		/** Job state; see EJobStatus. Flips to terminal states happen under m_lock */
		std::atomic<EJobStatus> m_status{ EJobStatus::WaitingDeps };

		/** Number of unsatisfied dependencies; the zero-crossing makes the job ready */
		std::atomic<uint32_t> m_pendingDependencies{ 0 };

		/** Spinlock making completion checks and successor attachment atomic (see class comment) */
		std::atomic_flag m_lock{};

		/** Jobs that depend on this one; only mutated under m_lock */
		std::vector<std::shared_ptr<ControlBlock>> m_successors;

		/** Self reference keeping the block alive while it is queued or executing */
		std::shared_ptr<ControlBlock> m_self;
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
