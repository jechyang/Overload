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
	using EngineFeature = OvCore::Rendering::EngineBufferRenderFeature;
	void UploadCamera(baregl::Buffer& buffer, const EngineFeature::CameraUploadCommand& command)
	{
		static_assert(sizeof(command) == 140);
		buffer.Upload(&command, baregl::data::BufferMemoryRange{.offset = sizeof(OvMaths::FMatrix4), .size = sizeof(command)});
	}
	void UploadFrame(baregl::Buffer& buffer, const EngineFeature::FrameUploadCommand& command)
	{
		static_assert(sizeof(command) == 144);
		static_assert(offsetof(EngineFeature::FrameUploadCommand, elapsedTime) == 140);
		buffer.Upload(&command, baregl::data::BufferMemoryRange{.offset = sizeof(OvMaths::FMatrix4), .size = sizeof(command)});
		buffer.Bind(baregl::types::EBufferType::UNIFORM, 0);
	}
	void UploadDraw(baregl::Buffer& buffer, const EngineFeature::DrawUploadCommand& command)
	{
		buffer.Upload(&command.modelMatrix, baregl::data::BufferMemoryRange{.offset = 0, .size = sizeof(command.modelMatrix)});
		buffer.Upload(&command.userMatrix, baregl::data::BufferMemoryRange{.offset = kUBOSize - sizeof(command.userMatrix), .size = sizeof(command.userMatrix)});
	}

}

OvCore::Rendering::EngineBufferRenderFeature::EngineBufferRenderFeature(
	OvRendering::Core::CompositeRenderer& p_renderer,
	OvRendering::Features::EFeatureExecutionPolicy p_executionPolicy
) :
	ARenderFeature(p_renderer, p_executionPolicy)
{
	m_engineBuffer = std::make_shared<baregl::Buffer>();
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
	UploadCamera(*m_engineBuffer, p_command);
}

void OvCore::Rendering::EngineBufferRenderFeature::ExecuteFrameUpload(const FrameUploadCommand& p_command)
{
	UploadFrame(*m_engineBuffer, p_command);
}

void OvCore::Rendering::EngineBufferRenderFeature::SetCamera(const OvRendering::Entities::Camera& p_camera)
{
	const auto command = PrepareCameraUpload(p_camera);
	m_renderer.SubmitRenderOperation([buffer = m_engineBuffer, command](auto&) { UploadCamera(*buffer, command); });
	if (m_frameUpload) m_frameUpload->camera = command;
}

void OvCore::Rendering::EngineBufferRenderFeature::OnBeginFrame(const OvRendering::Data::FrameDescriptor& p_frameDescriptor)
{
	OVASSERT(p_frameDescriptor.camera.has_value(), "Camera is not set in the frame descriptor");
	const auto currentTime = std::chrono::high_resolution_clock::now();
	const auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(currentTime - m_startTime);
	const auto command = PrepareFrameUpload(p_frameDescriptor.camera.value(), elapsed.count());
	m_renderer.SubmitRenderOperation([buffer = m_engineBuffer, command](auto&) { UploadFrame(*buffer, command); });
	m_frameUpload = command;
}

void OvCore::Rendering::EngineBufferRenderFeature::OnEndFrame()
{
	m_renderer.SubmitRenderOperation([buffer = m_engineBuffer](auto&) { buffer->Unbind(); });
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
	UploadDraw(*m_engineBuffer, p_command);
}

void OvCore::Rendering::EngineBufferRenderFeature::OnBeforeDraw(OvRendering::Data::PipelineState& p_pso, const OvRendering::Entities::Drawable& p_drawable)
{
	(void)p_pso;
	const auto command = PrepareDrawUpload(p_drawable);
	if (m_renderer.IsRecordingDraw())
	{
		if (!m_frameUpload) throw std::logic_error("Draw recording requires an active engine frame payload");
		m_renderer.RecordDrawOperation([buffer = m_engineBuffer, frame = *m_frameUpload, command] {
			UploadFrame(*buffer, frame);
			if (command) UploadDraw(*buffer, *command);
		});
	}
	else if (command) ExecuteDrawUpload(*command);
}
