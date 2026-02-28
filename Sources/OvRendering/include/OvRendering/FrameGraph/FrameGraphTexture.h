/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <string>

#include <OvRendering/Settings/EInternalFormat.h>
#include <OvRendering/Settings/ETextureFilteringMode.h>
#include <OvRendering/Settings/ETextureWrapMode.h>
#include <OvRendering/Settings/ETextureType.h>
#include <OvRendering/FrameGraph/FrameGraphHandle.h>

namespace OvRendering::FrameGraph
{
	/**
	* Describes a virtual texture resource to be created by the FrameGraph.
	*/
	struct FrameGraphTextureDesc
	{
		std::string debugName;
		uint32_t width = 1;
		uint32_t height = 1;
		Settings::EInternalFormat internalFormat = Settings::EInternalFormat::RGBA;
		Settings::ETextureFilteringMode minFilter = Settings::ETextureFilteringMode::LINEAR;
		Settings::ETextureFilteringMode magFilter = Settings::ETextureFilteringMode::LINEAR;
		Settings::ETextureWrapMode wrapS = Settings::ETextureWrapMode::CLAMP_TO_BORDER;
		Settings::ETextureWrapMode wrapT = Settings::ETextureWrapMode::CLAMP_TO_BORDER;
		bool generateMipmaps = false;
	};
}
