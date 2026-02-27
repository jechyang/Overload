/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <vector>
#include <cstdint>

#include "OvCore/ParticleSystem/ParticleSystemParticle.h"

namespace OvCore::ParticleSystem
{
	/**
	* Fixed-capacity object pool for ParticleSystemParticle.
	* Uses a free-list for O(1) acquire and release with no heap allocation after construction.
	*/
	class ParticlePool
	{
	public:
		static constexpr uint32_t kDefaultCapacity = 1000;

		explicit ParticlePool(uint32_t p_capacity = kDefaultCapacity);

		/**
		* Acquire a free slot from the pool.
		* @return Pointer to an inactive particle slot, or nullptr if the pool is full.
		*/
		ParticleSystemParticle* Acquire();

		/**
		* Return a particle back to the pool and mark it inactive.
		* @param p_particle  Must be a reference to a slot owned by this pool.
		*/
		void Release(ParticleSystemParticle& p_particle);

		/**
		* Access all slots (both active and inactive).
		* Callers should check the `active` flag before processing each slot.
		*/
		std::vector<ParticleSystemParticle>& GetSlots();
		const std::vector<ParticleSystemParticle>& GetSlots() const;

		uint32_t GetActiveCount() const;
		uint32_t GetCapacity() const;

		/**
		* Resize the pool. Clears all active particles.
		*/
		void Resize(uint32_t p_capacity);

		/**
		* Mark all slots inactive and rebuild the free-list.
		*/
		void Clear();

	private:
		std::vector<ParticleSystemParticle> m_slots;
		std::vector<uint32_t>               m_freeList;
		uint32_t                            m_activeCount = 0;
	};
}
