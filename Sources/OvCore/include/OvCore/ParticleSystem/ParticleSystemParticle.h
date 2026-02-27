/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvMaths/FVector2.h>
#include <OvMaths/FVector3.h>
#include <OvMaths/FVector4.h>

namespace OvCore::ParticleSystem
{
	struct ParticleSystemParticle
	{
		OvMaths::FVector3 position;
		OvMaths::FVector3 velocity;
		OvMaths::FVector4 color;
		OvMaths::FVector2 lb_uv;
		OvMaths::FVector2 rt_uv;
		float size;
		float timeToLive;
		float totalTimeToLive;
		bool  active = false;
	};
}
