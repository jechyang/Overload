/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/FrameGraph/FrameGraphBuilder.h>

OvRendering::FrameGraph::FrameGraphBuilder::FrameGraphBuilder(
	FrameGraphPassNode& p_pass,
	std::vector<FrameGraphTextureDesc>& p_textureDescs,
	std::vector<std::string>& p_textureNames,
	uint32_t& p_nextHandleId
) :
	m_pass(p_pass),
	m_textureDescs(p_textureDescs),
	m_textureNames(p_textureNames),
	m_nextHandleId(p_nextHandleId)
{
}

OvRendering::FrameGraph::FrameGraphTextureHandle OvRendering::FrameGraph::FrameGraphBuilder::Create(
	std::string_view p_name,
	const FrameGraphTextureDesc& p_desc
)
{
	FrameGraphTextureHandle handle{ m_nextHandleId++ };
	m_textureDescs.push_back(p_desc);
	m_textureNames.emplace_back(p_name);
	m_pass.writes.push_back(handle);
	return handle;
}

OvRendering::FrameGraph::FrameGraphTextureHandle OvRendering::FrameGraph::FrameGraphBuilder::Read(
	FrameGraphTextureHandle p_handle
)
{
	if (p_handle.IsValid())
	{
		m_pass.reads.push_back(p_handle);
	}
	return p_handle;
}

OvRendering::FrameGraph::FrameGraphTextureHandle OvRendering::FrameGraph::FrameGraphBuilder::Write(
	FrameGraphTextureHandle p_handle
)
{
	if (p_handle.IsValid())
	{
		m_pass.writes.push_back(p_handle);
	}
	return p_handle;
}

void OvRendering::FrameGraph::FrameGraphBuilder::SetAsOutput(FrameGraphTextureHandle p_handle)
{
	m_pass.isOutput = true;
}

OvRendering::FrameGraph::FrameGraphBufferHandle OvRendering::FrameGraph::FrameGraphBuilder::Read(
	FrameGraphBufferHandle p_handle
)
{
	if (p_handle.IsValid())
	{
		m_pass.bufferReads.push_back(p_handle);
	}
	return p_handle;
}

OvRendering::FrameGraph::FrameGraphBufferHandle OvRendering::FrameGraph::FrameGraphBuilder::Write(
	FrameGraphBufferHandle p_handle
)
{
	if (p_handle.IsValid())
	{
		m_pass.bufferWrites.push_back(p_handle);
	}
	return p_handle;
}
