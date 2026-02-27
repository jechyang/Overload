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
		std::function<void(const FrameGraphResources&, const Data&)> p_execute
	)
	{
		auto* node = new FrameGraphPass<Data>();
		node->name = p_name;
		node->executeFn = std::move(p_execute);

		FrameGraphBuilder builder(*node, m_textureDescs, m_textureNames, m_nextHandleId);
		p_setup(builder, node->data);

		m_passes.emplace_back(node);
		return { *node, node->data };
	}
}
