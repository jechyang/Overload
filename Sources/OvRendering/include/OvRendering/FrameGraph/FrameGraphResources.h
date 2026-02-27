/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <OvRendering/FrameGraph/FrameGraphHandle.h>
#include <OvRendering/FrameGraph/FrameGraphBlackboard.h>
#include <OvRendering/HAL/Texture.h>
#include <OvRendering/HAL/Framebuffer.h>

namespace OvRendering::FrameGraph
{
	/**
	* Interface provided to pass execute callbacks.
	* Provides access to resolved textures, framebuffers, and the blackboard.
	*/
	class FrameGraphResources
	{
	public:
		FrameGraphResources(
			const std::vector<std::shared_ptr<HAL::Texture>>& p_textures,
			std::unordered_map<std::string, std::unique_ptr<HAL::Framebuffer>>& p_framebufferCache,
			const std::vector<std::string>& p_textureNames,
			FrameGraphBlackboard& p_blackboard,
			uint32_t p_frameWidth,
			uint32_t p_frameHeight
		);

		/**
		* Returns the resolved texture for the given handle.
		*/
		HAL::Texture& GetTexture(FrameGraphTextureHandle p_handle) const;

		/**
		* Returns (or creates) a framebuffer with the given color and optional depth attachments.
		*/
		HAL::Framebuffer& GetFramebuffer(
			FrameGraphTextureHandle p_color,
			FrameGraphTextureHandle p_depth = {}
		) const;

		uint32_t GetFrameWidth() const;
		uint32_t GetFrameHeight() const;

		FrameGraphBlackboard& GetBlackboard() const;

	private:
		const std::vector<std::shared_ptr<HAL::Texture>>& m_textures;
		std::unordered_map<std::string, std::unique_ptr<HAL::Framebuffer>>& m_framebufferCache;
		const std::vector<std::string>& m_textureNames;
		FrameGraphBlackboard& m_blackboard;
		uint32_t m_frameWidth;
		uint32_t m_frameHeight;
	};
}
