/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <cstddef>
#include <stdexcept>
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

OvCore::Rendering::EngineBufferRenderFeature::CameraUploadCommand
OvCore::Rendering::EngineBufferRenderFeature::PrepareCameraUpload(const OvRendering::Entities::Camera& p_camera)
{
	ZoneScopedN("Prepare Engine Camera Upload");
	return { OvMaths::FMatrix4::Transpose(p_camera.GetViewMatrix()),
		OvMaths::FMatrix4::Transpose(p_camera.GetProjectionMatrix()), p_camera.GetPosition() };
}

OvCore::Rendering::EngineBufferRenderFeature::FrameUploadCommand
OvCore::Rendering::EngineBufferRenderFeature::PrepareFrameUpload(const OvRendering::Entities::Camera& p_camera, float p_elapsedTime)
{
	return { PrepareCameraUpload(p_camera), p_elapsedTime };
}

void OvCore::Rendering::EngineBufferRenderFeature::ExecuteCameraUpload(const CameraUploadCommand& p_command)
{
	ZoneScopedN("Execute Engine Camera Upload");
	static_assert(sizeof(CameraUploadCommand) == 140);
	m_engineBuffer->Upload(&p_command, baregl::data::BufferMemoryRange{
		.offset = sizeof(OvMaths::FMatrix4), .size = sizeof(p_command) });
}

void OvCore::Rendering::EngineBufferRenderFeature::ExecuteFrameUpload(const FrameUploadCommand& p_command)
{
	ZoneScopedN("Execute Engine Frame Upload");
	static_assert(sizeof(FrameUploadCommand) == 144);
	static_assert(offsetof(FrameUploadCommand, elapsedTime) == 140);
	m_engineBuffer->Upload(&p_command, baregl::data::BufferMemoryRange{
		.offset = sizeof(OvMaths::FMatrix4), .size = sizeof(p_command) });
	m_engineBuffer->Bind(baregl::types::EBufferType::UNIFORM, 0);
}

void OvCore::Rendering::EngineBufferRenderFeature::SetCamera(const OvRendering::Entities::Camera& p_camera)
{
	const auto command = PrepareCameraUpload(p_camera);
	ExecuteCameraUpload(command);
	if (m_frameUpload) m_frameUpload->camera = command;
}

void OvCore::Rendering::EngineBufferRenderFeature::OnBeginFrame(const OvRendering::Data::FrameDescriptor& p_frameDescriptor)
{
	OVASSERT(p_frameDescriptor.camera.has_value(), "Camera is not set in the frame descriptor");
	const auto currentTime = std::chrono::high_resolution_clock::now();
	const auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(currentTime - m_startTime);
	const auto command = PrepareFrameUpload(p_frameDescriptor.camera.value(), elapsed.count());
	ExecuteFrameUpload(command);
	m_frameUpload = command;
}

void OvCore::Rendering::EngineBufferRenderFeature::OnEndFrame()
{
	m_engineBuffer->Unbind();
	m_frameUpload.reset();
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
	const auto command = PrepareDrawUpload(p_drawable);
	if (m_renderer.IsRecordingDraw())
	{
		if (!m_frameUpload) throw std::logic_error("Draw recording requires an active engine frame payload");
		m_renderer.RecordDrawOperation([this, frame = *m_frameUpload, command] {
			ExecuteFrameUpload(frame);
			if (command) ExecuteDrawUpload(*command);
		});
	}
	else if (command) ExecuteDrawUpload(*command);
}
