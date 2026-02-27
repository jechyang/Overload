/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <functional>
#include <vector>

namespace OvCore::ECS::Components { class CReflectionProbe; }

namespace OvCore::Rendering::FrameGraphData
{
	struct ReflectionData
	{
		std::vector<std::reference_wrapper<OvCore::ECS::Components::CReflectionProbe>> reflectionProbes;
	};
}
