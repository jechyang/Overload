/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/HAL/UniformBuffer.h>

namespace OvCore::Rendering::FrameGraphData
{
	struct EngineBufferData
	{
		OvRendering::HAL::UniformBuffer* engineUBO = nullptr;
	};
}
