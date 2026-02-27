/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <cmath>
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

	p_material.SetProperty("_ShadowMap", sd.shadowMap, true);
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

	// ---- Pass 1: EngineBuffer ----
	struct EngineBufferPassData {};
	p_fg.AddPass<EngineBufferPassData>(
		"EngineBuffer",
		[](FrameGraphBuilder& builder, EngineBufferPassData&) { builder.SetAsOutput({}); },
		[this](const FrameGraphResources& resources, const EngineBufferPassData&)
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
			m_engineBuffer->Upload(&d, OvRendering::HAL::BufferMemoryRange{
				.offset = sizeof(OvMaths::FMatrix4), .size = sizeof(d)
			});
			m_engineBuffer->Bind(0);
			resources.GetBlackboard().Put(FrameGraphData::EngineBufferData{ m_engineBuffer.get() });
		}
	);

	// ---- Pass 2: Lighting ----
	struct LightingPassData {};
	p_fg.AddPass<LightingPassData>(
		"Lighting",
		[](FrameGraphBuilder& builder, LightingPassData&) { builder.SetAsOutput({}); },
		[this](const FrameGraphResources& resources, const LightingPassData&)
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

			const auto view = std::span{ lightMatrices };
			if (m_lightBuffer->Allocate(view.size_bytes(), OvRendering::Settings::EAccessSpecifier::STREAM_DRAW))
				m_lightBuffer->Upload(view.data());
			m_lightBuffer->Bind(0);
			resources.GetBlackboard().Put(FrameGraphData::LightingData{ m_lightBuffer.get() });
		}
	);

	// ---- Pass 3: Shadow ----
	struct ShadowPassData {};
	p_fg.AddPass<ShadowPassData>(
		"Shadow",
		[](FrameGraphBuilder& builder, ShadowPassData&) { builder.SetAsOutput({}); },
		[this](const FrameGraphResources& resources, const ShadowPassData&)
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

			for (auto lightRef : ld.lights)
			{
				auto& light = lightRef.get();
				if (!light.castShadows || lightIndex >= kMaxShadowMaps) continue;
				if (light.type != OvRendering::Settings::ELightType::DIRECTIONAL) continue;

				light.PrepareForShadowRendering(m_frameDescriptor);
				_SetCameraUBO(light.shadowCamera.value());

				light.shadowBuffer->Bind();
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

				light.shadowBuffer->Unbind();

				const auto shadowTex = light.shadowBuffer->GetAttachment<OvRendering::HAL::Texture>(
					OvRendering::Settings::EFramebufferAttachment::DEPTH);
				if (shadowTex)
				{
					resources.GetBlackboard().Put(FrameGraphData::ShadowData{
						.shadowMap = &shadowTex.value(),
						.lightSpaceMatrix = light.lightSpaceMatrix.value()
					});
				}

				_SetCameraUBO(m_frameDescriptor.camera.value());
				++lightIndex;
			}

			if (auto out = m_frameDescriptor.outputBuffer) out.value().Bind();
			SetViewport(0, 0, m_frameDescriptor.renderWidth, m_frameDescriptor.renderHeight);
		}
	);

	// ---- Pass 4: Reflection ----
	struct ReflectionPassData {};
	p_fg.AddPass<ReflectionPassData>(
		"Reflection",
		[](FrameGraphBuilder& builder, ReflectionPassData&) { builder.SetAsOutput({}); },
		[this](const FrameGraphResources& resources, const ReflectionPassData&)
		{
			ZoneScoped;
			TracyGpuZone("ReflectionPass");

			OVASSERT(HasDescriptor<FrameGraphData::ReflectionData>(),
				"Cannot find ReflectionDescriptor");

			auto& rd = GetDescriptor<FrameGraphData::ReflectionData>();

			// Prepare probe UBOs once per frame
			for (auto& probeRef : rd.reflectionProbes)
				probeRef.get()._PrepareUBO();

			// Store in blackboard for scene pass
			resources.GetBlackboard().Put(FrameGraphData::ReflectionData{ rd.reflectionProbes });

			constexpr uint32_t kFaceCount = 6;
			const OvMaths::FVector3 kFaceRotations[kFaceCount] = {
				{ 0.0f, -90.0f, 180.0f }, { 0.0f,  90.0f, 180.0f },
				{ 90.0f,  0.0f, 180.0f }, {-90.0f,  0.0f, 180.0f },
				{ 0.0f,   0.0f, 180.0f }, { 0.0f,-180.0f, 180.0f }
			};

			auto& drawables = GetDescriptor<SceneDrawablesDescriptor>();
			auto pso = CreatePipelineState();

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

				for (auto faceIdx : faceIndices)
				{
					cam.SetRotation(OvMaths::FQuaternion{ kFaceRotations[faceIdx] });
					cam.CacheMatrices(w, h);
					_SetCameraUBO(cam);
					fbo.SetTargetDrawBuffer(faceIdx);
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

					if (faceIdx == 5) probe._NotifyCubemapComplete();
				}

				fbo.Unbind();
			}

			_SetCameraUBO(m_frameDescriptor.camera.value());
			if (auto out = m_frameDescriptor.outputBuffer) out.value().Bind();
			SetViewport(0, 0, m_frameDescriptor.renderWidth, m_frameDescriptor.renderHeight);
		}
	);

	// ---- Pass 5: Scene (Opaque + Transparent + UI) ----
	struct ScenePassData { bool stencilWrite; };
	p_fg.AddPass<ScenePassData>(
		"Scene",
		[this](FrameGraphBuilder& builder, ScenePassData& data) {
			builder.SetAsOutput({});
			data.stencilWrite = m_stencilWrite;
		},
		[this](const FrameGraphResources& resources, const ScenePassData& data)
		{
			ZoneScoped;
			TracyGpuZone("ScenePass");

			OVASSERT(HasDescriptor<SceneFilteredDrawablesDescriptor>(),
				"Cannot find SceneFilteredDrawablesDescriptor");
			const auto& drawables = GetDescriptor<SceneFilteredDrawablesDescriptor>();

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
