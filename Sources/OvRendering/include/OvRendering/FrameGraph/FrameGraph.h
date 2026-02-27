/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <OvRendering/Data/FrameDescriptor.h>
#include <OvRendering/FrameGraph/FrameGraphBlackboard.h>
#include <OvRendering/FrameGraph/FrameGraphBuilder.h>
#include <OvRendering/FrameGraph/FrameGraphHandle.h>
#include <OvRendering/FrameGraph/FrameGraphPass.h>
#include <OvRendering/FrameGraph/FrameGraphResources.h>
#include <OvRendering/FrameGraph/FrameGraphTexture.h>
#include <OvRendering/HAL/Framebuffer.h>
#include <OvRendering/HAL/Texture.h>

namespace OvRendering::FrameGraph
{
	/**
	* Represents a frame as a DAG of render passes with declared resource dependencies.
	* Passes are compiled (culled + sorted) and executed each frame.
	*/
	class FrameGraph
	{
	public:
		/**
		* Reset the graph for a new frame. Must be called before AddPass.
		*/
		void Reset(const Data::FrameDescriptor& p_frameDescriptor);

		/**
		* Add a typed pass to the graph.
		* @param p_name     Unique name for this pass.
		* @param p_setup    Called immediately to declare resource reads/writes.
		* @param p_execute  Called during Execute() if the pass is not culled.
		* @returns Reference to the pass node and its data.
		*/
		template<typename Data>
		std::pair<FrameGraphPass<Data>&, Data&> AddPass(
			std::string_view p_name,
			std::function<void(FrameGraphBuilder&, Data&)> p_setup,
			std::function<void(const FrameGraphResources&, const Data&)> p_execute
		);

		/**
		* Import an externally-owned texture into the graph (e.g. the output framebuffer's color attachment).
		* Imported textures are never culled and never destroyed by the graph.
		*/
		FrameGraphTextureHandle ImportTexture(std::string_view p_name, std::shared_ptr<HAL::Texture> p_texture);

		/**
		* Compile the graph: compute reference counts, cull unused passes, instantiate transient textures.
		*/
		void Compile();

		/**
		* Execute all non-culled passes in declaration order.
		*/
		void Execute();

		FrameGraphBlackboard& GetBlackboard();

	private:
		// Texture registry (indexed by handle id)
		std::vector<FrameGraphTextureDesc> m_textureDescs;
		std::vector<std::string>           m_textureNames;
		std::vector<bool>                  m_textureImported;
		std::vector<std::shared_ptr<HAL::Texture>> m_textures; // resolved during Compile()

		// Pass registry
		std::vector<std::unique_ptr<FrameGraphPassNode>> m_passes;

		// Persistent caches (survive Reset)
		std::unordered_map<std::string, std::shared_ptr<HAL::Texture>>   m_textureCache;
		std::unordered_map<std::string, std::unique_ptr<HAL::Framebuffer>> m_framebufferCache;

		uint32_t m_nextHandleId = 0;
		uint32_t m_frameWidth   = 0;
		uint32_t m_frameHeight  = 0;

		FrameGraphBlackboard m_blackboard;
	};
}

#include "OvRendering/FrameGraph/FrameGraph.inl"
