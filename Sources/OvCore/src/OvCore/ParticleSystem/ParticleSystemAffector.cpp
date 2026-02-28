/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include "OvCore/ParticleSystem/ParticleSystemAffector.h"

OvCore::ParticleSystem::GravityAffector::GravityAffector(float p_gravity) :
	gravity(p_gravity)
{
}

void OvCore::ParticleSystem::GravityAffector::Apply(ParticleSystemParticle& p_particle, float p_deltaTime)
{
	p_particle.velocity.y -= gravity * p_deltaTime;
}

// =============================================================================
// ColorGradientAffector
// =============================================================================

OvCore::ParticleSystem::ColorGradientAffector::ColorGradientAffector(
	const OvMaths::FVector4& p_startColor,
	const OvMaths::FVector4& p_midColor,
	const OvMaths::FVector4& p_endColor,
	float p_midTime
) :
	startColor(p_startColor),
	midColor(p_midColor),
	endColor(p_endColor),
	midTime(p_midTime)
{
}

void OvCore::ParticleSystem::ColorGradientAffector::Apply(ParticleSystemParticle& p_particle, float p_deltaTime)
{
	// Calculate normalized lifetime (0 = birth, 1 = death)
	const float lifeRatio = 1.0f - (p_particle.timeToLive / p_particle.totalTimeToLive);

	// Interpolate color based on lifetime
	if (lifeRatio < midTime)
	{
		// Interpolate between start and mid
		const float t = midTime > 0.0f ? lifeRatio / midTime : 0.0f;
		p_particle.color = startColor + (midColor - startColor) * t;
	}
	else
	{
		// Interpolate between mid and end
		const float t = (midTime < 1.0f) ? (lifeRatio - midTime) / (1.0f - midTime) : 1.0f;
		p_particle.color = midColor + (endColor - midColor) * t;
	}
}
