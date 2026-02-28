/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <string_view>
#include <vector>

#include <OvRendering/FrameGraph/FrameGraphHandle.h>
#include <OvRendering/FrameGraph/FrameGraphTexture.h>
#include <OvRendering/FrameGraph/FrameGraphPass.h>

namespace OvRendering::FrameGraph
{
	/**
	* Interface provided to pass setup callbacks.
	* Used to declare resource reads, writes, and creations.
	*/
	class FrameGraphBuilder
	{
	public:
		FrameGraphBuilder(
			FrameGraphPassNode& p_pass,
			std::vector<FrameGraphTextureDesc>& p_textureDescs,
			std::vector<std::string>& p_textureNames,
			uint32_t& p_nextHandleId
		);

		/**
		* Create a new transient texture resource.
		*/
		FrameGraphTextureHandle Create(std::string_view p_name, const FrameGraphTextureDesc& p_desc);

		/**
		* Declare a read dependency on an existing texture resource.
		*/
		FrameGraphTextureHandle Read(FrameGraphTextureHandle p_handle);

		/**
		* Declare a write dependency on an existing texture resource.
		*/
		FrameGraphTextureHandle Write(FrameGraphTextureHandle p_handle);

		/**
		* Mark a resource as a frame output (prevents it from being culled).
		*/
		void SetAsOutput(FrameGraphTextureHandle p_handle);

		/**
		* Declare a read dependency on an existing buffer resource.
		*/
		FrameGraphBufferHandle Read(FrameGraphBufferHandle p_handle);

		/**
		* Declare a write dependency on an existing buffer resource.
		*/
		FrameGraphBufferHandle Write(FrameGraphBufferHandle p_handle);

	private:
		FrameGraphPassNode& m_pass;
		std::vector<FrameGraphTextureDesc>& m_textureDescs;
		std::vector<std::string>& m_textureNames;
		uint32_t& m_nextHandleId;
	};
}
