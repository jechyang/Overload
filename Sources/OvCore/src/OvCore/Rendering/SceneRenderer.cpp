/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <cmath>
#include <format>
#include <optional>
#include <ranges>
#include <tracy/Tracy.hpp>

#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvCore/ParticleSystem/CParticleSystem.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Rendering/EngineDrawableDescriptor.h>
#include <OvCore/Rendering/FrameGraphData/EngineBufferData.h>
#include <OvCore/Rendering/FrameGraphData/LightingData.h>
#include <OvCore/Rendering/FrameGraphData/ShadowData.h>
#include <OvCore/Rendering/FrameGraphData/ReflectionData.h>
#include <OvCore/Rendering/PostProcessRenderPass.h>
#include <OvCore/Rendering/SceneRenderer.h>
#include <OvCore/ResourceManagement/ShaderManager.h>

#include <OvRendering/Data/Frustum.h>
#include <OvRendering/Entities/Light.h>
#include <OvRendering/Features/LightingRenderFeature.h>
#include <OvRendering/HAL/Profiling.h>
#include <OvRendering/HAL/UniformBuffer.h>
#include <OvRendering/HAL/ShaderStorageBuffer.h>
#include <OvRendering/Settings/EAccessSpecifier.h>
#include <OvRendering/Settings/ELightType.h>

namespace
{
	using namespace OvCore::Rendering;

	constexpr size_t kUBOSize =
		sizeof(OvMaths::FMatrix4) +  // Model matrix
		sizeof(OvMaths::FMatrix4) +  // View matrix
		sizeof(OvMaths::FMatrix4) +  // Projection matrix
		sizeof(OvMaths::FVector3) +  // Camera position
		sizeof(float) +              // Elapsed time
		sizeof(OvMaths::FMatrix4);   // User matrix

	OvRendering::Features::LightingRenderFeature::LightSet FindActiveLights(const OvCore::SceneSystem::Scene& p_scene)
	{
		OvRendering::Features::LightingRenderFeature::LightSet lights;
		for (auto light : p_scene.GetFastAccessComponents().lights)
		{
			if (light->owner.IsActive())
				lights.push_back(std::ref(light->GetData()));
		}
		return lights;
	}

	std::vector<std::reference_wrapper<OvCore::ECS::Components::CReflectionProbe>> FindActiveReflectionProbes(
		const OvCore::SceneSystem::Scene& p_scene)
	{
		std::vector<std::reference_wrapper<OvCore::ECS::Components::CReflectionProbe>> probes;
		for (auto probe : p_scene.GetFastAccessComponents().reflectionProbes)
		{
			if (probe->owner.IsActive())
				probes.push_back(*probe);
		}
		return probes;
	}

	bool IsLightInFrustum(const OvRendering::Entities::Light& p_light, const OvRendering::Data::Frustum& p_frustum)
	{
		const auto& position = p_light.transform->GetWorldPosition();
		const auto effectRange = p_light.CalculateEffectRange();
		return std::isinf(effectRange) ||
			p_frustum.SphereInFrustum(position.x, position.y, position.z, effectRange);
	}
}

// ============================================================
// Constructor
// ============================================================

OvCore::Rendering::SceneRenderer::SceneRenderer(OvRendering::Context::Driver& p_driver, bool p_stencilWrite)
	: OvRendering::Core::CompositeRenderer(p_driver)
	, m_stencilWrite(p_stencilWrite)
{
	m_engineBuffer = std::make_unique<OvRendering::HAL::UniformBuffer>();
	m_engineBuffer->Allocate(kUBOSize, OvRendering::Settings::EAccessSpecifier::STREAM_DRAW);
	m_startTime = std::chrono::high_resolution_clock::now();

	m_lightBuffer = std::make_unique<OvRendering::HAL::ShaderStorageBuffer>();

	m_postProcessPass = std::make_unique<PostProcessRenderPass>(*this);

	// Upload model/user matrices to the engine UBO before each draw call.
	preDrawEntityEvent += [this](OvRendering::Data::PipelineState&, const OvRendering::Entities::Drawable& drawable)
	{
		OvTools::Utils::OptRef<const EngineDrawableDescriptor> descriptor;
		if (drawable.TryGetDescriptor<EngineDrawableDescriptor>(descriptor))
		{
			const auto modelMatrix = OvMaths::FMatrix4::Transpose(descriptor->modelMatrix);
			m_engineBuffer->Upload(&modelMatrix, OvRendering::HAL::BufferMemoryRange{
				.offset = 0,
				.size = sizeof(modelMatrix)
			});
			m_engineBuffer->Upload(&descriptor->userMatrix, OvRendering::HAL::BufferMemoryRange{
				.offset = kUBOSize - sizeof(modelMatrix),
				.size = sizeof(modelMatrix)
			});
		}
	};
}

// ============================================================
// BeginFrame
// ============================================================

void OvCore::Rendering::SceneRenderer::BeginFrame(const OvRendering::Data::FrameDescriptor& p_frameDescriptor)
{
	ZoneScoped;

	OVASSERT(HasDescriptor<SceneDescriptor>(), "Cannot find SceneDescriptor attached to this renderer");

	auto& sceneDescriptor = GetDescriptor<SceneDescriptor>();
	const bool frustumLightCulling = p_frameDescriptor.camera.value().HasFrustumLightCulling();

	AddDescriptor<OvRendering::Features::LightingRenderFeature::LightingDescriptor>({
		FindActiveLights(sceneDescriptor.scene),
		frustumLightCulling ? sceneDescriptor.frustumOverride : std::nullopt
	});

	AddDescriptor<FrameGraphData::ReflectionData>({
		FindActiveReflectionProbes(sceneDescriptor.scene)
	});

	OvRendering::Core::CompositeRenderer::BeginFrame(p_frameDescriptor);

	const auto& viewMatrix = p_frameDescriptor.camera.value().GetViewMatrix();
	const auto rightRow = OvMaths::FMatrix4::GetRow(viewMatrix, 0);
	const auto upRow    = OvMaths::FMatrix4::GetRow(viewMatrix, 1);
	const OvMaths::FVector3 cameraRight{ rightRow.x, rightRow.y, rightRow.z };
	const OvMaths::FVector3 cameraUp   { upRow.x,    upRow.y,    upRow.z    };

	AddDescriptor<SceneDrawablesDescriptor>({
		ParseScene(SceneParsingInput{
			.scene       = sceneDescriptor.scene,
			.cameraRight = cameraRight,
			.cameraUp    = cameraUp
		})
	});

	AddDescriptor<SceneFilteredDrawablesDescriptor>({
		FilterDrawables(
			GetDescriptor<SceneDrawablesDescriptor>(),
			SceneDrawablesFilteringInput{
				.camera = p_frameDescriptor.camera.value(),
				.frustumOverride = sceneDescriptor.frustumOverride,
				.overrideMaterial = sceneDescriptor.overrideMaterial,
				.fallbackMaterial = sceneDescriptor.fallbackMaterial,
				.requiredVisibilityFlags = EVisibilityFlags::GEOMETRY
			}
		)
	});
}

// ============================================================
// Helper methods
// ============================================================

void OvCore::Rendering::SceneRenderer::_SetCameraUBO(const OvRendering::Entities::Camera& p_camera)
{
	struct { OvMaths::FMatrix4 view; OvMaths::FMatrix4 proj; OvMaths::FVector3 pos; } d{
		OvMaths::FMatrix4::Transpose(p_camera.GetViewMatrix()),
		OvMaths::FMatrix4::Transpose(p_camera.GetProjectionMatrix()),
		p_camera.GetPosition()
	};
	m_engineBuffer->Upload(&d, OvRendering::HAL::BufferMemoryRange{
		.offset = sizeof(OvMaths::FMatrix4),
		.size = sizeof(d)
	});
}

void OvCore::Rendering::SceneRenderer::_BindLightBuffer()
{
	if (m_lightBuffer)
		m_lightBuffer->Bind(0);
}

void OvCore::Rendering::SceneRenderer::_BindShadowUniforms(OvRendering::Data::Material& p_material)
{
	if (!p_material.IsShadowReceiver() ||
		!p_material.HasProperty("_ShadowMap") ||
		!p_material.HasProperty("_LightSpaceMatrix"))
		return;

	if (!m_frameGraph.GetBlackboard().Has<FrameGraphData::ShadowData>())
		return;

	const auto& sd = m_frameGraph.GetBlackboard().Get<FrameGraphData::ShadowData>();
	if (!sd.shadowMap) return;

	// Get texture handle from shared_ptr
	auto* textureHandle = sd.shadowMap.get();
	p_material.SetProperty("_ShadowMap", textureHandle, true);
	p_material.SetProperty("_LightSpaceMatrix", sd.lightSpaceMatrix, true);
}

void OvCore::Rendering::SceneRenderer::_BindReflectionUniforms(
	OvRendering::Data::Material& p_material,
	const OvRendering::Entities::Drawable& p_drawable
)
{
	if (!p_material.IsReflectionReceiver() || !p_material.HasProperty("_EnvironmentMap"))
		return;

	if (!m_frameGraph.GetBlackboard().Has<FrameGraphData::ReflectionData>())
		return;

	const auto& rd = m_frameGraph.GetBlackboard().Get<FrameGraphData::ReflectionData>();

	OvTools::Utils::OptRef<const OvCore::ECS::Components::CReflectionProbe> targetProbe;

	if (p_drawable.HasDescriptor<EngineDrawableDescriptor>())
	{
		const auto& engineDesc = p_drawable.GetDescriptor<EngineDrawableDescriptor>();
		const auto& mm = engineDesc.modelMatrix;
		const OvMaths::FVector3 drawablePos{ mm.data[3], mm.data[7], mm.data[11] };

		struct Best {
			OvTools::Utils::OptRef<const OvCore::ECS::Components::CReflectionProbe> probe;
			float distance = std::numeric_limits<float>::max();
		} bestLocal, bestGlobal;

		for (auto& probeRef : rd.reflectionProbes)
		{
			const auto& probe = probeRef.get();
			const auto probePos = probe.owner.transform.GetWorldPosition() + probe.GetCapturePosition();
			const float dist = OvMaths::FVector3::Distance(drawablePos, probePos);
			const bool isLocal = probe.GetInfluencePolicy() ==
				OvCore::ECS::Components::CReflectionProbe::EInfluencePolicy::LOCAL;

			// Local probes take priority over global
			if (!isLocal && bestLocal.probe.has_value()) continue;

			auto& best = isLocal ? bestLocal : bestGlobal;
			if (dist < best.distance)
				best = { probe, dist };
		}

		targetProbe = bestLocal.probe ? bestLocal.probe : bestGlobal.probe;
	}

	p_material.SetProperty(
		"_EnvironmentMap",
		targetProbe.has_value() ?
			targetProbe->GetCubemap().get() :
			static_cast<OvRendering::HAL::TextureHandle*>(nullptr),
		true
	);

	if (targetProbe)
		targetProbe->_GetUniformBuffer().Bind(1);
}

// ============================================================
// BuildFrameGraph
// ============================================================

void OvCore::Rendering::SceneRenderer::BuildFrameGraph(OvRendering::FrameGraph::FrameGraph& p_fg)
{
	using namespace OvRendering::FrameGraph;
	using namespace OvRendering;

	// ---- Import buffer resources into FrameGraph ----
	// Engine UBO
	auto engineUBOHandle = p_fg.ImportBuffer("EngineUBO", m_engineBuffer.get());

	// Light SSBO
	auto lightSSBOHandle = p_fg.ImportBuffer("LightSSBO", m_lightBuffer.get());

	// ---- Pass 1: EngineBuffer ----
	struct EngineBufferPassData {
		FrameGraphBufferHandle engineUBO;
	};
	p_fg.AddPass<EngineBufferPassData>(
		"EngineBuffer",
		[engineUBOHandle](FrameGraphBuilder& builder, EngineBufferPassData& data) {
			// Declare write to engine UBO
			data.engineUBO = builder.Write(engineUBOHandle);
			// Mark as output to prevent culling
			builder.SetAsOutput({});
		},
		[this](const FrameGraphResources& resources, const EngineBufferPassData& data)
		{
			ZoneScoped;
			OVASSERT(m_frameDescriptor.camera.has_value(), "Camera is not set");

			auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(
				std::chrono::high_resolution_clock::now() - m_startTime).count();

			struct { OvMaths::FMatrix4 view; OvMaths::FMatrix4 proj; OvMaths::FVector3 pos; float time; } d{
				OvMaths::FMatrix4::Transpose(m_frameDescriptor.camera->GetViewMatrix()),
				OvMaths::FMatrix4::Transpose(m_frameDescriptor.camera->GetProjectionMatrix()),
				m_frameDescriptor.camera->GetPosition(),
				elapsed
			};

			// Get buffer via handle (for consistency, though we still have m_engineBuffer member)
			// In the future, this would be the only way to access the buffer
			auto& engineUBO = resources.GetBuffer<HAL::UniformBuffer>(data.engineUBO);
			engineUBO.Upload(&d, OvRendering::HAL::BufferMemoryRange{
				.offset = sizeof(OvMaths::FMatrix4), .size = sizeof(d)
			});
			engineUBO.Bind(0);

			// For backward compatibility, still write to Blackboard
			resources.GetBlackboard().Put(FrameGraphData::EngineBufferData{ &engineUBO });
		}
	);

	// ---- Pass 2: Lighting ----
	struct LightingPassData {
		FrameGraphBufferHandle lightSSBO;
	};
	p_fg.AddPass<LightingPassData>(
		"Lighting",
		[lightSSBOHandle](FrameGraphBuilder& builder, LightingPassData& data) {
			// Declare write to light SSBO
			data.lightSSBO = builder.Write(lightSSBOHandle);
			// Mark as output to prevent culling
			builder.SetAsOutput({});
		},
		[this](const FrameGraphResources& resources, const LightingPassData& data)
		{
			ZoneScoped;
			OVASSERT(HasDescriptor<OvRendering::Features::LightingRenderFeature::LightingDescriptor>(),
				"Cannot find LightingDescriptor");

			auto& ld = GetDescriptor<OvRendering::Features::LightingRenderFeature::LightingDescriptor>();
			auto frustum = ld.frustumOverride ? ld.frustumOverride : m_frameDescriptor.camera->GetLightFrustum();

			std::vector<OvMaths::FMatrix4> lightMatrices;
			lightMatrices.reserve(ld.lights.size());
			for (auto light : ld.lights)
			{
				if (!frustum || IsLightInFrustum(light.get(), frustum.value()))
					lightMatrices.push_back(light.get().GenerateMatrix());
			}

			auto& lightSSBO = resources.GetBuffer<HAL::ShaderStorageBuffer>(data.lightSSBO);

			// Upload light data if there are lights, otherwise allocate a minimal buffer
			if (!lightMatrices.empty())
			{
				const auto view = std::span{ lightMatrices };
				lightSSBO.Allocate(view.size_bytes(), OvRendering::Settings::EAccessSpecifier::STREAM_DRAW);
				lightSSBO.Upload(view.data());
			}
			else
			{
				// Allocate a minimal buffer to avoid upload assert on empty buffer
				lightSSBO.Allocate(sizeof(OvMaths::FMatrix4), OvRendering::Settings::EAccessSpecifier::STREAM_DRAW);
			}

			lightSSBO.Bind(0);

			// For backward compatibility, still write to Blackboard
			resources.GetBlackboard().Put(FrameGraphData::LightingData{ &lightSSBO });
		}
	);

	// ---- Pass 3: Shadow ----
	struct ShadowPassData {
		std::vector<FrameGraphTextureHandle> shadowMaps;
	};
	p_fg.AddPass<ShadowPassData>(
		"Shadow",
		[](FrameGraphBuilder& builder, ShadowPassData& data) {
			// Shadow maps will be created dynamically in the execute callback
			// based on the number of lights that cast shadows
			builder.SetAsOutput({});
		},
		[this](const FrameGraphResources& resources, const ShadowPassData& data)
		{
			ZoneScoped;
			TracyGpuZone("ShadowPass");

			OVASSERT(HasDescriptor<SceneDescriptor>(), "Cannot find SceneDescriptor");
			OVASSERT(HasDescriptor<OvRendering::Features::LightingRenderFeature::LightingDescriptor>(),
				"Cannot find LightingDescriptor");

			auto& ld = GetDescriptor<OvRendering::Features::LightingRenderFeature::LightingDescriptor>();
			auto& scene = GetDescriptor<SceneDescriptor>().scene;

			const auto shadowShader = OVSERVICE(OvCore::ResourceManagement::ShaderManager)
				.GetResource(":Shaders\\ShadowFallback.ovfx");
			OVASSERT(shadowShader, "Cannot find shadow shader");
			OvCore::Resources::Material shadowMaterial;
			shadowMaterial.SetShader(shadowShader);

			auto pso = CreatePipelineState();
			uint8_t lightIndex = 0;
			constexpr uint8_t kMaxShadowMaps = 1;

			std::optional<FrameGraphData::ShadowData> shadowData;

			for (auto lightRef : ld.lights)
			{
				auto& light = lightRef.get();
				if (!light.castShadows || lightIndex >= kMaxShadowMaps) continue;
				if (light.type != OvRendering::Settings::ELightType::DIRECTIONAL) continue;

				// Prepare light camera and matrix
				light.PrepareForShadowRendering(m_frameDescriptor);
				_SetCameraUBO(light.shadowCamera.value());

				// Create shadow map texture via FrameGraph (if not already created)
				const std::string shadowMapName = std::format("ShadowMap_{}", static_cast<int>(lightIndex));

				// Create texture description for shadow map
				OvRendering::FrameGraph::FrameGraphTextureDesc shadowDesc;
				shadowDesc.debugName = shadowMapName;
				shadowDesc.width = static_cast<uint32_t>(light.shadowMapResolution);
				shadowDesc.height = static_cast<uint32_t>(light.shadowMapResolution);
				shadowDesc.internalFormat = OvRendering::Settings::EInternalFormat::DEPTH_COMPONENT;
				shadowDesc.minFilter = OvRendering::Settings::ETextureFilteringMode::LINEAR;
				shadowDesc.magFilter = OvRendering::Settings::ETextureFilteringMode::LINEAR;
				shadowDesc.wrapS = OvRendering::Settings::ETextureWrapMode::CLAMP_TO_BORDER;
				shadowDesc.wrapT = OvRendering::Settings::ETextureWrapMode::CLAMP_TO_BORDER;
				shadowDesc.generateMipmaps = false;

				// Create framebuffer with the shadow map texture
				auto shadowFbo = std::make_unique<OvRendering::HAL::Framebuffer>(shadowMapName);
				auto shadowTex = std::make_shared<OvRendering::HAL::Texture>(
					OvRendering::Settings::ETextureType::TEXTURE_2D,
					shadowMapName
				);

				OvRendering::Settings::MutableTextureDesc mutableDesc;
				mutableDesc.format = OvRendering::Settings::EFormat::DEPTH_COMPONENT;
				mutableDesc.type = OvRendering::Settings::EPixelDataType::FLOAT;

				OvRendering::Settings::TextureDesc texDesc;
				texDesc.width = static_cast<uint32_t>(light.shadowMapResolution);
				texDesc.height = static_cast<uint32_t>(light.shadowMapResolution);
				texDesc.minFilter = OvRendering::Settings::ETextureFilteringMode::LINEAR;
				texDesc.magFilter = OvRendering::Settings::ETextureFilteringMode::LINEAR;
				texDesc.horizontalWrap = OvRendering::Settings::ETextureWrapMode::CLAMP_TO_BORDER;
				texDesc.verticalWrap = OvRendering::Settings::ETextureWrapMode::CLAMP_TO_BORDER;
				texDesc.internalFormat = OvRendering::Settings::EInternalFormat::DEPTH_COMPONENT;
				texDesc.useMipMaps = false;
				texDesc.mutableDesc = mutableDesc;

				shadowTex->Allocate(texDesc);
				shadowTex->SetBorderColor(OvMaths::FVector4::One);

				shadowFbo->Attach<OvRendering::HAL::Texture>(shadowTex, OvRendering::Settings::EFramebufferAttachment::DEPTH);
				shadowFbo->Validate();
				shadowFbo->SetTargetDrawBuffer(std::nullopt);
				shadowFbo->SetTargetReadBuffer(std::nullopt);

				// Set the shadow map texture to the light
				light.SetShadowMapTexture(shadowTex);

				// Render shadow map
				shadowFbo->Bind();
				SetViewport(0, 0, light.shadowMapResolution, light.shadowMapResolution);
				Clear(true, true, true);

				for (auto mr : scene.GetFastAccessComponents().modelRenderers)
				{
					auto& actor = mr->owner;
					if (!actor.IsActive()) continue;
					auto model = mr->GetModel();
					if (!model) continue;
					auto matRenderer = actor.GetComponent<OvCore::ECS::Components::CMaterialRenderer>();
					if (!matRenderer || !matRenderer->HasVisibilityFlags(EVisibilityFlags::SHADOW)) continue;

					const auto& mats = matRenderer->GetMaterials();
					const auto& modelMatrix = actor.transform.GetWorldMatrix();

					for (auto mesh : model->GetMeshes())
					{
						auto mat = mats.at(mesh->GetMaterialIndex());
						if (!mat || !mat->IsValid() || !mat->IsShadowCaster()) continue;

						const std::string shadowPass = "SHADOW_PASS";
						auto& targetMat = mat->HasPass(shadowPass) ? *mat : shadowMaterial;

						OvRendering::Entities::Drawable d;
						d.mesh = *mesh;
						d.material = targetMat;
						d.stateMask = targetMat.GenerateStateMask();
						d.stateMask.blendable = false;
						d.stateMask.depthTest = true;
						d.stateMask.colorWriting = false;
						d.stateMask.depthWriting = true;
						d.stateMask.frontfaceCulling = false;
						d.stateMask.backfaceCulling = false;
						d.pass = shadowPass;
						d.AddDescriptor<EngineDrawableDescriptor>({ modelMatrix, matRenderer->GetUserMatrix() });
						DrawEntity(pso, d);
					}
				}

				shadowFbo->Unbind();

				// Store shadow data for blackboard
				if (!shadowData.has_value())
				{
					shadowData = FrameGraphData::ShadowData{
						.shadowMap = shadowTex,
						.lightSpaceMatrix = light.lightSpaceMatrix.value()
					};
				}

				_SetCameraUBO(m_frameDescriptor.camera.value());
				++lightIndex;
			}

			// Write to blackboard for scene pass
			if (shadowData.has_value())
			{
				resources.GetBlackboard().Put(std::move(shadowData.value()));
			}

			if (auto out = m_frameDescriptor.outputBuffer) out.value().Bind();
			SetViewport(0, 0, m_frameDescriptor.renderWidth, m_frameDescriptor.renderHeight);
		}
	);

	// ---- Pass 4: Reflection ----
	struct ReflectionPassData {};
	p_fg.AddPass<ReflectionPassData>(
		"Reflection",
		[](FrameGraphBuilder& builder, ReflectionPassData&) {
			builder.SetAsOutput({});
		},
		[this](const FrameGraphResources& resources, const ReflectionPassData& data)
		{
			ZoneScoped;
			TracyGpuZone("ReflectionPass");

			OVASSERT(HasDescriptor<FrameGraphData::ReflectionData>(),
				"Cannot find ReflectionDescriptor");

			auto& rd = GetDescriptor<FrameGraphData::ReflectionData>();

			// Prepare probe UBOs once per frame
			for (auto& probeRef : rd.reflectionProbes)
				probeRef.get()._PrepareUBO();

			constexpr uint32_t kFaceCount = 6;
			const OvMaths::FVector3 kFaceRotations[kFaceCount] = {
				{ 0.0f, -90.0f, 180.0f }, { 0.0f,  90.0f, 180.0f },
				{ 90.0f,  0.0f, 180.0f }, {-90.0f,  0.0f, 180.0f },
				{ 0.0f,   0.0f, 180.0f }, { 0.0f,-180.0f, 180.0f }
			};

			auto& drawables = GetDescriptor<SceneDrawablesDescriptor>();
			auto pso = CreatePipelineState();

			// Render each probe's faces using its internal cubemap
			for (auto probeRef : rd.reflectionProbes)
			{
				auto& probe = probeRef.get();
				const auto faceIndices = probe._GetCaptureFaceIndices();
				if (faceIndices.empty()) continue;

				OvRendering::Entities::Camera cam;
				cam.SetPosition(probe.owner.transform.GetWorldPosition() + probe.GetCapturePosition());
				cam.SetFov(90.0f);

				auto& fbo = probe._GetTargetFramebuffer();
				const auto [w, h] = fbo.GetSize();
				fbo.Bind();
				SetViewport(0, 0, w, h);

				// If this is the first capture (no cubemap is complete yet), clear all 6 faces first
				// to ensure the cubemap has defined content. This prevents flickering from undefined
				// texture content when using progressive capture.
				bool isFirstCapture = !probe._IsCubemapComplete();
				if (isFirstCapture)
				{
					for (uint32_t face = 0; face < 6; ++face)
					{
						fbo.SetTargetDrawBuffer(face);
						Clear(true, true, true);
					}
					// Reset to first face index for actual rendering
					fbo.SetTargetDrawBuffer(0);
				}

				for (auto faceIdx : faceIndices)
				{
					cam.SetRotation(OvMaths::FQuaternion{ kFaceRotations[faceIdx] });
					cam.CacheMatrices(w, h);
					_SetCameraUBO(cam);
					fbo.SetTargetDrawBuffer(faceIdx);
					// Skip clear if already cleared in initialization phase
					if (!isFirstCapture)
						Clear(true, true, true);

					const auto filtered = FilterDrawables(drawables, SceneDrawablesFilteringInput{
						.camera = cam,
						.requiredVisibilityFlags = EVisibilityFlags::REFLECTION,
						.includeUI = false,
					});

					auto captureDrawable = [&](const OvRendering::Entities::Drawable& drawable) {
						if (drawable.material && drawable.material->IsCapturedByReflectionProbes())
						{
							auto copy = drawable;
							copy.pass = "REFLECTION_PASS";
							DrawEntity(pso, copy);
						}
					};

					for (const auto& d : filtered.opaques | std::views::values) captureDrawable(d);
					for (const auto& d : filtered.transparents | std::views::values) captureDrawable(d);

					// Only notify complete when the last face (face 5) is rendered
					if (faceIdx == 5)
					{
						probe._NotifyCubemapComplete();
					}
				}

				fbo.Unbind();
			}

			// Store reflection data in blackboard for scene pass
			resources.GetBlackboard().Put(FrameGraphData::ReflectionData{ rd.reflectionProbes });

			_SetCameraUBO(m_frameDescriptor.camera.value());
			if (auto out = m_frameDescriptor.outputBuffer) out.value().Bind();
			SetViewport(0, 0, m_frameDescriptor.renderWidth, m_frameDescriptor.renderHeight);
		}
	);

	// ---- Pass 5: Scene (Opaque + Transparent + UI) ----
	struct ScenePassData {
		bool stencilWrite;
		FrameGraphBufferHandle engineUBO;  // Read dependency from EngineBuffer
		FrameGraphBufferHandle lightSSBO;  // Read dependency from Lighting
	};
	p_fg.AddPass<ScenePassData>(
		"Scene",
		[this, engineUBOHandle, lightSSBOHandle](FrameGraphBuilder& builder, ScenePassData& data) {
			// Declare read dependencies on buffers created by previous passes
			data.engineUBO = builder.Read(engineUBOHandle);
			data.lightSSBO = builder.Read(lightSSBOHandle);
			data.stencilWrite = m_stencilWrite;
			// Mark as output to prevent culling (scene renders to output framebuffer)
			builder.SetAsOutput({});
		},
		[this](const FrameGraphResources& resources, const ScenePassData& data)
		{
			ZoneScoped;
			TracyGpuZone("ScenePass");

			OVASSERT(HasDescriptor<SceneFilteredDrawablesDescriptor>(),
				"Cannot find SceneFilteredDrawablesDescriptor");
			const auto& drawables = GetDescriptor<SceneFilteredDrawablesDescriptor>();

			// Bind output framebuffer
			if (auto out = m_frameDescriptor.outputBuffer)
				out.value().Bind();
			else
				return; // No output buffer, skip rendering

			// Clear buffers
			Clear(true, true, data.stencilWrite);

			// Bind buffers via handles (for consistency, though we still have members)
			auto& engineUBO = resources.GetBuffer<HAL::UniformBuffer>(data.engineUBO);
			engineUBO.Bind(0);

			auto& lightSSBO = resources.GetBuffer<HAL::ShaderStorageBuffer>(data.lightSSBO);
			lightSSBO.Bind(0);

			auto pso = CreatePipelineState();

			if (data.stencilWrite)
			{
				pso.stencilTest = true;
				pso.stencilWriteMask = 0xFF;
				pso.stencilFuncRef = 1;
				pso.stencilFuncMask = 0xFF;
				pso.stencilOpFail = OvRendering::Settings::EOperation::REPLACE;
				pso.depthOpFail = OvRendering::Settings::EOperation::REPLACE;
				pso.bothOpFail = OvRendering::Settings::EOperation::REPLACE;
				pso.colorWriting.mask = 0x00;
			}

			auto drawWithBindings = [&](const OvRendering::Entities::Drawable& drawable) {
				if (drawable.material)
				{
					_BindShadowUniforms(drawable.material.value());
					_BindReflectionUniforms(drawable.material.value(), drawable);
				}
				DrawEntity(pso, drawable);
			};

			for (const auto& d : drawables.opaques | std::views::values)     drawWithBindings(d);
			for (const auto& d : drawables.transparents | std::views::values) drawWithBindings(d);
			for (const auto& d : drawables.ui | std::views::values)           DrawEntity(pso, d);
		}
	);

	// ---- Pass 6: PostProcess ----
	struct PostProcessPassData {};
	p_fg.AddPass<PostProcessPassData>(
		"PostProcess",
		[](FrameGraphBuilder& builder, PostProcessPassData&) { builder.SetAsOutput({}); },
		[this](const FrameGraphResources&, const PostProcessPassData&)
		{
			ZoneScoped;
			TracyGpuZone("PostProcessPass");
			auto pso = CreatePipelineState();
			m_postProcessPass->Draw(pso);
		}
	);
}

// ============================================================
// DrawModelWithSingleMaterial
// ============================================================

void OvCore::Rendering::SceneRenderer::DrawModelWithSingleMaterial(
	OvRendering::Data::PipelineState p_pso,
	OvRendering::Resources::Model& p_model,
	OvRendering::Data::Material& p_material,
	const OvMaths::FMatrix4& p_modelMatrix
)
{
	auto stateMask = p_material.GenerateStateMask();
	auto userMatrix = OvMaths::FMatrix4::Identity;
	auto engineDesc = EngineDrawableDescriptor{ p_modelMatrix, userMatrix };

	for (auto mesh : p_model.GetMeshes())
	{
		OvRendering::Entities::Drawable element;
		element.mesh = *mesh;
		element.material = p_material;
		element.stateMask = stateMask;
		element.AddDescriptor(engineDesc);
		DrawEntity(p_pso, element);
	}
}

// ============================================================
// ParseScene
// ============================================================

SceneRenderer::SceneDrawablesDescriptor OvCore::Rendering::SceneRenderer::ParseScene(
	const SceneParsingInput& p_input)
{
	ZoneScoped;
	using namespace OvCore::ECS::Components;

	SceneDrawablesDescriptor result;
	const auto& scene = p_input.scene;

	for (const auto modelRenderer : scene.GetFastAccessComponents().modelRenderers)
	{
		auto& owner = modelRenderer->owner;
		if (!owner.IsActive()) continue;
		const auto model = modelRenderer->GetModel();
		if (!model) continue;
		const auto materialRenderer = owner.GetComponent<CMaterialRenderer>();
		if (!materialRenderer) continue;

		const auto& transform = owner.transform.GetFTransform();
		const auto& materials = materialRenderer->GetMaterials();

		for (auto& mesh : model->GetMeshes())
		{
			OvTools::Utils::OptRef<OvRendering::Data::Material> material;
			if (mesh->GetMaterialIndex() < kMaxMaterialCount)
				material = materials.at(mesh->GetMaterialIndex());

			OvRendering::Entities::Drawable drawable{
				.mesh = *mesh,
				.material = material,
				.stateMask = material.has_value() ? material->GenerateStateMask() : OvRendering::Data::StateMask{},
			};

			auto bounds = [&]() -> std::optional<OvRendering::Geometry::BoundingSphere> {
				using enum CModelRenderer::EFrustumBehaviour;
				switch (modelRenderer->GetFrustumBehaviour())
				{
				case MESH_BOUNDS:             return mesh->GetBoundingSphere();
				case DEPRECATED_MODEL_BOUNDS: return model->GetBoundingSphere();
				case CUSTOM_BOUNDS:           return modelRenderer->GetCustomBoundingSphere();
				default:                      return std::nullopt;
				}
			}();

			drawable.AddDescriptor<SceneDrawableDescriptor>({
				.actor = owner,
				.visibilityFlags = materialRenderer->GetVisibilityFlags(),
				.bounds = bounds,
			});
			drawable.AddDescriptor<EngineDrawableDescriptor>({
				transform.GetWorldMatrix(),
				materialRenderer->GetUserMatrix()
			});

			result.drawables.push_back(drawable);
		}
	}

	// Particle systems
	for (auto particleSystem : scene.GetFastAccessComponents().particleSystems)
	{
		auto& owner = particleSystem->owner;
		if (!owner.IsActive()) continue;
		if (!particleSystem->material || !particleSystem->material->IsValid()) continue;
		if (particleSystem->GetParticleCount() == 0) continue;

		particleSystem->RebuildMesh(p_input.cameraRight, p_input.cameraUp);

		OvRendering::Entities::Drawable drawable{
			.mesh      = particleSystem->GetMesh(),
			.material  = *particleSystem->material,
			.stateMask = particleSystem->material->GenerateStateMask(),
		};
		drawable.AddDescriptor<SceneDrawableDescriptor>({
			.actor = owner,
			.visibilityFlags = EVisibilityFlags::GEOMETRY,
			.bounds = std::nullopt,
		});
		drawable.AddDescriptor<EngineDrawableDescriptor>({
			OvMaths::FMatrix4::Identity,
			OvMaths::FMatrix4::Identity
		});

		result.drawables.push_back(drawable);
	}

	return result;
}

// ============================================================
// FilterDrawables
// ============================================================

SceneRenderer::SceneFilteredDrawablesDescriptor OvCore::Rendering::SceneRenderer::FilterDrawables(
	const SceneDrawablesDescriptor& p_drawables,
	const SceneDrawablesFilteringInput& p_filteringInput
)
{
	ZoneScoped;
	using namespace OvCore::ECS::Components;

	SceneFilteredDrawablesDescriptor output;

	const auto& camera = p_filteringInput.camera;
	OvTools::Utils::OptRef<const OvRendering::Data::Frustum> frustum;
	if (camera.HasFrustumGeometryCulling())
	{
		frustum = p_filteringInput.frustumOverride ?
			p_filteringInput.frustumOverride : camera.GetFrustum();
	}

	for (const auto& drawable : p_drawables.drawables)
	{
		const auto& desc = drawable.GetDescriptor<SceneDrawableDescriptor>();

		if (!SatisfiesVisibility(desc.visibilityFlags, p_filteringInput.requiredVisibilityFlags))
			continue;

		const auto targetMaterial =
			p_filteringInput.overrideMaterial.has_value() ?
			p_filteringInput.overrideMaterial.value() :
			(drawable.material.has_value() ? drawable.material.value() : p_filteringInput.fallbackMaterial);

		if (!targetMaterial || !targetMaterial->IsValid()) continue;

		if (!p_filteringInput.fallbackMaterial ||
			&p_filteringInput.fallbackMaterial.value() != &targetMaterial.value())
		{
			const bool isUI = targetMaterial->IsUserInterface();
			if (isUI && !p_filteringInput.includeUI) continue;
			if (!isUI && !targetMaterial->IsBlendable() && !p_filteringInput.includeOpaque) continue;
			if (!isUI && targetMaterial->IsBlendable() && !p_filteringInput.includeTransparent) continue;
		}

		if (frustum && desc.bounds.has_value())
		{
			ZoneScopedN("Frustum Culling");
			if (!frustum->BoundingSphereInFrustum(desc.bounds.value(), desc.actor.transform.GetFTransform()))
				continue;
		}

		const float distanceToCamera = OvMaths::FVector3::Distance(
			desc.actor.transform.GetWorldPosition(),
			camera.GetPosition()
		);

		auto drawableCopy = drawable;
		drawableCopy.material = targetMaterial;
		drawableCopy.stateMask = targetMaterial->GenerateStateMask();

		if (drawableCopy.material->IsUserInterface())
		{
			output.ui.emplace(decltype(decltype(output.ui)::value_type::first){
				.order = drawableCopy.material->GetDrawOrder(),
				.distance = distanceToCamera
			}, drawableCopy);
		}
		else if (drawableCopy.material->IsBlendable())
		{
			output.transparents.emplace(decltype(decltype(output.transparents)::value_type::first){
				.order = drawableCopy.material->GetDrawOrder(),
				.distance = distanceToCamera
			}, drawableCopy);
		}
		else
		{
			output.opaques.emplace(decltype(decltype(output.opaques)::value_type::first){
				.order = drawableCopy.material->GetDrawOrder(),
				.distance = distanceToCamera
			}, drawableCopy);
		}
	}

	return output;
}
