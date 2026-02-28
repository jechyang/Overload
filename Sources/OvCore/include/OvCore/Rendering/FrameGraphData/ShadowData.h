/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <memory>

#include <OvMaths/FMatrix4.h>
#include <OvRendering/HAL/Texture.h>

namespace OvCore::Rendering::FrameGraphData
{
	struct ShadowData
	{
		std::shared_ptr<OvRendering::HAL::Texture> shadowMap = nullptr;
		OvMaths::FMatrix4 lightSpaceMatrix;
	};
}
