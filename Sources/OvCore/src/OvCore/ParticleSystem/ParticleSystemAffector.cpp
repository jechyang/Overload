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
