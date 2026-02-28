/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/FrameGraph/FrameGraph.h>

namespace OvRendering::FrameGraph
{
	template<typename Data>
	std::pair<FrameGraphPass<Data>&, Data&> FrameGraph::AddPass(
		std::string_view p_name,
		std::function<void(FrameGraphBuilder&, Data&)> p_setup,
		std::function<void(const FrameGraphResources&, Data&)> p_execute
	)
	{
		auto* node = new FrameGraphPass<Data>();
		node->name = p_name;
		node->executeFn = std::move(p_execute);

		FrameGraphBuilder builder(*node, m_textureDescs, m_textureNames, m_bufferNames, m_bufferImported, m_buffers, m_nextHandleId);
		p_setup(builder, node->data);

		m_passes.emplace_back(node);
		return { *node, node->data };
	}

	template<typename BufferType>
	FrameGraphBufferHandle FrameGraph::ImportBuffer(std::string_view p_name, BufferType* p_buffer)
	{
		FrameGraphBufferHandle handle{ m_nextHandleId++ };
		m_bufferNames.emplace_back(p_name);
		m_bufferImported.push_back(true);
		m_buffers.push_back(std::shared_ptr<void>(p_buffer, [](void*){})); // null deleter
		return handle;
	}
}
