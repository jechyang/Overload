/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <tracy/Tracy.hpp>

#include <OvRendering/Core/CompositeRenderer.h>
#include <OvRendering/HAL/Profiling.h>

OvRendering::Core::CompositeRenderer::CompositeRenderer(Context::Driver& p_driver)
	: ABaseRenderer(p_driver)
{
}

void OvRendering::Core::CompositeRenderer::BeginFrame(const Data::FrameDescriptor& p_frameDescriptor)
{
	ZoneScoped;
	TracyGpuZone("BeginFrame");
	ABaseRenderer::BeginFrame(p_frameDescriptor);
}

void OvRendering::Core::CompositeRenderer::DrawFrame()
{
	ZoneScoped;
	TracyGpuZone("DrawFrame");

	m_frameGraph.Reset(m_frameDescriptor);
	BuildFrameGraph(m_frameGraph);
	m_frameGraph.Compile();
	m_frameGraph.Execute();
}

void OvRendering::Core::CompositeRenderer::EndFrame()
{
	ZoneScoped;
	TracyGpuZone("EndFrame");

	ClearDescriptors();
	ABaseRenderer::EndFrame();
}

void OvRendering::Core::CompositeRenderer::DrawEntity(
	OvRendering::Data::PipelineState p_pso,
	const Entities::Drawable& p_drawable
)
{
	ZoneScoped;

	if (!IsDrawable(p_drawable))
	{
		return;
	}

	preDrawEntityEvent.Invoke(p_pso, p_drawable);
	ABaseRenderer::DrawEntity(p_pso, p_drawable);
	postDrawEntityEvent.Invoke(p_drawable);
}
