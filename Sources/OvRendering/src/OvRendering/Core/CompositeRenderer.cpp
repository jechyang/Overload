/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <glad.h>

#include <tracy/Tracy.hpp>

#include <OvRendering/Core/CompositeRenderer.h>
#include <OvRendering/HAL/Profiling.h>
#include <OvDebug/Logger.h>

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

void OvRendering::Core::CompositeRenderer::UploadMatricesAndDraw(
	OvRendering::Data::PipelineState p_pso,
	const Entities::Drawable& p_drawable,
	OvRendering::HAL::UniformBuffer& p_engineUBO,
	const OvMaths::FMatrix4& p_modelMatrix,
	const OvMaths::FMatrix4& p_userMatrix,
	size_t p_uboSize
)
{
	// Pre-compute transposed model matrix for UBO upload
	const auto transposedModelMatrix = OvMaths::FMatrix4::Transpose(p_modelMatrix);

	// Upload model matrix to offset 0
	p_engineUBO.Upload(&transposedModelMatrix, OvRendering::HAL::BufferMemoryRange{
		.offset = 0,
		.size = sizeof(transposedModelMatrix)
	});

	#if _DEBUG
	if (GLenum err = glGetError(); err != GL_NO_ERROR)
	{
		OVLOG_ERROR("[UploadMatricesAndDraw] After model matrix upload: OpenGL error " + std::to_string(err));
	}
	#endif

	// Upload user matrix to offset kUBOSize - sizeof(matrix)
	p_engineUBO.Upload(&p_userMatrix, OvRendering::HAL::BufferMemoryRange{
		.offset = p_uboSize - sizeof(p_userMatrix),
		.size = sizeof(p_userMatrix)
	});

	#if _DEBUG
	if (GLenum err = glGetError(); err != GL_NO_ERROR)
	{
		OVLOG_ERROR("[UploadMatricesAndDraw] After user matrix upload: OpenGL error " + std::to_string(err) +
			", offset=" + std::to_string(p_uboSize - sizeof(p_userMatrix)) +
			", size=" + std::to_string(sizeof(p_userMatrix)) +
			", uboSize=" + std::to_string(p_uboSize));
	}
	#endif

	// Bind UBO and draw
	p_engineUBO.Bind(0);

	#if _DEBUG
	if (GLenum err = glGetError(); err != GL_NO_ERROR)
	{
		OVLOG_ERROR("[UploadMatricesAndDraw] After UBO bind: OpenGL error " + std::to_string(err));
	}
	#endif

	// Draw the drawable (bypassing preDrawEntityEvent/postDrawEntityEvent)
	// This is a low-level helper for DrawModelWithSingleMaterial implementations
	DrawEntity(p_pso, p_drawable);
}
