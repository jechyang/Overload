/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

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
}
