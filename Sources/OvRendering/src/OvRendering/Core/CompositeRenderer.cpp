/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <functional>
#include <ranges>
#include <stdexcept>
#include <tracy/Tracy.hpp>

#include <OvRendering/Core/CompositeRenderer.h>
#include <OvRendering/Utils/Profiling.h>

OvRendering::Core::CompositeRenderer::CompositeRenderer(Context::Driver& p_driver)
	: ABaseRenderer(p_driver)
{
}

void OvRendering::Core::CompositeRenderer::BeginFrame(const Data::FrameDescriptor& p_frameDescriptor)
{
	ZoneScoped;
	TracyGpuZone("BeginFrame");

	try
	{
		if (IsRecordingCommands() && !m_drawRecordingEnabled)
			throw std::logic_error("Command recording requires retained draw packets");
		if (IsRecordingCommands())
		{
			for (const auto& feature : m_features | std::views::values)
				if (feature->IsEnabled() && !feature->SupportsCommandRecording())
					throw std::invalid_argument("Enabled feature has not opted into command recording");
			for (const auto& pass : m_passes | std::views::values)
				if (pass.second->IsEnabled() && !pass.second->SupportsCommandRecording())
					throw std::invalid_argument("Enabled pass has not opted into command recording");
		}
		m_recordedMaterial.reset();
		ABaseRenderer::BeginFrame(p_frameDescriptor);

		for (const auto& feature : m_features | std::views::values)
		{
			if (feature->IsEnabled())
			{
				feature->OnBeginFrame(p_frameDescriptor);
			}
		}

		for (const auto& pass : m_passes | std::views::values)
		{
			if (pass.second->IsEnabled())
			{
				pass.second->OnBeginFrame(p_frameDescriptor);
			}
		}
	}
	catch (...)
	{
		if (m_commands) m_commands->FailRecording();
		throw;
	}
}

void OvRendering::Core::CompositeRenderer::DrawFrame()
{
	ZoneScoped;
	TracyGpuZone("DrawFrame");

	auto pso = CreatePipelineState();

	OVASSERT(!m_currentPass.has_value(), "Cannot draw frame while a pass is already being drawn. Did you forget to call EndFrame() ?");

	for (const auto& pass : m_passes | std::views::values)
	{
		m_currentPass = pass.second.get();

		BindTarget(m_frameDescriptor.outputBuffer ? &m_frameDescriptor.outputBuffer.value() : nullptr);
		SetViewport(0, 0, m_frameDescriptor.renderWidth, m_frameDescriptor.renderHeight);

		if (m_currentPass->IsEnabled())
		{
			try { m_currentPass->Draw(pso); }
			catch (...) { m_currentPass.reset(); if (m_commands) m_commands->FailRecording(); throw; }
		}

		m_currentPass.reset();
	}
}

void OvRendering::Core::CompositeRenderer::EndFrame()
{
	ZoneScoped;
	TracyGpuZone("EndFrame");

	try
	{
		for (const auto& pass : m_passes | std::views::values)
		{
			if (pass.second->IsEnabled())
			{
				pass.second->OnEndFrame();
			}
		}

		for (const auto& feature : m_features | std::views::values)
		{
			if (feature->IsEnabled())
			{
				feature->OnEndFrame();
			}
		}

		ClearDescriptors();
		m_recordedMaterial.reset();
		ABaseRenderer::EndFrame();
	}
	catch (...)
	{
		if (m_commands)
		{
			m_commands->FailRecording();
			ClearDescriptors();
			m_recordedMaterial.reset();
			if (IsDrawing()) ABaseRenderer::EndFrame();
		}
		throw;
	}
}

void OvRendering::Core::CompositeRenderer::DrawEntity(
	OvRendering::Data::PipelineState p_pso,
	const Entities::Drawable& p_drawable
)
{
	ZoneScoped;

	// Ensure the drawable is valid.
	// If not, skip the draw call and the attached features.
	if (!IsDrawable(p_drawable))
	{
		return;
	}

	const auto& currentPass = m_currentPass.value();
	const auto& passTypeId = typeid(currentPass);

	if (m_drawRecordingEnabled)
	{
		if (m_recording) throw std::logic_error("Nested draw recording is unsupported");
		if (typeid(p_drawable.mesh.value()) != typeid(Resources::Mesh))
			throw std::invalid_argument("Draw packets require standard retained Mesh geometry");
		for (const auto& feature : m_features | std::views::values)
			if (feature->IsEnabledFor(passTypeId) && !feature->SupportsDrawRecording())
				throw std::invalid_argument("Enabled feature has not opted into draw recording");
		DrawRecording recording;
		OvTools::Utils::OptRef<const RetainedTexturesDescriptor> retained;
		if (p_drawable.TryGetDescriptor<RetainedTexturesDescriptor>(retained)) recording.textures = retained->textures;
		m_recording = &recording;
		try
		{
			for (const auto& feature : m_features | std::views::values)
				if (feature->IsEnabledFor(passTypeId)) feature->OnBeforeDraw(p_pso, p_drawable);
			const auto features = p_drawable.featureSetOverride.has_value() ?
				OvTools::Utils::OptRef<const Data::FeatureSet>(p_drawable.featureSetOverride.value()) : std::nullopt;
			const auto binding = p_drawable.material->PrepareBinding(p_drawable.pass, features,
				m_recordingFallback2D.get(), m_recordingFallbackCube.get(), m_previousMaterialSignature);
			const bool stable = !m_previousMaterialSignature || binding.signature.stablePropertySignature != m_previousMaterialSignature->stablePropertySignature;
			const bool once = !m_previousMaterialSignature || binding.signature.singleUsePropertySignature != m_previousMaterialSignature->singleUsePropertySignature;
			auto geometry = PrepareGeometryDraw(p_pso, p_drawable);
			auto material = std::make_shared<Data::Material::Snapshot>(p_drawable.material->RecordCachedSnapshot(
				p_drawable.pass, features, m_recordingFallback2D, m_recordingFallbackCube,
				recording.textures, m_recordedMaterial.get(), stable, once));
			DrawPacket packet{ RecordPassState(), std::move(geometry), material, std::move(recording) };
			m_recording = nullptr;
			if (IsRecordingCommands())
			{
				SubmitRenderOperation([packet = std::move(packet)](Context::Driver& driver) {
					ExecutePassState(driver, packet.state);
					for (const auto& operation : packet.features.operations) operation();
					packet.material->Execute();
					const auto& geometry = packet.geometry;
					driver.Draw(geometry.pipelineState, geometry.mesh, geometry.primitiveMode, geometry.instances);
				});
			}
			else ExecuteDrawPacket(packet);
			m_recordedMaterial = std::move(material);
			m_previousMaterialSignature = binding.signature;
		}
		catch (...)
		{
			m_recording = nullptr;
			if (m_commands) m_commands->FailRecording();
			m_previousMaterialSignature.reset();
			m_recordedMaterial.reset();
			throw;
		}
		for (const auto& feature : m_features | std::views::values)
			if (feature->IsEnabledFor(passTypeId)) feature->OnAfterDraw(p_drawable);
		return;
	}

	for (const auto& feature : m_features | std::views::values)
	{
		if (feature->IsEnabledFor(passTypeId))
		{
			feature->OnBeforeDraw(p_pso, p_drawable);
		}
	}

	ABaseRenderer::DrawEntity(p_pso, p_drawable);

	for (const auto& feature : m_features | std::views::values)
	{
		if (feature->IsEnabledFor(passTypeId))
		{
			feature->OnAfterDraw(p_drawable);
		}
	}
}

void OvRendering::Core::CompositeRenderer::SetDrawRecordingEnabled(bool p_enabled)
{
	if (IsDrawing() || m_recording) throw std::logic_error("Toggle draw recording outside a frame");
	if (p_enabled && !m_recordingFallback2D)
	{
		auto texture2D = std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D);
		auto textureCube = std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_CUBE);
		const baregl::data::TextureDesc desc{ .width = 1, .height = 1,
			.minFilter = baregl::types::ETextureFilteringMode::NEAREST, .magFilter = baregl::types::ETextureFilteringMode::NEAREST,
			.horizontalWrap = baregl::types::ETextureWrapMode::REPEAT, .verticalWrap = baregl::types::ETextureWrapMode::REPEAT,
			.internalFormat = baregl::types::EInternalFormat::RGBA8, .useMipMaps = false };
		const std::array<uint8_t, 4> white{255,255,255,255};
		const std::array<uint8_t, 24> black{};
		texture2D->Allocate(desc); texture2D->Upload(white.data(), baregl::types::EFormat::RGBA, baregl::types::EPixelDataType::UNSIGNED_BYTE);
		textureCube->Allocate(desc); textureCube->Upload(black.data(), baregl::types::EFormat::RGBA, baregl::types::EPixelDataType::UNSIGNED_BYTE);
		m_recordingFallback2D = std::move(texture2D); m_recordingFallbackCube = std::move(textureCube);
	}
	m_drawRecordingEnabled = p_enabled;
	m_previousMaterialSignature.reset(); m_recordedMaterial.reset();
}

void OvRendering::Core::CompositeRenderer::RecordDrawOperation(std::function<void()> p_operation)
{
	if (!m_recording || !p_operation) throw std::logic_error("No active draw recording");
	m_recording->operations.push_back(std::move(p_operation));
}

void OvRendering::Core::CompositeRenderer::RetainDrawTexture(std::shared_ptr<baregl::Texture> p_texture)
{
	if (!m_recording) throw std::logic_error("No active draw recording");
	if (p_texture) m_recording->textures.push_back(std::move(p_texture));
}

void OvRendering::Core::CompositeRenderer::ExecuteDrawPacket(const DrawPacket& p_packet)
{
	ExecutePassState(p_packet.state);
	ZoneScopedN("Execute Draw Packet");
	for (const auto& operation : p_packet.features.operations) operation();
	p_packet.material->Execute();
	ExecuteGeometryDraw(p_packet.geometry);
}
