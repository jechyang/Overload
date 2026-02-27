/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/HAL/ShaderStorageBuffer.h>

namespace OvCore::Rendering::FrameGraphData
{
	struct LightingData
	{
		OvRendering::HAL::ShaderStorageBuffer* lightSSBO = nullptr;
	};
}
