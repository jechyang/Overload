/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvMaths/FVector3.h>

#include "OvCore/ParticleSystem/ParticleSystemParticle.h"
#include "OvCore/ParticleSystem/ParticlePool.h"

namespace OvCore::ParticleSystem
{
	/**
	* Abstract base class for particle emitters.
	* Responsible for spawning new particles into the system.
	*/
	class AParticleEmitter
	{
	public:
		virtual ~AParticleEmitter() = default;

		/**
		* Initialize a single particle's properties at spawn time.
		* @param p_particle  Particle to initialize
		*/
		virtual void InitParticle(ParticleSystemParticle& p_particle) = 0;

		/**
		* Accumulate emission time and acquire slots from the pool for newly spawned particles.
		* @param p_pool       Particle pool to acquire slots from
		* @param p_deltaTime  Frame delta time in seconds
		*/
		virtual void Emit(ParticlePool& p_pool, float p_deltaTime) = 0;
	};

	/**
	* Emits particles from a single point in world space.
	*/
	class PointParticleEmitter : public AParticleEmitter
	{
	public:
		/**
		* @param p_emissionRate    Particles per second
		* @param p_lifetime        Particle lifetime in seconds
		* @param p_initialSpeed    Initial speed magnitude
		* @param p_size            Particle quad size in world units
		* @param p_spread          Cone half-angle in radians (0 = straight up)
		*/
		PointParticleEmitter(
			float p_emissionRate = 10.0f,
			float p_lifetime     = 2.0f,
			float p_initialSpeed = 1.0f,
			float p_size         = 0.1f,
			float p_spread       = 0.5f
		);

		virtual void InitParticle(ParticleSystemParticle& p_particle) override;
		virtual void Emit(ParticlePool& p_pool, float p_deltaTime) override;

	public:
		float emissionRate;
		float lifetime;
		float initialSpeed;
		float size;
		float spread;

	private:
		float m_accumulator = 0.0f;
	};
}
