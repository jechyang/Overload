/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <algorithm>
#include <cmath>
#include <numbers>

#include "OvCore/ParticleSystem/AParticleSystemEmitter.h"

namespace
{
	float RandF()
	{
		static uint32_t seed = 12345u;
		seed = seed * 1664525u + 1013904223u;
		return static_cast<float>(static_cast<int32_t>(seed)) / static_cast<float>(0x7FFFFFFF);
	}
}

OvCore::ParticleSystem::PointParticleEmitter::PointParticleEmitter(
	float p_emissionRate,
	float p_lifetime,
	float p_initialSpeed,
	float p_size,
	float p_spread
) :
	emissionRate(p_emissionRate),
	lifetime(p_lifetime),
	initialSpeed(p_initialSpeed),
	size(p_size),
	spread(p_spread)
{
}

void OvCore::ParticleSystem::PointParticleEmitter::InitParticle(ParticleSystemParticle& p_particle)
{
	const float theta = RandF() * std::numbers::pi_v<float>;
	const float phi   = std::acos(std::clamp(1.0f - spread * (0.5f + RandF() * 0.5f), -1.0f, 1.0f));

	p_particle.position        = OvMaths::FVector3::Zero;
	p_particle.velocity        = OvMaths::FVector3{
		std::sin(phi) * std::cos(theta),
		std::cos(phi),
		std::sin(phi) * std::sin(theta)
	} * initialSpeed;
	p_particle.color           = { 1.0f, 1.0f, 1.0f, 1.0f };
	p_particle.lb_uv           = { 0.0f, 0.0f };
	p_particle.rt_uv           = { 1.0f, 1.0f };
	p_particle.size            = size;
	p_particle.timeToLive      = lifetime;
	p_particle.totalTimeToLive = lifetime;
}

void OvCore::ParticleSystem::PointParticleEmitter::Emit(
	ParticlePool& p_pool,
	float p_deltaTime
)
{
	m_accumulator += emissionRate * p_deltaTime;

	while (m_accumulator >= 1.0f)
	{
		if (auto* slot = p_pool.Acquire())
			InitParticle(*slot);
		m_accumulator -= 1.0f;
	}
}
