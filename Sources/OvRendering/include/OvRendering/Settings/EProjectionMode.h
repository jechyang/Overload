/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <cstdint>
#include <OvReflection/Annotations.h>

namespace OvRendering::Settings
{
	/**
	* Projection modes, mostly used for cameras
	*/
	enum class OV_TYPE() EProjectionMode : uint8_t
	{
		ORTHOGRAPHIC,
		PERSPECTIVE
	};
}
