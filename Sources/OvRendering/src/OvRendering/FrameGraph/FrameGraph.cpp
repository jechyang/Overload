/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <format>

#include <OvRendering/FrameGraph/FrameGraph.h>
#include <OvRendering/Settings/ETextureFilteringMode.h>
#include <OvRendering/Settings/ETextureWrapMode.h>
#include <OvRendering/Settings/ETextureType.h>
#include <OvRendering/Settings/TextureDesc.h>

void OvRendering::FrameGraph::FrameGraph::Reset(const Data::FrameDescriptor& p_frameDescriptor)
{
	m_passes.clear();
	m_textureDescs.clear();
	m_textureNames.clear();
	m_textureImported.clear();
	m_textures.clear();

	m_bufferNames.clear();
	m_bufferImported.clear();
	m_buffers.clear();

	m_blackboard.Clear();
	m_nextHandleId = 0;
	m_frameWidth  = p_frameDescriptor.renderWidth;
	m_frameHeight = p_frameDescriptor.renderHeight;
}

OvRendering::FrameGraph::FrameGraphTextureHandle OvRendering::FrameGraph::FrameGraph::ImportTexture(
	std::string_view p_name,
	std::shared_ptr<HAL::Texture> p_texture
)
{
	FrameGraphTextureHandle handle{ m_nextHandleId++ };
	m_textureDescs.push_back({}); // placeholder desc
	m_textureNames.emplace_back(p_name);
	m_textureImported.push_back(true);
	m_textures.push_back(std::move(p_texture));
	return handle;
}

void OvRendering::FrameGraph::FrameGraph::Compile()
{
	// Resize textures to cover all handles
	const uint32_t totalTextures = m_nextHandleId;
	m_textures.resize(totalTextures);
	m_textureImported.resize(totalTextures, false);

	// Resize buffers to cover all handles (buffers use the same handle ID space)
	m_buffers.resize(totalTextures);
	m_bufferImported.resize(totalTextures, false);

	// --- Reference counting for culling ---
	// refCount per resource = number of passes that read it
	std::vector<int> resourceRefCount(totalTextures, 0);

	for (const auto& pass : m_passes)
	{
		// Count texture reads
		for (auto handle : pass->reads)
		{
			if (handle.IsValid() && handle.id < totalTextures)
			{
				++resourceRefCount[handle.id];
			}
		}
		// Count buffer reads
		for (auto handle : pass->bufferReads)
		{
			if (handle.IsValid() && handle.id < totalTextures)
			{
				++resourceRefCount[handle.id];
			}
		}

		// Passes marked as output are never culled
		if (pass->isOutput)
		{
			pass->refCount = 1;
		}
		else
		{
			// refCount = number of written resources that are referenced
			for (auto handle : pass->writes)
			{
				if (handle.IsValid() && handle.id < totalTextures)
				{
					pass->refCount += resourceRefCount[handle.id];
				}
			}
			for (auto handle : pass->bufferWrites)
			{
				if (handle.IsValid() && handle.id < totalTextures)
				{
					pass->refCount += resourceRefCount[handle.id];
				}
			}
		}
	}

	// Propagate culling: passes with refCount == 0 are culled
	// (simple single-pass; good enough for our use case)
	for (auto& pass : m_passes)
	{
		if (!pass->isOutput && pass->refCount == 0)
		{
			pass->culled = true;
		}
	}

	// --- Instantiate transient textures for non-culled passes ---
	for (uint32_t i = 0; i < totalTextures; ++i)
	{
		// Skip buffers (they are always imported, not created by FrameGraph)
		if (i < m_bufferImported.size() && m_bufferImported[i])
		{
			continue;
		}
		if (i < m_textureImported.size() && m_textureImported[i])
		{
			continue; // imported texture, already set
		}

		// Check if any non-culled pass writes this texture
		bool needed = false;
		for (const auto& pass : m_passes)
		{
			if (pass->culled) continue;
			for (auto handle : pass->writes)
			{
				if (handle.id == i)
				{
					needed = true;
					break;
				}
			}
			if (needed) break;
		}

		if (!needed) continue;

		const auto& desc = m_textureDescs[i];
		const auto& name = m_textureNames[i];

		// Check persistent cache
		auto it = m_textureCache.find(name);
		if (it != m_textureCache.end())
		{
			auto& tex = it->second;
			// Resize if dimensions changed
			const auto& existingDesc = tex->GetDesc();
			if (existingDesc.width != desc.width || existingDesc.height != desc.height)
			{
				tex->Resize(desc.width, desc.height);
				// Invalidate any framebuffers that use this texture
				std::erase_if(m_framebufferCache, [&](const auto& kv) {
					return kv.first.find(name) != std::string::npos;
				});
			}
			m_textures[i] = tex;
		}
		else
		{
			using namespace OvRendering::Settings;

			auto tex = std::make_shared<HAL::Texture>(
				ETextureType::TEXTURE_2D,
				name
			);

			TextureDesc texDesc{
				.width          = std::max(1u, desc.width),
				.height         = std::max(1u, desc.height),
				.minFilter      = desc.minFilter,
				.magFilter      = desc.magFilter,
				.horizontalWrap = desc.wrapS,
				.verticalWrap   = desc.wrapT,
				.internalFormat = desc.internalFormat,
				.useMipMaps     = desc.generateMipmaps,
				.mutableDesc    = MutableTextureDesc{}
			};

			tex->Allocate(texDesc);
			m_textureCache[name] = tex;
			m_textures[i] = tex;
		}
	}
}

void OvRendering::FrameGraph::FrameGraph::Execute()
{
	FrameGraphResources resources(
		m_textures,
		m_buffers,
		m_framebufferCache,
		m_textureNames,
		m_bufferNames,
		m_blackboard,
		m_frameWidth,
		m_frameHeight
	);

	for (const auto& pass : m_passes)
	{
		if (!pass->culled)
		{
			pass->Execute(resources);
		}
	}
}

OvRendering::FrameGraph::FrameGraphBlackboard& OvRendering::FrameGraph::FrameGraph::GetBlackboard()
{
	return m_blackboard;
}
