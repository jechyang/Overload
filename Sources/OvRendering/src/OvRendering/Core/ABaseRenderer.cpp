/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <functional>
#include <stdexcept>

#include <baregl/Texture.h>
#include <tracy/Tracy.hpp>

#include <OvRendering/Core/ABaseRenderer.h>
#include <OvRendering/Utils/Profiling.h>
#include <OvRendering/Resources/Loaders/TextureLoader.h>

std::atomic_bool OvRendering::Core::ABaseRenderer::s_isDrawing{ false };

namespace
{
	const OvRendering::Entities::Camera kDefaultCamera;

	constexpr auto kUnitQuadVertices = std::to_array<OvRendering::Geometry::Vertex>({
		{ {-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f} }, // Bottom-left
		{ { 1.0f, -1.0f, 0.0f}, {1.0f, 0.0f} }, // Bottom-right
		{ { 1.0f,  1.0f, 0.0f}, {1.0f, 1.0f} }, // Top-right
		{ {-1.0f,  1.0f, 0.0f}, {0.0f, 1.0f} }  // Top-left
	});

	constexpr auto kUnitQuadIndices = std::to_array<uint32_t>({
		0, 1, 2, // First triangle
		0, 2, 3  // Second triangle
	});

	constexpr auto kWhitePixel = std::to_array<uint8_t>({ 255, 255, 255, 255 });
	constexpr auto kBlackPixel = std::to_array<uint8_t, 6 * 4>({ 0 });
}

OvRendering::Core::ABaseRenderer::ABaseRenderer(Context::Driver& p_driver) :
	m_driver(p_driver),
	m_isDrawing(false),
	m_emptyTexture2D{ baregl::types::ETextureType::TEXTURE_2D },
	m_emptyTextureCube{ baregl::types::ETextureType::TEXTURE_CUBE },
	m_unitQuad(kUnitQuadVertices, kUnitQuadIndices)
{
	const auto kEmptyTextureDesc = baregl::data::TextureDesc{
		.width = 1,
		.height = 1,
		.minFilter = baregl::types::ETextureFilteringMode::NEAREST,
		.magFilter = baregl::types::ETextureFilteringMode::NEAREST,
		.horizontalWrap = baregl::types::ETextureWrapMode::REPEAT,
		.verticalWrap = baregl::types::ETextureWrapMode::REPEAT,
		.internalFormat = baregl::types::EInternalFormat::RGBA8,
		.useMipMaps = false
	};

	m_emptyTexture2D.Allocate(kEmptyTextureDesc);
	m_emptyTexture2D.Upload(kWhitePixel.data(), baregl::types::EFormat::RGBA, baregl::types::EPixelDataType::UNSIGNED_BYTE);

	m_emptyTextureCube.Allocate(kEmptyTextureDesc);
	m_emptyTextureCube.Upload(kBlackPixel.data(), baregl::types::EFormat::RGBA, baregl::types::EPixelDataType::UNSIGNED_BYTE);
}

void OvRendering::Core::ABaseRenderer::BeginFrame(const Data::FrameDescriptor& p_frameDescriptor)
{
	ZoneScoped;

	OVASSERT(!s_isDrawing, "Cannot call BeginFrame() when previous frame hasn't finished.");
	OVASSERT(p_frameDescriptor.IsValid(), "Invalid FrameDescriptor!");

	m_previousMaterialSignature.reset();

	m_frameDescriptor = p_frameDescriptor;

	BindTarget(p_frameDescriptor.outputBuffer ? &p_frameDescriptor.outputBuffer.value() : nullptr);

	m_basePipelineState = m_driver.CreatePipelineState();
	SetViewport(0, 0, p_frameDescriptor.renderWidth, p_frameDescriptor.renderHeight);

	Clear(
		p_frameDescriptor.camera->GetClearColorBuffer(),
		p_frameDescriptor.camera->GetClearDepthBuffer(),
		p_frameDescriptor.camera->GetClearStencilBuffer(),
		{ p_frameDescriptor.camera.value().GetClearColor(), 1.0f }
	);

	p_frameDescriptor.camera->CacheMatrices(p_frameDescriptor.renderWidth, p_frameDescriptor.renderHeight);

	m_isDrawing = true;
	s_isDrawing.store(true);
}

void OvRendering::Core::ABaseRenderer::EndFrame()
{
	ZoneScoped;

	OVASSERT(s_isDrawing, "Cannot call EndFrame() before calling BeginFrame()");

	BindTarget(nullptr);

	m_isDrawing = false;
	s_isDrawing.store(false);
}

const OvRendering::Data::FrameDescriptor& OvRendering::Core::ABaseRenderer::GetFrameDescriptor() const
{
	OVASSERT(m_isDrawing, "Cannot call GetFrameDescriptor() outside of a frame");
	return m_frameDescriptor;
}

OvRendering::Data::PipelineState OvRendering::Core::ABaseRenderer::CreatePipelineState() const
{
	return m_basePipelineState;
}

bool OvRendering::Core::ABaseRenderer::IsDrawing() const
{
	return m_isDrawing;
}

void OvRendering::Core::ABaseRenderer::SetViewport(uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height)
{
	m_passState.viewport = { p_x, p_y, p_width, p_height };
	SubmitRenderOperation([=](Context::Driver& driver) { driver.SetViewport(p_x, p_y, p_width, p_height); });
}

void OvRendering::Core::ABaseRenderer::Clear(
	bool p_colorBuffer,
	bool p_depthBuffer,
	bool p_stencilBuffer,
	const OvMaths::FVector4& p_color
)
{
	ZoneScoped;
	const auto command = PrepareClear(p_colorBuffer, p_depthBuffer, p_stencilBuffer, p_color);
	SubmitRenderOperation([command](Context::Driver& driver) {
		ExecutePassState(driver, command.state);
		driver.Clear(command.colorBuffer, command.depthBuffer, command.stencilBuffer, command.color);
	});
}

void OvRendering::Core::ABaseRenderer::Blit(
	OvRendering::Data::PipelineState p_pso,
	baregl::Framebuffer& p_src,
	baregl::Framebuffer& p_dst,
	OvRendering::Data::Material& p_material,
	OvRendering::Settings::EBlitFlags p_flags
)
{
	BlitWithRetainedTextures(p_pso, p_src, p_dst, p_material, p_flags, {});
}

void OvRendering::Core::ABaseRenderer::BlitWithRetainedTextures(
	Data::PipelineState p_pso, baregl::Framebuffer& p_src, baregl::Framebuffer& p_dst,
	Data::Material& p_material, Settings::EBlitFlags p_flags,
	std::span<const std::shared_ptr<baregl::Texture>> p_textures)
{
	ZoneScoped;

	const auto [srcWidth, srcHeight] = p_src.GetSize();

	if (OvRendering::Settings::IsFlagSet(OvRendering::Settings::EBlitFlags::RESIZE_DST_TO_MATCH_SRC, p_flags))
	{
		p_dst.Resize(srcWidth, srcHeight);
	}

	if (OvRendering::Settings::IsFlagSet(OvRendering::Settings::EBlitFlags::FILL_INPUT_TEXTURE, p_flags))
	{
		const auto colorTex = p_src.GetAttachment<baregl::Texture>(baregl::types::EFramebufferAttachment::COLOR);
		OVASSERT(colorTex.has_value(), "Invalid color attachment");
		p_material.SetProperty("_InputTexture", &colorTex.value().get());
	}

	OvRendering::Entities::Drawable blit;
	blit.mesh = m_unitQuad;
	blit.material = p_material;
	RetainedTexturesDescriptor retained{ { p_textures.begin(), p_textures.end() } };
	if (OvRendering::Settings::IsFlagSet(OvRendering::Settings::EBlitFlags::FILL_INPUT_TEXTURE, p_flags))
		retained.textures.push_back(p_src.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR));
	if (!retained.textures.empty()) blit.AddDescriptor<RetainedTexturesDescriptor>(std::move(retained));

	if (OvRendering::Settings::IsFlagSet(OvRendering::Settings::EBlitFlags::USE_MATERIAL_STATE_MASK, p_flags))
	{
		blit.stateMask = p_material.GenerateStateMask();
	}
	else
	{
		blit.stateMask.depthWriting = false;
		blit.stateMask.colorWriting = true;
		blit.stateMask.blendable = false;
		blit.stateMask.frontfaceCulling = false;
		blit.stateMask.backfaceCulling = false;
		blit.stateMask.depthTest = false;
	}

	BindTarget(&p_dst);

	if (OvRendering::Settings::IsFlagSet(OvRendering::Settings::EBlitFlags::UPDATE_VIEWPORT_SIZE, p_flags))
	{
		const auto [dstWidth, dstHeight] = p_dst.GetSize();
		SetViewport(0, 0, dstWidth, dstHeight);
	}

	DrawEntity(p_pso, blit);
	BindTarget(nullptr);
}

bool OvRendering::Core::ABaseRenderer::IsDrawable(const Entities::Drawable& p_drawable) const
{
	return
		p_drawable.mesh &&
		p_drawable.material &&
		p_drawable.material->IsValid()&&
		p_drawable.material->SupportsProjectionMode(m_frameDescriptor.camera->GetProjectionMode()) &&
		p_drawable.material->GetGPUInstances() > 0;
}

OvRendering::Core::ABaseRenderer::GeometryDrawCommand OvRendering::Core::ABaseRenderer::PrepareGeometryDraw(
	Data::PipelineState p_pso,
	const Entities::Drawable& p_drawable
)
{
	ZoneScopedN("Prepare Geometry Draw");
	// CPU-only. Standard immutable geometry is retained; other frame bindings stay external.
	if (!p_drawable.mesh || !p_drawable.material || p_drawable.material->GetGPUInstances() <= 0)
	{
		throw std::invalid_argument("Geometry draw requires a mesh, material and positive instance count");
	}

	p_pso.depthWriting = p_drawable.stateMask.depthWriting;
	p_pso.colorWriting.mask = p_drawable.stateMask.colorWriting ? 0xFF : 0x00;
	p_pso.blending = p_drawable.stateMask.blendable;
	p_pso.culling = p_drawable.stateMask.frontfaceCulling || p_drawable.stateMask.backfaceCulling;
	p_pso.depthTest = p_drawable.stateMask.depthTest;

	if (p_pso.culling)
	{
		if (p_drawable.stateMask.backfaceCulling && p_drawable.stateMask.frontfaceCulling)
		{
			p_pso.cullFace = baregl::types::ECullFace::FRONT_AND_BACK;
		}
		else
		{
			p_pso.cullFace =
				p_drawable.stateMask.backfaceCulling ?
				baregl::types::ECullFace::BACK :
				baregl::types::ECullFace::FRONT;
		}
	}

	// Do not bypass virtual behavior of custom meshes, including Mesh subclasses.
	std::shared_ptr<const Resources::IMesh> retained;
	if (typeid(p_drawable.mesh.value()) == typeid(Resources::Mesh))
	{
		retained = static_cast<const Resources::Mesh&>(p_drawable.mesh.value()).RetainGeometry();
	}
	return { p_pso, retained, retained ? *retained : p_drawable.mesh.value(), p_drawable.primitiveMode,
		static_cast<uint32_t>(p_drawable.material->GetGPUInstances()) };
}

void OvRendering::Core::ABaseRenderer::ExecuteGeometryDraw(const GeometryDrawCommand& p_command)
{
	ZoneScopedN("Execute Geometry Draw");
	// Material, textures, uniforms and feature buffers have already been bound.
	// No Scene, Actor, Drawable or Material is inspected during geometry submission.
	m_driver.Draw(p_command.pipelineState, p_command.mesh, p_command.primitiveMode, p_command.instances);
}

void OvRendering::Core::ABaseRenderer::DrawEntity(
	OvRendering::Data::PipelineState p_pso,
	const Entities::Drawable& p_drawable
)
{
	ZoneScoped;

	OVASSERT(IsDrawable(p_drawable), "Submitted an entity that isn't properly configured!");

	if (IsRecordingCommands()) throw std::logic_error("Immediate geometry path cannot record owned frame commands");
	const auto geometryCommand = PrepareGeometryDraw(p_pso, p_drawable);

	const auto signature = p_drawable.material->Bind(
		p_drawable.pass,
		p_drawable.featureSetOverride.has_value() ?
			OvTools::Utils::OptRef<const Data::FeatureSet>(p_drawable.featureSetOverride.value()) :
			std::nullopt,
		&m_emptyTexture2D,
		&m_emptyTextureCube,
		m_previousMaterialSignature
	);

	const bool uploadStableProperties = !m_previousMaterialSignature.has_value() || signature.stablePropertySignature != m_previousMaterialSignature->stablePropertySignature;
	const bool uploadSingleUseProperties = !m_previousMaterialSignature.has_value() || signature.singleUsePropertySignature != m_previousMaterialSignature->singleUsePropertySignature;

	if (uploadStableProperties || uploadSingleUseProperties)
	{
		p_drawable.material->UploadProperties(
			uploadStableProperties,
			uploadSingleUseProperties,
			&m_emptyTexture2D,
			&m_emptyTextureCube
		);
	}

	m_previousMaterialSignature = signature;

	ExecuteGeometryDraw(geometryCommand);

	p_drawable.material->Unbind();
}

void OvRendering::Core::ABaseRenderer::BindTarget(baregl::Framebuffer* p_target)
{
	auto target = p_target ? std::optional{p_target->RecordTarget()} : std::nullopt;
	if (m_commands) SubmitRenderOperation([target](Context::Driver&) {
		if (target) target->Bind(); else baregl::Framebuffer::BindDefault();
	});
	else if (p_target) p_target->Bind();
	else baregl::Framebuffer::BindDefault();
	m_passState.target = std::move(target);
}

void OvRendering::Core::ABaseRenderer::BindTarget(baregl::Framebuffer::Snapshot p_target)
{
    SubmitRenderOperation([target = p_target](Context::Driver&) { target.Bind(); });
    m_passState.target = std::move(p_target);
}

OvRendering::Core::ABaseRenderer::PassStateCommand OvRendering::Core::ABaseRenderer::RecordPassState() const
{
	return m_passState;
}

void OvRendering::Core::ABaseRenderer::ExecutePassState(const PassStateCommand& p_command)
{
	ExecutePassState(m_driver, p_command);
}

void OvRendering::Core::ABaseRenderer::ExecutePassState(Context::Driver& p_driver, const PassStateCommand& p_command)
{
	if (p_command.target) p_command.target->Bind();
	else baregl::Framebuffer::BindDefault();
	const auto& viewport = p_command.viewport;
	p_driver.SetViewport(viewport.x, viewport.y, viewport.width, viewport.height);
}

OvRendering::Core::ABaseRenderer::ClearCommand OvRendering::Core::ABaseRenderer::PrepareClear(
	bool p_colorBuffer, bool p_depthBuffer, bool p_stencilBuffer, const OvMaths::FVector4& p_color) const
{
	return { RecordPassState(), p_colorBuffer, p_depthBuffer, p_stencilBuffer, p_color };
}

void OvRendering::Core::ABaseRenderer::ExecuteClear(const ClearCommand& p_command)
{
	ExecutePassState(p_command.state);
	m_driver.Clear(p_command.colorBuffer, p_command.depthBuffer, p_command.stencilBuffer, p_command.color);
}

void OvRendering::Core::ABaseRenderer::BeginCommandRecording()
{
	if (IsDrawing() || m_commands) throw std::logic_error("Begin command recording outside a frame");
	m_commands = std::make_unique<Data::RenderCommandList>();
}

OvRendering::Data::RenderCommandList OvRendering::Core::ABaseRenderer::FinishCommandRecording(bool p_deferEncoding)
{
	if (IsDrawing() || !m_commands) throw std::logic_error("Finish recording after EndFrame");
	if (!p_deferEncoding) m_commands->Seal(m_parallelCommandRecording ? m_commandRecordingJobs : nullptr);
	auto result = std::move(*m_commands);
	m_commands.reset();
	return result;
}

void OvRendering::Core::ABaseRenderer::CancelCommandRecording()
{
	if (IsDrawing() || !m_commands) throw std::logic_error("Cancel recording after EndFrame");
	if (m_commands->GetState() != Data::RenderCommandList::State::FAILED) m_commands->Cancel();
	m_commands.reset();
}

void OvRendering::Core::ABaseRenderer::SubmitRenderOperation(Data::RenderCommandList::Operation p_operation)
{
	if (!p_operation) throw std::invalid_argument("Empty render operation");
	if (m_commands)
	{
		if (m_commands->GetState() == Data::RenderCommandList::State::FAILED) return; // Permit CPU EndFrame cleanup.
		m_commands->Append(std::move(p_operation));
	}
	else p_operation(m_driver);
}

OvRendering::Utils::GpuScope OvRendering::Core::ABaseRenderer::ProfileGpuScope(const tracy::SourceLocationData* p_source)
{ return Utils::GpuScope(m_commands.get(), p_source); }

void OvRendering::Core::ABaseRenderer::SubmitCommandList(Data::RenderCommandList commands)
{
    m_previousMaterialSignature.reset();
    if (m_commands) m_commands->Append(std::move(commands));
    else commands.Execute(m_driver);
}
