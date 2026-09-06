/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include <OvTools/Jobs/Detail/ControlBlock.h>

namespace OvTools::Jobs::Detail
{
	/**
	* Work-stealing deque contract shared by both implementations below:
	* the owner thread pushes and pops at the bottom (LIFO, cache friendly),
	* thief threads steal from the top (FIFO). Push returns false when the
	* deque refuses the item (fixed-capacity implementations); the caller is
	* responsible for spilling it elsewhere. Items are raw pointers; liveness
	* is guaranteed by the ControlBlock self reference (see ControlBlock.h)
	*/
	class MutexDeque
	{
	public:
		/**
		* Owner-side push; always succeeds (unbounded container)
		* @param p_item
		*/
		bool Push(ControlBlock* p_item)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_items.push_back(p_item);
			return true;
		}

		/**
		* Owner-side pop; returns nullptr when empty
		*/
		ControlBlock* Pop()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_items.empty())
			{
				return nullptr;
			}
			ControlBlock* item = m_items.back();
			m_items.pop_back();
			return item;
		}

		/**
		* Thief-side steal; returns nullptr when empty
		*/
		ControlBlock* Steal()
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
		std::vector<ControlBlock*> Drain()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			std::vector<ControlBlock*> items(m_items.begin(), m_items.end());
			m_items.clear();
			return items;
		}

	private:
		std::mutex m_mutex;
		std::deque<ControlBlock*> m_items;
	};

	/**
	* Chase-Lev lock-free work-stealing deque
	*
	* Fixed capacity: when full, Push returns false and the scheduler spills
	* the job into the global queue. This deliberately avoids the array-growth
	* dance (atomic array pointer swap + stale-array reclamation), which is the
	* most error-prone part of growing implementations, while keeping memory
	* bounded and never dropping jobs.
	*
	* ABA safety: m_top and m_bottom are monotonic 64-bit counters, so the high
	* bits act as a generation tag above the low index bits; every steal/pop
	* compares the full counter via CAS, making a reused slot index undetectable
	* as a match. Wrap-around would need 2^63 operations.
	*
	* Memory ordering: Push publishes the slot with a release store of m_bottom;
	* Steal places a seq_cst fence between its acquire index loads, reads the slot before
	* its CAS; Pop uses a seq_cst fence between retracting m_bottom and reading
	* m_top so the last-element race is arbitrated by the seq_cst CAS on m_top
	*/
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // Intentional cache-line padding.
#endif
	class alignas(kCacheLineSize) ChaseLevDeque
	{
	public:
		ChaseLevDeque()
			: m_slots(std::make_unique<std::atomic<ControlBlock*>[]>(kCapacity))
		{
		}

		/**
		* Owner-side push; returns false when full (caller must spill)
		* @param p_item
		*/
		bool Push(ControlBlock* p_item)
		{
			const int64_t bottom = m_bottom.load(std::memory_order_relaxed);
			const int64_t top = m_top.load(std::memory_order_acquire);
			if (bottom - top >= static_cast<int64_t>(kCapacity))
			{
				return false;
			}
			m_slots[static_cast<uint64_t>(bottom) & kMask].store(p_item, std::memory_order_relaxed);
			m_bottom.store(bottom + 1, std::memory_order_release);
			return true;
		}

		/**
		* Owner-side pop; returns nullptr when empty or when the last element
		* got stolen concurrently
		*/
		ControlBlock* Pop()
		{
			// Retract the bottom first so concurrent stealers see an empty deque
			// while we compete for the last element.
			const int64_t bottom = m_bottom.load(std::memory_order_relaxed) - 1;
			m_bottom.store(bottom, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_seq_cst);
			int64_t top = m_top.load(std::memory_order_acquire);
			if (top <= bottom)
			{
				ControlBlock* item = m_slots[static_cast<uint64_t>(bottom) & kMask].load(std::memory_order_relaxed);
				if (top == bottom)
				{
					// Last element: race against stealers through m_top.
					if (!m_top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
					{
						item = nullptr; // Lost the race, a stealer took it.
					}
					m_bottom.store(bottom + 1, std::memory_order_relaxed);
				}
				return item;
			}
			m_bottom.store(bottom + 1, std::memory_order_relaxed);
			return nullptr;
		}

		/**
		* Thief-side steal; returns nullptr when empty or when losing a race
		*/
		ControlBlock* Steal()
		{
			int64_t top = m_top.load(std::memory_order_acquire);
			// Pair with Pop's fence (Le et al., PPoPP 2013, Figure 1).
			std::atomic_thread_fence(std::memory_order_seq_cst);
			const int64_t bottom = m_bottom.load(std::memory_order_acquire);
			if (top < bottom)
			{
				// Read the slot before the CAS: after a successful CAS the owner may
				// reuse the slot, and the full-counter CAS discards stale attempts.
				ControlBlock* item = m_slots[static_cast<uint64_t>(top) & kMask].load(std::memory_order_acquire);
				if (m_top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
				{
					return item;
				}
			}
			return nullptr;
		}

		/**
		* Removes every item; single-threaded use only (shutdown)
		*/
		std::vector<ControlBlock*> Drain()
		{
			std::vector<ControlBlock*> items;
			while (ControlBlock* item = Pop())
			{
				items.push_back(item);
			}
			while (ControlBlock* item = Steal())
			{
				items.push_back(item);
			}
			return items;
		}

	private:
		// Must be a power of two.
		static constexpr uint64_t kCapacity = 4096;
		static constexpr uint64_t kMask = kCapacity - 1;

		std::unique_ptr<std::atomic<ControlBlock*>[]> m_slots;

		// Thieves hammer m_top while the owner hammers m_bottom: separate lines.
		alignas(kCacheLineSize) std::atomic<int64_t> m_top{ 0 };
		alignas(kCacheLineSize) std::atomic<int64_t> m_bottom{ 0 };
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
