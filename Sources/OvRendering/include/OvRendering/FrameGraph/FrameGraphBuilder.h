/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include <OvRendering/FrameGraph/FrameGraphHandle.h>
#include <OvRendering/FrameGraph/FrameGraphTexture.h>
#include <OvRendering/FrameGraph/FrameGraphPass.h>
#include <OvRendering/Settings/EAccessSpecifier.h>
#include <OvRendering/HAL/UniformBuffer.h>
#include <OvRendering/HAL/ShaderStorageBuffer.h>

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
			std::vector<std::string>& p_bufferNames,
			std::vector<bool>& p_bufferImported,
			std::vector<std::shared_ptr<void>>& p_buffers,
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

		/**
		* Create a new transient buffer resource.
		* @param p_name Debug name for the buffer
		* @param p_size Size in bytes
		* @param p_usage Access specifier (e.g. STREAM_DRAW, DYNAMIC_DRAW, etc.)
		*/
		template<typename BufferType>
		FrameGraphBufferHandle CreateBuffer(
			std::string_view p_name,
			size_t p_size,
			OvRendering::Settings::EAccessSpecifier p_usage
		)
		{
			FrameGraphBufferHandle handle{ m_nextHandleId++ };
			m_bufferNames.emplace_back(p_name);
			m_bufferImported.push_back(false);

			// Create the actual buffer object
			auto buffer = std::make_unique<BufferType>();
			buffer->Allocate(p_size, p_usage);
			m_buffers.push_back(std::shared_ptr<void>(std::move(buffer)));

			m_pass.bufferWrites.push_back(handle);
			return handle;
		}

	private:
		FrameGraphPassNode& m_pass;
		std::vector<FrameGraphTextureDesc>& m_textureDescs;
		std::vector<std::string>& m_textureNames;
		std::vector<std::string>& m_bufferNames;
		std::vector<bool>& m_bufferImported;
		std::vector<std::shared_ptr<void>>& m_buffers;
		uint32_t& m_nextHandleId;
	};
}
