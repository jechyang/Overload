/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <cmath>
#include <stdexcept>
#include <tracy/Tracy.hpp>

#include "OvRendering/Features/LightingRenderFeature.h"
#include "OvRendering/Core/CompositeRenderer.h"

OvRendering::Features::LightingRenderFeature::LightingRenderFeature(
	Core::CompositeRenderer& p_renderer,
	OvRendering::Features::EFeatureExecutionPolicy p_executionPolicy,
	uint32_t p_bufferBindingPoint
) :
	ARenderFeature(p_renderer, p_executionPolicy),
	m_bufferBindingPoint(p_bufferBindingPoint)
{
	m_lightBuffer = std::make_shared<baregl::Buffer>();
}

bool IsLightInFrustum(const OvRendering::Entities::Light& p_light, const OvRendering::Data::Frustum& p_frustum)
{
	const auto& position = p_light.transform->GetWorldPosition();
	const auto effectRange = p_light.CalculateEffectRange();

	// We always consider lights that have an +inf range (Not necessary to test if they are in frustum)
	const bool isOmniscientLight = std::isinf(effectRange);

	return
		isOmniscientLight ||
		p_frustum.SphereInFrustum(position.x, position.y, position.z, effectRange);
}

void OvRendering::Features::LightingRenderFeature::Bind() const
{
	m_renderer.SubmitRenderOperation([buffer = m_lightBuffer, binding = m_bufferBindingPoint](auto&) { buffer->Bind(baregl::types::EBufferType::SHADER_STORAGE, binding); });
	m_boundLightBuffer = m_lightBuffer;
}

uint32_t OvRendering::Features::LightingRenderFeature::GetBufferBindingPoint() const
{
	return m_bufferBindingPoint;
}

OvRendering::Features::LightingRenderFeature::LightingUploadCommand
OvRendering::Features::LightingRenderFeature::PrepareLightingUpload(const LightingDescriptor& p_lights, const Entities::Camera& p_camera)
{
	ZoneScopedN("Prepare Lighting Upload");
	LightingUploadCommand command;
	command.matrices.reserve(p_lights.lights.size());
	const auto frustum = p_lights.frustumOverride ? p_lights.frustumOverride : p_camera.GetLightFrustum();
	for (auto light : p_lights.lights)
	{
		if (!frustum || IsLightInFrustum(light.get(), frustum.value()))
			command.matrices.push_back(light.get().GenerateMatrix());
	}
	return command;
}

void OvRendering::Features::LightingRenderFeature::ExecuteLightingUpload(const LightingUploadCommand& p_command)
{
	ZoneScopedN("Execute Lighting Upload");
	const auto matrices = std::span{p_command.matrices};
	if (m_lightBuffer->Allocate(matrices.size_bytes(), baregl::types::EAccessSpecifier::STREAM_DRAW))
		m_lightBuffer->Upload(matrices.data());
	Bind();
}

OvRendering::Features::LightingRenderFeature::LightBindingCommand
OvRendering::Features::LightingRenderFeature::RecordLightBinding() const
{
	if (!m_boundLightBuffer) throw std::logic_error("Light binding requires an active frame or explicit binding");
	return { m_boundLightBuffer, m_bufferBindingPoint };
}

void OvRendering::Features::LightingRenderFeature::ExecuteLightBinding(const LightBindingCommand& p_command)
{
	if (!p_command.buffer || p_command.buffer.use_count() == 0)
		throw std::invalid_argument("Light binding requires an owned buffer");
	p_command.buffer->Bind(baregl::types::EBufferType::SHADER_STORAGE, p_command.bindingPoint);
}

void OvRendering::Features::LightingRenderFeature::BindOverride(std::shared_ptr<baregl::Buffer> p_buffer)
{
	if (!p_buffer) throw std::invalid_argument("Light binding requires an owned buffer");
	m_renderer.SubmitRenderOperation([command = LightBindingCommand{p_buffer, m_bufferBindingPoint}](auto&) { ExecuteLightBinding(command); });
	m_boundLightBuffer = std::move(p_buffer);
}

void OvRendering::Features::LightingRenderFeature::OnBeginFrame(const Data::FrameDescriptor& p_frameDescriptor)
{
	OVASSERT(m_renderer.HasDescriptor<LightingDescriptor>(), "Cannot find LightingDescriptor attached to this renderer");
	auto command = PrepareLightingUpload(m_renderer.GetDescriptor<LightingDescriptor>(), p_frameDescriptor.camera.value());
	m_renderer.SubmitRenderOperation([buffer = m_lightBuffer, binding = m_bufferBindingPoint, command = std::move(command)](auto&) {
		const auto matrices = std::span{command.matrices};
		if (buffer->Allocate(matrices.size_bytes(), baregl::types::EAccessSpecifier::STREAM_DRAW)) buffer->Upload(matrices.data());
		buffer->Bind(baregl::types::EBufferType::SHADER_STORAGE, binding);
	});
	m_boundLightBuffer = m_lightBuffer;
}

void OvRendering::Features::LightingRenderFeature::OnBeforeDraw(Data::PipelineState&, const Entities::Drawable&)
{
	if (m_renderer.IsRecordingDraw())
	{
		const auto command = RecordLightBinding();
		m_renderer.RecordDrawOperation([command] { ExecuteLightBinding(command); });
	}
}

void OvRendering::Features::LightingRenderFeature::OnEndFrame()
{
	m_renderer.SubmitRenderOperation([buffer = m_lightBuffer](auto&) { buffer->Unbind(); });
	m_boundLightBuffer.reset();
}
