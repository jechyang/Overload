/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <algorithm>
#include <cmath>
#include <numbers>

#include "OvCore/ParticleSystem/AParticleSystemEmitter.h"
#include <OvMaths/FVector3.h>

namespace
{
	float RandF()
	{
		static uint32_t seed = 12345u;
		seed = seed * 1664525u + 1013904223u;
		return static_cast<float>(static_cast<int32_t>(seed)) / static_cast<float>(0x7FFFFFFF);
	}

	OvMaths::FVector3 GenerateOrthonormalVector(const OvMaths::FVector3& p_direction)
	{
		// Generate a vector orthogonal to the direction
		const OvMaths::FVector3 absDir = OvMaths::FVector3{
			std::abs(p_direction.x),
			std::abs(p_direction.y),
			std::abs(p_direction.z)
		};

		// Choose the axis with smallest dot product (most orthogonal)
		if (absDir.x < absDir.y && absDir.x < absDir.z)
			return OvMaths::FVector3::Cross(p_direction, OvMaths::FVector3{ 1.0f, 0.0f, 0.0f });
		else if (absDir.y < absDir.z)
			return OvMaths::FVector3::Cross(p_direction, OvMaths::FVector3{ 0.0f, 1.0f, 0.0f });
		else
			return OvMaths::FVector3::Cross(p_direction, OvMaths::FVector3{ 0.0f, 0.0f, 1.0f });
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

// =============================================================================
// CircleParticleEmitter
// =============================================================================

OvCore::ParticleSystem::CircleParticleEmitter::CircleParticleEmitter(
	float p_emissionRate,
	float p_lifetime,
	float p_initialSpeed,
	float p_size,
	float p_radius,
	OvMaths::FVector3 p_direction,
	float p_spread
) :
	emissionRate(p_emissionRate),
	lifetime(p_lifetime),
	initialSpeed(p_initialSpeed),
	size(p_size),
	radius(p_radius),
	direction(OvMaths::FVector3::Normalize(p_direction)),
	spread(p_spread)
{
}

void OvCore::ParticleSystem::CircleParticleEmitter::InitParticle(ParticleSystemParticle& p_particle)
{
	// Generate a random point within a circle using uniform distribution
	// Using sqrt(r) for uniform distribution within the circle
	const float r = radius * std::sqrt(RandF());
	const float theta = RandF() * 2.0f * std::numbers::pi_v<float>;

	// Create orthonormal basis vectors for the circle plane
	const OvMaths::FVector3 tangent = GenerateOrthonormalVector(direction);
	OvMaths::FVector3 bitangent = OvMaths::FVector3::Normalize(OvMaths::FVector3::Cross(direction, tangent));

	// Position in the circle plane
	const float cosTheta = std::cos(theta);
	const float sinTheta = std::sin(theta);

	p_particle.position = tangent * (r * cosTheta) + bitangent * (r * sinTheta);

	// Generate velocity direction with spread around the circle normal
	const float phi = std::acos(std::clamp(1.0f - spread * (0.5f + RandF() * 0.5f), -1.0f, 1.0f));
	const float velocityTheta = RandF() * 2.0f * std::numbers::pi_v<float>;

	// Create velocity in local space (relative to circle normal)
	const OvMaths::FVector3 localVelocity{
		std::sin(phi) * std::cos(velocityTheta),
		std::cos(phi),
		std::sin(phi) * std::sin(velocityTheta)
	};

	// Transform velocity to world space using the circle basis
	const OvMaths::FVector3 worldVelocity =
		tangent * localVelocity.x +
		direction * localVelocity.y +
		bitangent * localVelocity.z;

	p_particle.velocity = worldVelocity * initialSpeed;
	p_particle.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	p_particle.lb_uv = { 0.0f, 0.0f };
	p_particle.rt_uv = { 1.0f, 1.0f };
	p_particle.size = size;
	p_particle.timeToLive = lifetime;
	p_particle.totalTimeToLive = lifetime;
}

void OvCore::ParticleSystem::CircleParticleEmitter::Emit(
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
