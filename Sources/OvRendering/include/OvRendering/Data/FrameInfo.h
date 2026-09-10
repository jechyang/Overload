/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <cstdint>

namespace OvRendering::Data
{
	/**
	* Holds information about a given frame
	*/
	struct FrameInfo
	{
		void Add(uint64_t vertices, uint64_t indices, uint64_t instances)
        { if(instances) { ++batchCount; instanceCount+=instances; polyCount+=(indices/3)*instances; vertexCount+=vertices*instances; } }
        FrameInfo& operator+=(const FrameInfo& other)
        { batchCount+=other.batchCount; instanceCount+=other.instanceCount; polyCount+=other.polyCount; vertexCount+=other.vertexCount; return *this; }
        uint64_t batchCount = 0;
		uint64_t instanceCount = 0;
		uint64_t polyCount = 0;
		uint64_t vertexCount = 0;
	};
}
