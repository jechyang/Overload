/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvMaths/FVector4.h>

#include "OvCore/ParticleSystem/ParticleSystemParticle.h"

namespace OvCore::ParticleSystem
{
	/**
	* Abstract base for particle affectors.
	* Affectors modify existing particles each frame (e.g. gravity, drag).
	*/
	class AParticleAffector
	{
	public:
		virtual ~AParticleAffector() = default;

		/**
		* Apply the affector to a single particle.
		* @param p_particle   Particle to modify
		* @param p_deltaTime  Frame delta time in seconds
		*/
		virtual void Apply(ParticleSystemParticle& p_particle, float p_deltaTime) = 0;
	};

	/**
	* Applies a constant downward acceleration to particles.
	*/
	class GravityAffector : public AParticleAffector
	{
	public:
		explicit GravityAffector(float p_gravity = 9.8f);
		virtual void Apply(ParticleSystemParticle& p_particle, float p_deltaTime) override;

	public:
		float gravity;
	};

	/**
	* Interpolates particle color over its lifetime.
	* Uses three key colors: start, mid, and end.
	*/
	class ColorGradientAffector : public AParticleAffector
	{
	public:
		ColorGradientAffector(
			const OvMaths::FVector4& p_startColor = OvMaths::FVector4{ 1.0f, 1.0f, 1.0f, 1.0f },
			const OvMaths::FVector4& p_midColor   = OvMaths::FVector4{ 1.0f, 1.0f, 0.5f, 0.8f },
			const OvMaths::FVector4& p_endColor   = OvMaths::FVector4{ 1.0f, 0.3f, 0.0f, 0.0f },
			float p_midTime                       = 0.5f
		);

		virtual void Apply(ParticleSystemParticle& p_particle, float p_deltaTime) override;

	public:
		OvMaths::FVector4 startColor;
		OvMaths::FVector4 midColor;
		OvMaths::FVector4 endColor;
		float midTime; // Normalized time (0-1) when midColor is reached
	};
}
