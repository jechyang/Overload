/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <tracy/Tracy.hpp>

#include <OvCore/Rendering/EngineBufferRenderFeature.h>
#include <OvCore/Rendering/EngineDrawableDescriptor.h>
#include <OvRendering/Core/CompositeRenderer.h>

namespace
{
	constexpr size_t kUBOSize =
		sizeof(OvMaths::FMatrix4) +	// Model matrix
		sizeof(OvMaths::FMatrix4) +	// View matrix
		sizeof(OvMaths::FMatrix4) +	// Projection matrix
		sizeof(OvMaths::FVector3) +	// Camera position
		sizeof(float) +				// Elapsed time
		sizeof(OvMaths::FMatrix4);	// User matrix
}

OvCore::Rendering::EngineBufferRenderFeature::EngineBufferRenderFeature(
	OvRendering::Core::CompositeRenderer& p_renderer,
	OvRendering::Features::EFeatureExecutionPolicy p_executionPolicy
) : 
	ARenderFeature(p_renderer, p_executionPolicy)
{
	m_engineBuffer = std::make_unique<baregl::Buffer>();
	m_engineBuffer->Allocate(kUBOSize, baregl::types::EAccessSpecifier::STREAM_DRAW);
	m_startTime = std::chrono::high_resolution_clock::now();
}

void OvCore::Rendering::EngineBufferRenderFeature::SetCamera(const OvRendering::Entities::Camera& p_camera)
{
	struct
	{
		OvMaths::FMatrix4 viewMatrix;
		OvMaths::FMatrix4 projectionMatrix;
		OvMaths::FVector3 cameraPosition;
	} uboDataPage{
		.viewMatrix = OvMaths::FMatrix4::Transpose(p_camera.GetViewMatrix()),
		.projectionMatrix = OvMaths::FMatrix4::Transpose(p_camera.GetProjectionMatrix()),
		.cameraPosition = p_camera.GetPosition()
	};

	m_engineBuffer->Upload(&uboDataPage, baregl::data::BufferMemoryRange{
		.offset = sizeof(OvMaths::FMatrix4), // Skip uploading the first matrix (Model matrix)
		.size = sizeof(uboDataPage)
	});
}

void OvCore::Rendering::EngineBufferRenderFeature::OnBeginFrame(const OvRendering::Data::FrameDescriptor& p_frameDescriptor)
{
	OVASSERT(p_frameDescriptor.camera.has_value(), "Camera is not set in the frame descriptor");

	auto currentTime = std::chrono::high_resolution_clock::now();
	auto elapsedTime = std::chrono::duration_cast<std::chrono::duration<float>>(currentTime - m_startTime);

	struct
	{
		OvMaths::FMatrix4 viewMatrix;
		OvMaths::FMatrix4 projectionMatrix;
		OvMaths::FVector3 cameraPosition;
		float elapsedTime;
	} uboDataPage{
		.viewMatrix = OvMaths::FMatrix4::Transpose(p_frameDescriptor.camera->GetViewMatrix()),
		.projectionMatrix = OvMaths::FMatrix4::Transpose(p_frameDescriptor.camera->GetProjectionMatrix()),
		.cameraPosition = p_frameDescriptor.camera->GetPosition(),
		.elapsedTime = elapsedTime.count()
	};

	m_engineBuffer->Upload(&uboDataPage, baregl::data::BufferMemoryRange{
		.offset = sizeof(OvMaths::FMatrix4), // Skip uploading the first matrix (Model matrix)
		.size = sizeof(uboDataPage)
	});

	m_engineBuffer->Bind(baregl::types::EBufferType::UNIFORM, 0);
}

void OvCore::Rendering::EngineBufferRenderFeature::OnEndFrame()
{
	m_engineBuffer->Unbind();
}

std::optional<OvCore::Rendering::EngineBufferRenderFeature::DrawUploadCommand>
OvCore::Rendering::EngineBufferRenderFeature::PrepareDrawUpload(const OvRendering::Entities::Drawable& p_drawable)
{
	ZoneScopedN("Prepare Engine Draw Upload");
	OvTools::Utils::OptRef<const EngineDrawableDescriptor> descriptor;
	if (!p_drawable.TryGetDescriptor<EngineDrawableDescriptor>(descriptor)) return std::nullopt;
	// The model is transposed, but the legacy user matrix is uploaded as-is.
	return DrawUploadCommand{ OvMaths::FMatrix4::Transpose(descriptor->modelMatrix), descriptor->userMatrix };
}

void OvCore::Rendering::EngineBufferRenderFeature::ExecuteDrawUpload(const DrawUploadCommand& p_command)
{
	ZoneScopedN("Execute Engine Draw Upload");
	m_engineBuffer->Upload(&p_command.modelMatrix, baregl::data::BufferMemoryRange{
		.offset = 0,
		.size = sizeof(p_command.modelMatrix)
	});
	m_engineBuffer->Upload(&p_command.userMatrix, baregl::data::BufferMemoryRange{
		.offset = kUBOSize - sizeof(p_command.userMatrix),
		.size = sizeof(p_command.userMatrix)
	});
}

void OvCore::Rendering::EngineBufferRenderFeature::OnBeforeDraw(OvRendering::Data::PipelineState& p_pso, const OvRendering::Entities::Drawable& p_drawable)
{
	(void)p_pso;
	if (const auto command = PrepareDrawUpload(p_drawable))
	{
		if (m_renderer.IsRecordingDraw())
			m_renderer.RecordDrawOperation([this, value = *command] { ExecuteDrawUpload(value); });
		else ExecuteDrawUpload(*command);
	}
}
