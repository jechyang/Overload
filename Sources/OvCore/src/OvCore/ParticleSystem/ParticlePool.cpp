/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include "OvCore/ParticleSystem/ParticlePool.h"

OvCore::ParticleSystem::ParticlePool::ParticlePool(uint32_t p_capacity)
{
	Resize(p_capacity);
}

OvCore::ParticleSystem::ParticleSystemParticle* OvCore::ParticleSystem::ParticlePool::Acquire()
{
	if (m_freeList.empty())
		return nullptr;

	const uint32_t idx = m_freeList.back();
	m_freeList.pop_back();
	m_slots[idx].active = true;
	++m_activeCount;
	return &m_slots[idx];
}

void OvCore::ParticleSystem::ParticlePool::Release(ParticleSystemParticle& p_particle)
{
	const uint32_t idx = static_cast<uint32_t>(&p_particle - m_slots.data());
	p_particle.active = false;
	m_freeList.push_back(idx);
	--m_activeCount;
}

std::vector<OvCore::ParticleSystem::ParticleSystemParticle>& OvCore::ParticleSystem::ParticlePool::GetSlots()
{
	return m_slots;
}

const std::vector<OvCore::ParticleSystem::ParticleSystemParticle>& OvCore::ParticleSystem::ParticlePool::GetSlots() const
{
	return m_slots;
}

uint32_t OvCore::ParticleSystem::ParticlePool::GetActiveCount() const
{
	return m_activeCount;
}

uint32_t OvCore::ParticleSystem::ParticlePool::GetCapacity() const
{
	return static_cast<uint32_t>(m_slots.size());
}

void OvCore::ParticleSystem::ParticlePool::Resize(uint32_t p_capacity)
{
	m_slots.assign(p_capacity, ParticleSystemParticle{});
	m_freeList.clear();
	m_freeList.reserve(p_capacity);
	for (uint32_t i = p_capacity; i-- > 0;)
		m_freeList.push_back(i);
	m_activeCount = 0;
}

void OvCore::ParticleSystem::ParticlePool::Clear()
{
	Resize(static_cast<uint32_t>(m_slots.size()));
}
