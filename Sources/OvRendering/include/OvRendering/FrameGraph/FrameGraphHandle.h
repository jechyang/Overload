/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <cstdint>
#include <limits>

namespace OvRendering::FrameGraph
{
	/**
	* A lightweight handle referencing a virtual texture resource in the FrameGraph.
	*/
	struct FrameGraphTextureHandle
	{
		static constexpr uint32_t kInvalid = std::numeric_limits<uint32_t>::max();
		uint32_t id = kInvalid;

		bool IsValid() const { return id != kInvalid; }

		bool operator==(const FrameGraphTextureHandle&) const = default;
	};

	/**
	* A lightweight handle referencing a virtual buffer resource (UBO/SSBO) in the FrameGraph.
	*/
	struct FrameGraphBufferHandle
	{
		static constexpr uint32_t kInvalid = std::numeric_limits<uint32_t>::max();
		uint32_t id = kInvalid;

		bool IsValid() const { return id != kInvalid; }

		bool operator==(const FrameGraphBufferHandle&) const = default;
	};
}
