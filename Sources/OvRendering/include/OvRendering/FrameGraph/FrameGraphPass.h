/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <OvRendering/FrameGraph/FrameGraphHandle.h>

namespace OvRendering::FrameGraph
{
	class FrameGraphResources;

	/**
	* Type-erased base for a FrameGraph pass node.
	*/
	struct FrameGraphPassNode
	{
		std::string name;
		bool culled = false;
		bool isOutput = false;
		std::vector<FrameGraphTextureHandle> reads;
		std::vector<FrameGraphTextureHandle> writes;
		std::vector<FrameGraphBufferHandle> bufferReads;
		std::vector<FrameGraphBufferHandle> bufferWrites;
		int refCount = 0;

		virtual ~FrameGraphPassNode() = default;
		virtual void Execute(const FrameGraphResources& resources) = 0;
	};

	/**
	* Typed FrameGraph pass node carrying user data and an execute callback.
	*/
	template<typename Data>
	struct FrameGraphPass final : FrameGraphPassNode
	{
		Data data{};
		std::function<void(const FrameGraphResources&, Data&)> executeFn;

		void Execute(const FrameGraphResources& resources) override
		{
			if (executeFn)
			{
				executeFn(resources, data);
			}
		}
	};
}
