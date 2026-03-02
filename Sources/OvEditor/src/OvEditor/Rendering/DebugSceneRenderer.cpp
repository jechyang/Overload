/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <glad.h>

#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/ECS/Components/CDirectionalLight.h>
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvCore/ECS/Components/CPhysicalBox.h>
#include <OvCore/ECS/Components/CPhysicalCapsule.h>
#include <OvCore/ECS/Components/CPhysicalSphere.h>
#include <OvCore/ECS/Components/CPointLight.h>
#include <OvCore/ECS/Components/CSpotLight.h>
#include <OvCore/ECS/Components/CReflectionProbe.h>
#include <OvCore/Rendering/EngineDrawableDescriptor.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <OvCore/Rendering/SceneRenderer.h>

#include <OvDebug/Assertion.h>

#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Core/EditorResources.h>
#include <OvEditor/Panels/AView.h>
#include <OvEditor/Panels/GameView.h>
#include <OvEditor/Rendering/DebugModelRenderFeature.h>
#include <OvEditor/Rendering/DebugRenderState.h>
#include <OvEditor/Rendering/DebugSceneRenderer.h>
#include <OvEditor/Rendering/GizmoRenderFeature.h>
#include <OvEditor/Rendering/OutlineRenderFeature.h>
#include <OvEditor/Settings/EditorSettings.h>

#include <OvRendering/Features/DebugShapeRenderFeature.h>
#include <OvRendering/Features/FrameInfoRenderFeature.h>
#include <OvRendering/HAL/Profiling.h>

#include "OvDebug/Logger.h"

using namespace OvMaths;
using namespace OvRendering::Resources;
using namespace OvCore::Resources;

namespace
{
	const OvMaths::FVector3 kDebugBoundsColor = { 1.0f, 0.0f, 0.0f };
	const OvMaths::FVector3 kLightVolumeColor = { 1.0f, 1.0f, 0.0f };
	const OvMaths::FVector3 kColliderColor = { 0.0f, 1.0f, 0.0f };
	const OvMaths::FVector3 kFrustumColor = { 1.0f, 1.0f, 1.0f };

	constexpr float kHoveredOutlineWidth = 2.5f;
	constexpr float kSelectedOutlineWidth = 5.0f;

	OvMaths::FMatrix4 CalculateUnscaledModelMatrix(OvCore::ECS::Actor& p_actor)
	{
		auto translation = FMatrix4::Translation(p_actor.transform.GetWorldPosition());
		auto rotation = FQuaternion::ToMatrix4(p_actor.transform.GetWorldRotation());
		return translation * rotation;
	}

	std::optional<std::string> GetLightTypeTextureName(OvRendering::Settings::ELightType type)
	{
		using namespace OvRendering::Settings;

		switch (type)
		{
		case ELightType::POINT: return "Point_Light";
		case ELightType::SPOT: return "Spot_Light";
		case ELightType::DIRECTIONAL: return "Directional_Light";
		case ELightType::AMBIENT_BOX: return "Ambient_Box_Light";
		case ELightType::AMBIENT_SPHERE: return "Ambient_Sphere_Light";
		}

		return std::nullopt;
	}

	OvMaths::FMatrix4 CreateDebugDirectionalLight()
	{
		OvRendering::Entities::Light directionalLight{
			.intensity = 2.0f,
			.type = OvRendering::Settings::ELightType::DIRECTIONAL,
		};

		directionalLight.transform->SetLocalPosition({ 0.0f, 10.0f, 0.0f });
		directionalLight.transform->SetLocalRotation(OvMaths::FQuaternion({ 120.0f, -40.0f, 0.0f }));
		return directionalLight.GenerateMatrix();
	}

	OvMaths::FMatrix4 CreateDebugAmbientLight()
	{
		return OvRendering::Entities::Light{
			.intensity = 0.01f,
			.constant = 10000.0f, // radius
			.type = OvRendering::Settings::ELightType::AMBIENT_SPHERE
		}.GenerateMatrix();
	}

	std::unique_ptr<OvRendering::HAL::ShaderStorageBuffer> CreateDebugLightBuffer()
	{
		auto lightBuffer = std::make_unique<OvRendering::HAL::ShaderStorageBuffer>();

		const auto lightMatrices = std::to_array<OvMaths::FMatrix4>({
			CreateDebugDirectionalLight(),
			CreateDebugAmbientLight()
		});

		lightBuffer->Allocate(sizeof(lightMatrices), OvRendering::Settings::EAccessSpecifier::STATIC_READ);
		lightBuffer->Upload(lightMatrices.data());

		return lightBuffer;
	}

	// ============================================================
	// Helper lambdas for Debug Passes (inlined into BuildFrameGraph)
	// ============================================================

	void DrawDebugCameras(
		OvCore::Rendering::SceneRenderer& p_renderer,
		OvEditor::Rendering::DebugModelRenderFeature& p_debugModelFeature,
		OvCore::Resources::Material& p_cameraMaterial,
		OvRendering::HAL::ShaderStorageBuffer& p_fakeLightsBuffer
	)
	{
		ZoneScoped;
		TracyGpuZone("DebugCameras");

		// Override the light buffer with fake lights
		p_fakeLightsBuffer.Bind(0);

		// Set up camera matrices in engine UBO using RAII helper
		OvEditor::Rendering::DebugRenderStateSetup setup(p_renderer, p_renderer.GetFrameDescriptor().camera.value());

		auto& sceneDescriptor = p_renderer.GetDescriptor<OvCore::Rendering::SceneRenderer::SceneDescriptor>();

		for (auto camera : sceneDescriptor.scene.GetFastAccessComponents().cameras)
		{
			auto& actor = camera->owner;

			if (actor.IsActive())
			{
				auto& model = *EDITOR_CONTEXT(editorResources)->GetModel("Camera");
				auto modelMatrix = CalculateUnscaledModelMatrix(actor);

				p_debugModelFeature.DrawModelWithSingleMaterial(
					p_renderer.CreatePipelineState(),
					model,
					p_cameraMaterial,
					modelMatrix
				);
			}
		}

		// Restore the original light buffer
		p_renderer._BindLightBuffer();
	}

	void DrawDebugReflectionProbes(
		OvCore::Rendering::SceneRenderer& p_renderer,
		OvEditor::Rendering::DebugModelRenderFeature& p_debugModelFeature,
		OvCore::Resources::Material& p_reflectiveMaterial,
		OvRendering::HAL::ShaderStorageBuffer& p_fakeLightsBuffer
	)
	{
		ZoneScoped;
		TracyGpuZone("DebugReflectionProbes");

		// Override the light buffer with fake lights
		p_fakeLightsBuffer.Bind(0);

		// Set up camera matrices in engine UBO using RAII helper
		OvEditor::Rendering::DebugRenderStateSetup setup(p_renderer, p_renderer.GetFrameDescriptor().camera.value());

		auto& sceneDescriptor = p_renderer.GetDescriptor<OvCore::Rendering::SceneRenderer::SceneDescriptor>();

		for (auto reflectionProbe : sceneDescriptor.scene.GetFastAccessComponents().reflectionProbes)
		{
			auto& actor = reflectionProbe->owner;

			if (actor.IsActive())
			{
				// Skip if reflection probe doesn't have a valid cubemap yet
				auto cubemap = reflectionProbe->GetCubemap();
				if (!cubemap)
					continue;

				auto& model = *EDITOR_CONTEXT(editorResources)->GetModel("Sphere");
				auto modelMatrix =
					OvMaths::FMatrix4::Scale(
						OvMaths::FMatrix4::Translate(
							CalculateUnscaledModelMatrix(actor),
							reflectionProbe->GetCapturePosition()
						),
						OvMaths::FVector3::One * OvEditor::Settings::EditorSettings::ReflectionProbeScale
					);

				// Inline ReflectionRenderFeature::PrepareProbe / SendProbeData / BindProbe
				reflectionProbe->_PrepareUBO();
				p_reflectiveMaterial.SetProperty(
					"_EnvironmentMap",
					cubemap.get(),
					true
				);
				reflectionProbe->_GetUniformBuffer().Bind(1);

				p_debugModelFeature.DrawModelWithSingleMaterial(
					p_renderer.CreatePipelineState(),
					model,
					p_reflectiveMaterial,
					modelMatrix
				);
			}
		}

		// Restore the original light buffer
		p_renderer._BindLightBuffer();
	}

	void DrawDebugLights(
		OvCore::Rendering::SceneRenderer& p_renderer,
		OvEditor::Rendering::DebugModelRenderFeature& p_debugModelFeature,
		OvCore::Resources::Material& p_lightMaterial
	)
	{
		ZoneScoped;
		TracyGpuZone("DebugLights");

		// Set up camera matrices in engine UBO using RAII helper
		OvEditor::Rendering::DebugRenderStateSetup setup(p_renderer, p_renderer.GetFrameDescriptor().camera.value());

		auto& sceneDescriptor = p_renderer.GetDescriptor<OvCore::Rendering::SceneRenderer::SceneDescriptor>();
		p_lightMaterial.SetProperty("u_Scale", OvEditor::Settings::EditorSettings::LightBillboardScale * 0.1f);

		for (auto light : sceneDescriptor.scene.GetFastAccessComponents().lights)
		{
			auto& actor = light->owner;
			if (!actor.IsActive()) continue;

			auto& model = *EDITOR_CONTEXT(editorResources)->GetModel("Vertical_Plane");
			auto modelMatrix = OvMaths::FMatrix4::Translation(actor.transform.GetWorldPosition());
			auto lightTypeTextureName = GetLightTypeTextureName(light->GetData().type);
			auto lightTexture = lightTypeTextureName ?
				EDITOR_CONTEXT(editorResources)->GetTexture(lightTypeTextureName.value()) : nullptr;
			const auto& lightColor = light->GetColor();
			p_lightMaterial.SetProperty("u_DiffuseMap", lightTexture);
			p_lightMaterial.SetProperty("u_Diffuse", OvMaths::FVector4(lightColor.x, lightColor.y, lightColor.z, 0.75f));
			p_debugModelFeature.DrawModelWithSingleMaterial(
				p_renderer.CreatePipelineState(),
				model,
				p_lightMaterial,
				modelMatrix
			);
		}
	}

	void DrawFrustumLines(
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		const OvMaths::FVector3& pos,
		const OvMaths::FVector3& forward,
		float near, float far,
		const OvMaths::FVector3& a, const OvMaths::FVector3& b,
		const OvMaths::FVector3& c, const OvMaths::FVector3& d,
		const OvMaths::FVector3& e, const OvMaths::FVector3& f,
		const OvMaths::FVector3& g, const OvMaths::FVector3& h)
	{
		auto pso = p_debugShapeFeature.GetRenderer().CreatePipelineState();
		auto draw = [&](const FVector3& s, const FVector3& t, float dist) {
			auto off = pos + forward * dist;
			p_debugShapeFeature.DrawLine(pso, off + s, off + t, kFrustumColor, 1.0f, false);
		};
		draw(a, b, near); draw(b, d, near); draw(d, c, near); draw(c, a, near);
		draw(e, f, far);  draw(f, h, far);  draw(h, g, far);  draw(g, e, far);
		draw(a + forward * near, e + forward * far, 0);
		draw(b + forward * near, f + forward * far, 0);
		draw(c + forward * near, g + forward * far, 0);
		draw(d + forward * near, h + forward * far, 0);
	}

	void DrawCameraPerspectiveFrustum(
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		std::pair<uint16_t, uint16_t>& sz,
		OvCore::ECS::Components::CCamera& cam)
	{
		auto& camera = cam.GetCamera();
		const auto& pos = cam.owner.transform.GetWorldPosition();
		const auto& rot = cam.owner.transform.GetWorldRotation();
		const auto& fwd = cam.owner.transform.GetWorldForward();
		camera.CacheMatrices(sz.first, sz.second);
		const auto proj = FMatrix4::Transpose(camera.GetProjectionMatrix());
		const auto n = camera.GetNear(), fr = camera.GetFar();
		auto a = rot * FVector3{ n * (proj.data[2] - 1) / proj.data[0], n * (1 + proj.data[6]) / proj.data[5], 0 };
		auto b = rot * FVector3{ n * (1 + proj.data[2]) / proj.data[0], n * (1 + proj.data[6]) / proj.data[5], 0 };
		auto c = rot * FVector3{ n * (proj.data[2] - 1) / proj.data[0], n * (proj.data[6] - 1) / proj.data[5], 0 };
		auto d = rot * FVector3{ n * (1 + proj.data[2]) / proj.data[0], n * (proj.data[6] - 1) / proj.data[5], 0 };
		auto e = rot * FVector3{ fr * (proj.data[2] - 1) / proj.data[0], fr * (1 + proj.data[6]) / proj.data[5], 0 };
		auto f = rot * FVector3{ fr * (1 + proj.data[2]) / proj.data[0], fr * (1 + proj.data[6]) / proj.data[5], 0 };
		auto g = rot * FVector3{ fr * (proj.data[2] - 1) / proj.data[0], fr * (proj.data[6] - 1) / proj.data[5], 0 };
		auto h = rot * FVector3{ fr * (1 + proj.data[2]) / proj.data[0], fr * (proj.data[6] - 1) / proj.data[5], 0 };
		DrawFrustumLines(p_debugShapeFeature, pos, fwd, n, fr, a, b, c, d, e, f, g, h);
	}

	void DrawCameraOrthographicFrustum(
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		std::pair<uint16_t, uint16_t>& sz,
		OvCore::ECS::Components::CCamera& cam)
	{
		auto& camera = cam.GetCamera();
		const float ratio = sz.first / static_cast<float>(sz.second);
		const auto& pos = cam.owner.transform.GetWorldPosition();
		const auto& rot = cam.owner.transform.GetWorldRotation();
		const auto& fwd = cam.owner.transform.GetWorldForward();
		const auto n = camera.GetNear(), fr = camera.GetFar();
		const auto size = cam.GetSize(), right = ratio * size;
		auto a = rot * FVector3{ -right, size, 0 }; auto b = rot * FVector3{ right, size, 0 };
		auto c = rot * FVector3{ -right, -size, 0 }; auto d = rot * FVector3{ right, -size, 0 };
		DrawFrustumLines(p_debugShapeFeature, pos, fwd, n, fr, a, b, c, d, a, b, c, d);
	}

	void DrawCameraFrustum(
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		OvCore::ECS::Components::CCamera& cam)
	{
		auto& gameView = EDITOR_PANEL(OvEditor::Panels::GameView, "Game View");
		auto sz = gameView.GetSafeSize();
		if (sz.first == 0 || sz.second == 0) sz = { 16, 9 };
		switch (cam.GetProjectionMode())
		{
		case OvRendering::Settings::EProjectionMode::ORTHOGRAPHIC: DrawCameraOrthographicFrustum(p_debugShapeFeature, sz, cam); break;
		case OvRendering::Settings::EProjectionMode::PERSPECTIVE:  DrawCameraPerspectiveFrustum(p_debugShapeFeature, sz, cam);  break;
		}
	}

	void DrawReflectionProbeInfluenceVolume(
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		OvCore::ECS::Components::CReflectionProbe& probe)
	{
		auto pso = p_debugShapeFeature.GetRenderer().CreatePipelineState(); pso.depthTest = false;
		p_debugShapeFeature.DrawBox(pso, probe.owner.transform.GetWorldPosition(),
			probe.owner.transform.GetWorldRotation(), probe.GetInfluenceSize(), kDebugBoundsColor, 1.0f, false);
	}

	void DrawActorCollider(
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		OvCore::ECS::Actor& actor)
	{
		using namespace OvCore::ECS::Components;
		auto pso = p_debugShapeFeature.GetRenderer().CreatePipelineState(); pso.depthTest = false;
		if (auto box = actor.GetComponent<CPhysicalBox>())
			p_debugShapeFeature.DrawBox(pso, actor.transform.GetWorldPosition(), actor.transform.GetWorldRotation(),
				box->GetSize() * actor.transform.GetWorldScale(), OvMaths::FVector3{ 0,1,0 }, 1.0f, false);
		if (auto sph = actor.GetComponent<CPhysicalSphere>())
		{
			auto s = actor.transform.GetWorldScale();
			p_debugShapeFeature.DrawSphere(pso, actor.transform.GetWorldPosition(), actor.transform.GetWorldRotation(),
				sph->GetRadius() * std::max({ s.x,s.y,s.z,0.f }), OvMaths::FVector3{ 0,1,0 }, 1.0f, false);
		}
		if (auto cap = actor.GetComponent<CPhysicalCapsule>())
		{
			auto s = actor.transform.GetWorldScale();
			p_debugShapeFeature.DrawCapsule(pso, actor.transform.GetWorldPosition(), actor.transform.GetWorldRotation(),
				abs(cap->GetRadius() * std::max({ s.x,s.z,0.f })), abs(cap->GetHeight() * s.y),
				OvMaths::FVector3{ 0,1,0 }, 1.0f, false);
		}
	}

	void DrawLightBounds(
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		OvCore::ECS::Components::CLight& light)
	{
		auto pso = p_debugShapeFeature.GetRenderer().CreatePipelineState(); pso.depthTest = false;
		auto& d = light.GetData();
		p_debugShapeFeature.DrawSphere(pso, d.transform->GetWorldPosition(), d.transform->GetWorldRotation(),
			d.CalculateEffectRange(), kDebugBoundsColor, 1.0f, false);
	}

	void DrawAmbientBoxVolume(
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		OvCore::ECS::Components::CAmbientBoxLight& ab)
	{
		auto pso = p_debugShapeFeature.GetRenderer().CreatePipelineState(); pso.depthTest = false;
		auto& d = ab.GetData();
		p_debugShapeFeature.DrawBox(pso, ab.owner.transform.GetWorldPosition(), d.transform->GetWorldRotation(),
			{ d.constant, d.linear, d.quadratic }, d.CalculateEffectRange(), 1.0f, false);
	}

	void DrawAmbientSphereVolume(
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		OvCore::ECS::Components::CAmbientSphereLight& as)
	{
		auto pso = p_debugShapeFeature.GetRenderer().CreatePipelineState(); pso.depthTest = false;
		auto& d = as.GetData();
		p_debugShapeFeature.DrawSphere(pso, as.owner.transform.GetWorldPosition(),
			as.owner.transform.GetWorldRotation(), d.constant, kLightVolumeColor, 1.0f, false);
	}

	void DrawBoundingSpheres(
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		OvCore::ECS::Components::CModelRenderer& mr)
	{
		using enum OvCore::ECS::Components::CModelRenderer::EFrustumBehaviour;
		auto pso = p_debugShapeFeature.GetRenderer().CreatePipelineState();
		auto fb = mr.GetFrustumBehaviour();
		if (fb == DISABLED) return;
		if (auto model = mr.GetModel())
		{
			auto& actor = mr.owner;
			const auto& s = actor.transform.GetWorldScale();
			const auto& rot = actor.transform.GetWorldRotation();
			const auto& pos = actor.transform.GetWorldPosition();
			const float rs = std::max({ s.x, s.y, s.z, 0.f });
			auto drawBounds = [&](const OvRendering::Geometry::BoundingSphere& b) {
				auto off = OvMaths::FQuaternion::RotatePoint(b.position, rot) * rs;
				p_debugShapeFeature.DrawSphere(pso, pos + off, rot, b.radius * rs, kDebugBoundsColor, 1.0f, false);
			};
			if (fb == MESH_BOUNDS) for (auto mesh : model->GetMeshes()) drawBounds(mesh->GetBoundingSphere());
			else drawBounds(fb == CUSTOM_BOUNDS ? mr.GetCustomBoundingSphere() : model->GetBoundingSphere());
		}
	}

	void DrawActorDebugElements(
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		OvCore::ECS::Actor& p_actor)
	{
		if (!p_actor.IsActive()) return;
		if (OvEditor::Settings::EditorSettings::ShowGeometryBounds)
		{
			auto mr = p_actor.GetComponent<OvCore::ECS::Components::CModelRenderer>();
			if (mr && mr->GetModel()) DrawBoundingSpheres(p_debugShapeFeature, *mr);
		}
		if (auto cam = p_actor.GetComponent<OvCore::ECS::Components::CCamera>()) DrawCameraFrustum(p_debugShapeFeature, *cam);
		if (auto probe = p_actor.GetComponent<OvCore::ECS::Components::CReflectionProbe>())
			if (probe->GetInfluencePolicy() == OvCore::ECS::Components::CReflectionProbe::EInfluencePolicy::LOCAL)
				DrawReflectionProbeInfluenceVolume(p_debugShapeFeature, *probe);
		if (p_actor.GetComponent<OvCore::ECS::Components::CPhysicalObject>()) DrawActorCollider(p_debugShapeFeature, p_actor);
		if (auto ab = p_actor.GetComponent<OvCore::ECS::Components::CAmbientBoxLight>()) DrawAmbientBoxVolume(p_debugShapeFeature, *ab);
		if (auto as = p_actor.GetComponent<OvCore::ECS::Components::CAmbientSphereLight>()) DrawAmbientSphereVolume(p_debugShapeFeature, *as);
		if (OvEditor::Settings::EditorSettings::ShowLightBounds)
			if (auto light = p_actor.GetComponent<OvCore::ECS::Components::CLight>()) DrawLightBounds(p_debugShapeFeature, *light);
		for (auto& child : p_actor.GetChildren()) DrawActorDebugElements(p_debugShapeFeature, *child);
	}

	void DrawDebugActor(
		OvCore::Rendering::SceneRenderer& p_renderer,
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		OvEditor::Rendering::OutlineRenderFeature& p_outlineFeature,
		OvEditor::Rendering::GizmoRenderFeature& p_gizmoFeature,
		const OvEditor::Rendering::DebugSceneRenderer::DebugSceneDescriptor& p_desc)
	{
		ZoneScoped;
		TracyGpuZone("DebugActor");

		// Set up camera matrices in engine UBO using RAII helper
		OvEditor::Rendering::DebugRenderStateSetup setup(
			p_renderer,
			p_renderer.GetFrameDescriptor().camera.value()
		);

		p_renderer.Clear(false, false, true);

		if (p_desc.selectedActor)
		{
			auto& selected = p_desc.selectedActor.value();
			const bool isHovered = p_desc.highlightedActor && p_desc.highlightedActor->GetID() == selected.GetID();
			DrawActorDebugElements(p_debugShapeFeature, selected);
			p_outlineFeature.DrawOutline(selected,
				isHovered ? OvMaths::FVector4{ 1.0f, 1.0f, 0.0f, 1.0f } : OvMaths::FVector4{ 1.0f, 0.7f, 0.0f, 1.0f },
				kSelectedOutlineWidth);
			p_renderer.Clear(false, true, false, OvMaths::FVector3::Zero);
			p_gizmoFeature.DrawGizmo(
				selected.transform.GetWorldPosition(),
				selected.transform.GetWorldRotation(),
				p_desc.gizmoOperation, false,
				p_desc.highlightedGizmoDirection);
		}

		if (p_desc.highlightedActor)
		{
			auto& highlighted = p_desc.highlightedActor.value();
			if (!p_desc.selectedActor || highlighted.GetID() != p_desc.selectedActor->GetID())
				p_outlineFeature.DrawOutline(highlighted, OvMaths::FVector4{ 1.0f, 1.0f, 0.0f, 1.0f }, kHoveredOutlineWidth);
		}
	}
}

// ============================================================
// DebugSceneRenderer Implementation
// ============================================================

OvEditor::Rendering::DebugSceneRenderer::DebugSceneRenderer(OvRendering::Context::Driver& p_driver) :
	OvCore::Rendering::SceneRenderer(p_driver, true)
{
	using namespace OvRendering::Features;
	using namespace OvEditor::Rendering;
	using enum EFeatureExecutionPolicy;

	m_frameInfoFeature   = std::make_unique<FrameInfoRenderFeature>(*this, ALWAYS);
	m_debugShapeFeature  = std::make_unique<DebugShapeRenderFeature>(*this, FRAME_EVENTS_ONLY);
	m_debugModelFeature  = std::make_unique<DebugModelRenderFeature>(*this, NEVER);
	m_outlineFeature     = std::make_unique<OutlineRenderFeature>(*this, NEVER);
	m_gizmoFeature       = std::make_unique<GizmoRenderFeature>(*this, NEVER, *m_debugModelFeature);

	// Initialize picking framebuffer
	OvCore::Rendering::FramebufferUtil::SetupFramebuffer(
		m_pickingFramebuffer, 1, 1, true, false, false
	);

	/* Light Material */
	m_lightMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("Billboard"));
	m_lightMaterial.SetDepthTest(false);

	/* Gizmo Pickable Material */
	m_gizmoPickingMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("Gizmo"));
	m_gizmoPickingMaterial.SetGPUInstances(3);
	m_gizmoPickingMaterial.SetProperty("u_IsBall", false);
	m_gizmoPickingMaterial.SetProperty("u_IsPickable", true);
	m_gizmoPickingMaterial.SetDepthTest(true);

	m_reflectionProbeMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("PickingFallback"));
	m_reflectionProbeMaterial.SetDepthTest(false);

	/* Picking Material */
	m_pickingFallbackMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("PickingFallback"));
}

const OvRendering::Data::FrameInfo& OvEditor::Rendering::DebugSceneRenderer::GetFrameInfo() const
{
	return m_frameInfoFeature->GetFrameInfo();
}

void OvEditor::Rendering::DebugSceneRenderer::SetPickingEnabled(bool p_enabled)
{
	m_pickingEnabled = p_enabled;
}

OvEditor::Rendering::DebugSceneRenderer::PickingResult OvEditor::Rendering::DebugSceneRenderer::ReadbackPickingResult(
	const OvCore::SceneSystem::Scene& p_scene,
	uint32_t p_x,
	uint32_t p_y
)
{
	uint8_t pixel[3];

	m_pickingFramebuffer.ReadPixels(
		p_x, p_y, 1, 1,
		OvRendering::Settings::EPixelDataFormat::RGB,
		OvRendering::Settings::EPixelDataType::UNSIGNED_BYTE,
		pixel
	);

	uint32_t actorID = (0 << 24) | (pixel[2] << 16) | (pixel[1] << 8) | (pixel[0] << 0);
	auto actorUnderMouse = p_scene.FindActorByID(actorID);

	if (actorUnderMouse)
	{
		return OvTools::Utils::OptRef(*actorUnderMouse);
	}
	else if (
		pixel[0] == 255 &&
		pixel[1] == 255 &&
		pixel[2] >= 252 &&
		pixel[2] <= 254
		)
	{
		return static_cast<OvEditor::Core::GizmoBehaviour::EDirection>(pixel[2] - 252);
	}

	return std::nullopt;
}

void OvEditor::Rendering::DebugSceneRenderer::EndFrame()
{
	m_frameInfoFeature->OnEndFrame();
	OvCore::Rendering::SceneRenderer::EndFrame();
}

// ============================================================
// BuildFrameGraph
// ============================================================

void OvEditor::Rendering::DebugSceneRenderer::BuildFrameGraph(OvRendering::FrameGraph::FrameGraph& p_fg)
{
	using namespace OvRendering::FrameGraph;

	// Run base scene passes first
	SceneRenderer::BuildFrameGraph(p_fg);

	// Notify features of frame begin
	m_debugShapeFeature->OnBeginFrame(m_frameDescriptor);
	m_frameInfoFeature->OnBeginFrame(m_frameDescriptor);

	// PostProcess's Blit() calls outputBuffer.Unbind() at the end.
	// Re-bind the output framebuffer so all debug passes render to the correct target.
	struct RestoreOutputFBData {};
	p_fg.AddPass<RestoreOutputFBData>("RestoreOutputFramebuffer",
		[](FrameGraphBuilder& b, RestoreOutputFBData&) { b.SetAsOutput({}); },
		[this](const FrameGraphResources&, const RestoreOutputFBData&) {
			if (m_frameDescriptor.outputBuffer)
				m_frameDescriptor.outputBuffer.value().Bind();
		}
	);

	// ============================================================
	// Grid Pass - inlined from GridRenderPass
	// ============================================================
	struct GridPassData
	{
		OvCore::Resources::Material gridMaterial;
		std::unique_ptr<OvRendering::HAL::ShaderStorageBuffer> fakeLightsBuffer;
	};

	auto gridPassData = p_fg.AddPass<GridPassData>("Grid",
		[this](FrameGraphBuilder& builder, GridPassData& data) {
			// Initialize materials and buffers
			data.gridMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("Grid"));
			data.gridMaterial.SetBlendable(true);
			data.gridMaterial.SetBackfaceCulling(false);
			data.gridMaterial.SetDepthWriting(false);
			data.gridMaterial.SetDepthTest(true);
			builder.SetAsOutput({});
		},
		[this](const FrameGraphResources& resources, GridPassData& data) {
			ZoneScoped;
			TracyGpuZone("Grid");

			auto& gridDescriptor = GetDescriptor<DebugSceneRenderer::GridDescriptor>();
			auto& frameDescriptor = m_frameDescriptor;
			auto pso = CreatePipelineState();

			// Set up camera matrices
			DebugRenderStateSetup setup(
				static_cast<OvCore::Rendering::SceneRenderer&>(*this),
				frameDescriptor.camera.value()
			);

			constexpr float gridSize = 5000.0f;
			OvMaths::FMatrix4 model =
				OvMaths::FMatrix4::Translation({ gridDescriptor.viewPosition.x, 0.0f, gridDescriptor.viewPosition.z }) *
				OvMaths::FMatrix4::Scaling({ gridSize * 2.0f, 1.f, gridSize * 2.0f });

			data.gridMaterial.SetProperty("u_Color", gridDescriptor.gridColor);

			m_debugModelFeature->DrawModelWithSingleMaterial(
				pso,
				*EDITOR_CONTEXT(editorResources)->GetModel("Plane"),
				data.gridMaterial,
				model
			);

			constexpr float kLineWidth = 1.0f;
			m_debugShapeFeature->DrawLine(pso, OvMaths::FVector3(-gridSize + gridDescriptor.viewPosition.x, 0.0f, 0.0f), OvMaths::FVector3(gridSize + gridDescriptor.viewPosition.x, 0.0f, 0.0f), OvMaths::FVector3::Right, kLineWidth);
			m_debugShapeFeature->DrawLine(pso, OvMaths::FVector3(0.0f, -gridSize + gridDescriptor.viewPosition.y, 0.0f), OvMaths::FVector3(0.0f, gridSize + gridDescriptor.viewPosition.y, 0.0f), OvMaths::FVector3::Up, kLineWidth);
			m_debugShapeFeature->DrawLine(pso, OvMaths::FVector3(0.0f, 0.0f, -gridSize + gridDescriptor.viewPosition.z), OvMaths::FVector3(0.0f, 0.0f, gridSize + gridDescriptor.viewPosition.z), OvMaths::FVector3::Forward, kLineWidth);
		}
	);

	// ============================================================
	// Debug Cameras Pass
	// ============================================================
	struct DebugCamerasPassData
	{
		OvCore::Resources::Material cameraMaterial;
		std::unique_ptr<OvRendering::HAL::ShaderStorageBuffer> fakeLightsBuffer;
	};

	p_fg.AddPass<DebugCamerasPassData>("DebugCameras",
		[](FrameGraphBuilder& b, DebugCamerasPassData& data) {
			// Initialize material
			data.cameraMaterial.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\Standard.ovfx"]);
			data.cameraMaterial.SetProperty("u_Albedo", FVector4{ 0.0f, 0.447f, 1.0f, 1.0f });
			data.cameraMaterial.SetProperty("u_Metallic", 0.0f);
			data.cameraMaterial.SetProperty("u_Roughness", 0.25f);
			data.cameraMaterial.SetProperty("u_BuiltInGammaCorrection", true);
			data.cameraMaterial.SetProperty("u_BuiltInToneMapping", true);
			b.SetAsOutput({});
		},
		[this](const FrameGraphResources&, DebugCamerasPassData& data) {
			// Initialize fake lights buffer on first use
			static std::once_flag initFlag;
			static std::unique_ptr<OvRendering::HAL::ShaderStorageBuffer> fakeLightsBuffer;
			std::call_once(initFlag, [&]() {
				fakeLightsBuffer = CreateDebugLightBuffer();
			});

			DrawDebugCameras(
				static_cast<OvCore::Rendering::SceneRenderer&>(*this),
				*m_debugModelFeature,
				data.cameraMaterial,
				*fakeLightsBuffer
			);
		}
	);

	// ============================================================
	// Debug Reflection Probes Pass
	// ============================================================
	struct DebugReflectionPassData
	{
		OvCore::Resources::Material reflectiveMaterial;
	};

	p_fg.AddPass<DebugReflectionPassData>("DebugReflectionProbes",
		[](FrameGraphBuilder& b, DebugReflectionPassData& data) {
			// Initialize material
			data.reflectiveMaterial.SetDepthTest(false);
			data.reflectiveMaterial.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\Standard.ovfx"]);
			data.reflectiveMaterial.SetProperty("u_Albedo", FVector4{ 1.0, 1.0f, 1.0f, 1.0f });
			data.reflectiveMaterial.SetProperty("u_Metallic", 1.0f);
			data.reflectiveMaterial.SetProperty("u_Roughness", 0.0f);
			data.reflectiveMaterial.SetProperty("u_BuiltInGammaCorrection", true);
			data.reflectiveMaterial.SetProperty("u_BuiltInToneMapping", true);
			b.SetAsOutput({});
		},
		[this](const FrameGraphResources&, DebugReflectionPassData& data) {
			// Initialize fake lights buffer on first use
			static std::once_flag initFlag;
			static std::unique_ptr<OvRendering::HAL::ShaderStorageBuffer> fakeLightsBuffer;
			std::call_once(initFlag, [&]() {
				fakeLightsBuffer = CreateDebugLightBuffer();
			});

			DrawDebugReflectionProbes(
				static_cast<OvCore::Rendering::SceneRenderer&>(*this),
				*m_debugModelFeature,
				data.reflectiveMaterial,
				*fakeLightsBuffer
			);
		}
	);

	// ============================================================
	// Debug Lights Pass
	// ============================================================
	struct DebugLightsPassData
	{
		OvCore::Resources::Material lightMaterial;
	};

	p_fg.AddPass<DebugLightsPassData>("DebugLights",
		[](FrameGraphBuilder& b, DebugLightsPassData& data) {
			// Initialize material
			data.lightMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("Billboard"));
			data.lightMaterial.SetProperty("u_Diffuse", FVector4{ 1.f, 1.f, 0.5f, 0.5f });
			data.lightMaterial.SetBackfaceCulling(false);
			data.lightMaterial.SetBlendable(true);
			data.lightMaterial.SetDepthTest(false);
			b.SetAsOutput({});
		},
		[this](const FrameGraphResources&, DebugLightsPassData& data) {
			DrawDebugLights(
				static_cast<OvCore::Rendering::SceneRenderer&>(*this),
				*m_debugModelFeature,
				data.lightMaterial
			);
		}
	);

	// ============================================================
	// Debug Actor Pass
	// ============================================================
	struct DebugActorPassData {};
	p_fg.AddPass<DebugActorPassData>("DebugActor",
		[](FrameGraphBuilder& b, DebugActorPassData&) {
			b.SetAsOutput({});
		},
		[this](const FrameGraphResources&, const DebugActorPassData&) {
			auto& desc = GetDescriptor<DebugSceneDescriptor>();
			DrawDebugActor(
				static_cast<OvCore::Rendering::SceneRenderer&>(*this),
				*m_debugShapeFeature,
				*m_outlineFeature,
				*m_gizmoFeature,
				desc
			);
		}
	);

	// ============================================================
	// Picking Pass - inlined from PickingRenderPass
	// ============================================================
	struct PickingPassData
	{
		OvCore::Resources::Material pickingFallbackMaterial;
		OvCore::Resources::Material reflectionProbeMaterial;
		OvCore::Resources::Material lightMaterial;
		OvCore::Resources::Material gizmoPickingMaterial;
	};

	p_fg.AddPass<PickingPassData>("Picking",
		[this](FrameGraphBuilder& builder, PickingPassData& data) {
			// Initialize materials
			data.pickingFallbackMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("PickingFallback"));
			data.reflectionProbeMaterial.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\Standard.ovfx"]);
			data.reflectionProbeMaterial.SetDepthTest(false);
			data.lightMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("Billboard"));
			data.lightMaterial.SetDepthTest(false);
			data.gizmoPickingMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("Gizmo"));
			data.gizmoPickingMaterial.SetGPUInstances(3);
			data.gizmoPickingMaterial.SetProperty("u_IsBall", false);
			data.gizmoPickingMaterial.SetProperty("u_IsPickable", true);
			data.gizmoPickingMaterial.SetDepthTest(true);
			builder.SetAsOutput({});
		},
		[this](const FrameGraphResources&, PickingPassData& data) {
			// Skip picking pass if disabled
			if (!m_pickingEnabled)
				return;

			ZoneScoped;
			TracyGpuZone("PickingPass");

			auto& sceneDescriptor = GetDescriptor<SceneRenderer::SceneDescriptor>();
			auto& debugSceneDescriptor = GetDescriptor<DebugSceneDescriptor>();
			auto& frameDescriptor = m_frameDescriptor;
			auto& scene = sceneDescriptor.scene;

			// Debug: Check for OpenGL errors at pass start
			#if _DEBUG
			if (GLenum err = glGetError(); err != GL_NO_ERROR)
			{
				OVLOG_ERROR("[Picking Pass] Start: OpenGL error " + std::to_string(err));
			}
			#endif

			// Bind light SSBO for shaders that require it (e.g., Standard.ovfx used by reflection probes)
			_BindLightBuffer();

			#if _DEBUG
			if (GLenum err = glGetError(); err != GL_NO_ERROR)
			{
				OVLOG_ERROR("[Picking Pass] After _BindLightBuffer: OpenGL error " + std::to_string(err));
			}
			#endif

			// Resize and bind picking framebuffer
			m_pickingFramebuffer.Resize(frameDescriptor.renderWidth, frameDescriptor.renderHeight);
			m_pickingFramebuffer.Bind();

			#if _DEBUG
			if (GLenum err = glGetError(); err != GL_NO_ERROR)
			{
				OVLOG_ERROR("[Picking Pass] After framebuffer bind: OpenGL error " + std::to_string(err));
			}
			#endif

			auto pso = CreatePipelineState();
			Clear(true, true, true);

			#if _DEBUG
			if (GLenum err = glGetError(); err != GL_NO_ERROR)
			{
				OVLOG_ERROR("[Picking Pass] After Clear: OpenGL error " + std::to_string(err));
			}
			#endif

			// Set up camera matrices
			DebugRenderStateSetup setup(
				static_cast<SceneRenderer&>(*this),
				frameDescriptor.camera.value()
			);

			#if _DEBUG
			if (GLenum err = glGetError(); err != GL_NO_ERROR)
			{
				OVLOG_ERROR("[Picking Pass] After setup: OpenGL error " + std::to_string(err));
			}
			#endif

			// Helper lambda for preparing picking material
			auto preparePickingMaterial = [&](const OvCore::ECS::Actor& actor, OvCore::Resources::Material& material) {
				uint32_t actorID = static_cast<uint32_t>(actor.GetID());
				auto bytes = reinterpret_cast<uint8_t*>(&actorID);
				auto color = OvMaths::FVector4{ bytes[0] / 255.0f, bytes[1] / 255.0f, bytes[2] / 255.0f, 1.0f };
				if (material.HasProperty("_PickingColor"))
					material.SetProperty("_PickingColor", color, true);
			};

			// Helper lambda for drawing pickable drawable
			auto drawPickingDrawable = [&](const OvRendering::Entities::Drawable& drawable, OvCore::Resources::Material* targetMaterialPtr) {
				OvCore::Resources::Material& targetMaterial = *targetMaterialPtr;

				const SceneRenderer::SceneDrawableDescriptor& sceneDrawableDesc = drawable.GetDescriptor<SceneRenderer::SceneDrawableDescriptor>();
				const auto& actor = sceneDrawableDesc.actor;
				preparePickingMaterial(actor, targetMaterial);

				OvRendering::Data::StateMask stateMask;
				if (drawable.material && drawable.material->IsValid())
					stateMask = drawable.material->GenerateStateMask();
				else
					stateMask = targetMaterial.GenerateStateMask();

				OvRendering::Entities::Drawable finalDrawable;
				finalDrawable.mesh = drawable.mesh;
				finalDrawable.material = static_cast<OvRendering::Data::Material&>(targetMaterial);
				finalDrawable.stateMask = stateMask;
				finalDrawable.stateMask.frontfaceCulling = false;
				finalDrawable.stateMask.backfaceCulling = false;
				finalDrawable.primitiveMode = drawable.primitiveMode;
				finalDrawable.pass = "PICKING_PASS";
				finalDrawable.featureSetOverride = drawable.featureSetOverride;
				// Copy descriptor by value (create a new instance with same data)
				finalDrawable.AddDescriptor<SceneRenderer::SceneDrawableDescriptor>(SceneRenderer::SceneDrawableDescriptor{
					.actor = sceneDrawableDesc.actor,
					.visibilityFlags = sceneDrawableDesc.visibilityFlags,
					.bounds = sceneDrawableDesc.bounds
				});

				DrawEntity(pso, finalDrawable);
			};

			// Draw pickable models
			const auto& filteredDrawables = GetDescriptor<SceneRenderer::SceneFilteredDrawablesDescriptor>();
			const std::string pickingPassName = "PICKING_PASS";

			for (auto& [key, drawable] : filteredDrawables.opaques)
			{
				OvCore::Resources::Material* targetMaterialPtr = nullptr;
				if (drawable.material && drawable.material->IsValid())
				{
					// Cast base material reference to derived type using static_cast
					auto* derivedMat = static_cast<OvCore::Resources::Material*>(&drawable.material.value());
					if (derivedMat->HasPass(pickingPassName))
						targetMaterialPtr = derivedMat;
				}
				if (!targetMaterialPtr)
					targetMaterialPtr = &data.pickingFallbackMaterial;
				drawPickingDrawable(drawable, targetMaterialPtr);
			}

			#if _DEBUG
			if (GLenum err = glGetError(); err != GL_NO_ERROR)
			{
				OVLOG_ERROR("[Picking Pass] After drawing opaques: OpenGL error " + std::to_string(err));
			}
			#endif

			for (auto& [key, drawable] : filteredDrawables.transparents)
			{
				OvCore::Resources::Material* targetMaterialPtr = nullptr;
				if (drawable.material && drawable.material->IsValid())
				{
					// Cast base material reference to derived type using static_cast
					auto* derivedMat = static_cast<OvCore::Resources::Material*>(&drawable.material.value());
					if (derivedMat->HasPass(pickingPassName))
						targetMaterialPtr = derivedMat;
				}
				if (!targetMaterialPtr)
					targetMaterialPtr = &data.pickingFallbackMaterial;
				drawPickingDrawable(drawable, targetMaterialPtr);
			}

			#if _DEBUG
			if (GLenum err = glGetError(); err != GL_NO_ERROR)
			{
				OVLOG_ERROR("[Picking Pass] After drawing transparents: OpenGL error " + std::to_string(err));
			}
			#endif
			for (auto& [key, drawable] : filteredDrawables.ui)
			{
				OvCore::Resources::Material* targetMaterialPtr = nullptr;
				if (drawable.material && drawable.material->IsValid())
				{
					// Cast base material reference to derived type using static_cast
					auto* derivedMat = static_cast<OvCore::Resources::Material*>(&drawable.material.value());
					if (derivedMat->HasPass(pickingPassName))
						targetMaterialPtr = derivedMat;
				}
				if (!targetMaterialPtr)
					targetMaterialPtr = &data.pickingFallbackMaterial;
				drawPickingDrawable(drawable, targetMaterialPtr);
			}

			// Draw pickable cameras (TEMPORARILY DISABLED FOR DEBUG)
			/*
			for (auto camera : scene.GetFastAccessComponents().cameras)
			{
				auto& actor = camera->owner;
				if (actor.IsActive())
				{
					preparePickingMaterial(actor, data.pickingFallbackMaterial);
					auto& cameraModel = *EDITOR_CONTEXT(editorResources)->GetModel("Camera");
					auto modelMatrix = OvMaths::FMatrix4::Translation(actor.transform.GetWorldPosition()) *
						OvMaths::FQuaternion::ToMatrix4(actor.transform.GetWorldRotation());
					m_debugModelFeature->DrawModelWithSingleMaterial(pso, cameraModel,
						data.pickingFallbackMaterial, modelMatrix);
				}
			}
			*/

			#if _DEBUG
			if (GLenum err = glGetError(); err != GL_NO_ERROR)
			{
				OVLOG_ERROR("[Picking Pass] After drawing cameras: OpenGL error " + std::to_string(err));
			}
			#endif

			// Draw pickable reflection probes (TEMPORARILY DISABLED FOR DEBUG)
			/*
			for (auto reflectionProbe : scene.GetFastAccessComponents().reflectionProbes)
			{
				auto& actor = reflectionProbe->owner;
				if (actor.IsActive())
				{
					preparePickingMaterial(actor, data.reflectionProbeMaterial);

					// Skip if reflection probe doesn't have a valid cubemap yet
					auto cubemap = reflectionProbe->GetCubemap();
					if (!cubemap)
						continue;

					auto& model = *EDITOR_CONTEXT(editorResources)->GetModel("Sphere");
					auto modelMatrix = OvMaths::FMatrix4::Scale(
						OvMaths::FMatrix4::Translate(CalculateUnscaledModelMatrix(actor), reflectionProbe->GetCapturePosition()),
						OvMaths::FVector3::One * OvEditor::Settings::EditorSettings::ReflectionProbeScale
					);
					reflectionProbe->_PrepareUBO();
					data.reflectionProbeMaterial.SetProperty("_EnvironmentMap", cubemap.get(), true);

					// Bind reflection UBO before drawing
					reflectionProbe->_GetUniformBuffer().Bind(1);

					m_debugModelFeature->DrawModelWithSingleMaterial(pso, model,
						data.reflectionProbeMaterial, modelMatrix);

					// Debug: Check for OpenGL errors after drawing reflection probe
					#if _DEBUG
					if (GLenum err = glGetError(); err != GL_NO_ERROR)
					{
						OVLOG_ERROR("[Picking Pass] After drawing reflection probe: OpenGL error " + std::to_string(err));
					}
					#endif
				}
			}
			*/

			// Draw pickable lights
			if (OvEditor::Settings::EditorSettings::LightBillboardScale > 0.001f)
			{
				Clear(false, true, false);
				data.lightMaterial.SetProperty("u_Scale",
					OvEditor::Settings::EditorSettings::LightBillboardScale * 0.1f);

				for (auto light : scene.GetFastAccessComponents().lights)
				{
					auto& actor = light->owner;
					if (actor.IsActive())
					{
						preparePickingMaterial(actor, data.lightMaterial);
						auto& model = *EDITOR_CONTEXT(editorResources)->GetModel("Vertical_Plane");
						auto modelMatrix = OvMaths::FMatrix4::Translation(actor.transform.GetWorldPosition());
						auto lightTypeTextureName = GetLightTypeTextureName(light->GetData().type);
						if (lightTypeTextureName)
						{
							data.lightMaterial.SetProperty("u_DiffuseMap",
								EDITOR_CONTEXT(editorResources)->GetTexture(lightTypeTextureName.value()), true);
						}
						data.lightMaterial.SetProperty("u_Diffuse",
							OvMaths::FVector4(light->GetColor().x, light->GetColor().y, light->GetColor().z, 0.75f));
						m_debugModelFeature->DrawModelWithSingleMaterial(pso, model,
							data.lightMaterial, modelMatrix);
					}
				}
			}

			// Clear depth and draw gizmo
			Clear(false, true, false);
			if (debugSceneDescriptor.selectedActor)
			{
				auto& selected = debugSceneDescriptor.selectedActor.value();
				auto modelMatrix = OvMaths::FMatrix4::Translation(selected.transform.GetWorldPosition()) *
					OvMaths::FQuaternion::ToMatrix4(OvMaths::FQuaternion::Normalize(selected.transform.GetWorldRotation()));
				auto arrowModel = EDITOR_CONTEXT(editorResources)->GetModel("Arrow_Picking");
				m_debugModelFeature->DrawModelWithSingleMaterial(pso, *arrowModel,
					data.gizmoPickingMaterial, modelMatrix);
			}

			// Unbind and restore output framebuffer
			m_pickingFramebuffer.Unbind();
			if (auto output = frameDescriptor.outputBuffer)
				output.value().Bind();
		}
	);
}
