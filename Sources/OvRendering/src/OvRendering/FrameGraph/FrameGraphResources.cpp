/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <format>

#include <OvRendering/FrameGraph/FrameGraphResources.h>
#include <OvRendering/Settings/EFramebufferAttachment.h>
#include <OvRendering/Settings/ETextureType.h>

OvRendering::FrameGraph::FrameGraphResources::FrameGraphResources(
	const std::vector<std::shared_ptr<HAL::Texture>>& p_textures,
	const std::vector<std::shared_ptr<void>>& p_buffers,
	std::unordered_map<std::string, std::unique_ptr<HAL::Framebuffer>>& p_framebufferCache,
	const std::vector<std::string>& p_textureNames,
	const std::vector<std::string>& p_bufferNames,
	FrameGraphBlackboard& p_blackboard,
	uint32_t p_frameWidth,
	uint32_t p_frameHeight
) :
	m_textures(p_textures),
	m_buffers(p_buffers),
	m_framebufferCache(p_framebufferCache),
	m_textureNames(p_textureNames),
	m_bufferNames(p_bufferNames),
	m_blackboard(p_blackboard),
	m_frameWidth(p_frameWidth),
	m_frameHeight(p_frameHeight)
{
}

std::shared_ptr<OvRendering::HAL::Texture> OvRendering::FrameGraph::FrameGraphResources::GetTexture(
	FrameGraphTextureHandle p_handle
) const
{
	return m_textures[p_handle.id];
}

OvRendering::HAL::Framebuffer& OvRendering::FrameGraph::FrameGraphResources::GetFramebuffer(
	FrameGraphTextureHandle p_color,
	FrameGraphTextureHandle p_depth
) const
{
	using namespace OvRendering::Settings;

	const std::string colorName = p_color.IsValid() ? m_textureNames[p_color.id] : "none";
	const std::string depthName = p_depth.IsValid()  ? m_textureNames[p_depth.id]  : "none";
	const std::string key = std::format("{}:{}", colorName, depthName);

	auto it = m_framebufferCache.find(key);
	if (it == m_framebufferCache.end())
	{
		auto fbo = std::make_unique<HAL::Framebuffer>(key);

		if (p_color.IsValid())
		{
			fbo->Attach(m_textures[p_color.id], EFramebufferAttachment::COLOR);
		}

		if (p_depth.IsValid())
		{
			fbo->Attach(m_textures[p_depth.id], EFramebufferAttachment::DEPTH);
		}

		fbo->Validate();
		it = m_framebufferCache.emplace(key, std::move(fbo)).first;
	}

	return *it->second;
}

uint32_t OvRendering::FrameGraph::FrameGraphResources::GetFrameWidth() const
{
	return m_frameWidth;
}

uint32_t OvRendering::FrameGraph::FrameGraphResources::GetFrameHeight() const
{
	return m_frameHeight;
}

OvRendering::FrameGraph::FrameGraphBlackboard& OvRendering::FrameGraph::FrameGraphResources::GetBlackboard() const
{
	return m_blackboard;
}

template<typename BufferType>
BufferType& OvRendering::FrameGraph::FrameGraphResources::GetBuffer(FrameGraphBufferHandle p_handle) const
{
	if (p_handle.id >= m_buffers.size() || !m_buffers[p_handle.id])
	{
		throw std::runtime_error("Invalid buffer handle or buffer not found");
	}
	auto* buffer = static_cast<BufferType*>(m_buffers[p_handle.id].get());
	if (!buffer)
	{
		throw std::runtime_error("Buffer type mismatch");
	}
	return *buffer;
}

// Explicit instantiations for common buffer types
template OvRendering::HAL::UniformBuffer& OvRendering::FrameGraph::FrameGraphResources::GetBuffer<OvRendering::HAL::UniformBuffer>(FrameGraphBufferHandle) const;
template OvRendering::HAL::ShaderStorageBuffer& OvRendering::FrameGraph::FrameGraphResources::GetBuffer<OvRendering::HAL::ShaderStorageBuffer>(FrameGraphBufferHandle) const;
