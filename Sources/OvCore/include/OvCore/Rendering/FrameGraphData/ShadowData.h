/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvMaths/FMatrix4.h>
#include <OvRendering/HAL/Texture.h>

namespace OvCore::Rendering::FrameGraphData
{
	struct ShadowData
	{
		OvRendering::HAL::Texture* shadowMap = nullptr;
		OvMaths::FMatrix4 lightSpaceMatrix;
	};
}
