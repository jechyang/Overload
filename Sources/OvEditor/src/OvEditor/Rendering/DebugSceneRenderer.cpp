/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

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
#include <OvCore/Rendering/SceneRenderer.h>

#include <OvDebug/Assertion.h>

#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Core/EditorResources.h>
#include <OvEditor/Panels/AView.h>
#include <OvEditor/Panels/GameView.h>
#include <OvEditor/Rendering/DebugModelRenderFeature.h>
#include <OvEditor/Rendering/DebugSceneRenderer.h>
#include <OvEditor/Rendering/GizmoRenderFeature.h>
#include <OvEditor/Rendering/GridRenderPass.h>
#include <OvEditor/Rendering/OutlineRenderFeature.h>
#include <OvEditor/Rendering/PickingRenderPass.h>
#include <OvEditor/Settings/EditorSettings.h>

#include <OvRendering/Features/DebugShapeRenderFeature.h>
#include <OvRendering/Features/FrameInfoRenderFeature.h>
#include <OvRendering/HAL/Profiling.h>

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
}

// ============================================================
// DebugCamerasRenderPass
// ============================================================

class DebugCamerasRenderPass : public OvRendering::Core::ARenderPass
{
public:
	DebugCamerasRenderPass(
		OvRendering::Core::CompositeRenderer& p_renderer,
		OvEditor::Rendering::DebugModelRenderFeature& p_debugModelFeature
	) : OvRendering::Core::ARenderPass(p_renderer),
		m_debugModelFeature(p_debugModelFeature)
	{
		m_fakeLightsBuffer = CreateDebugLightBuffer();

		m_cameraMaterial.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\Standard.ovfx"]);
		m_cameraMaterial.SetProperty("u_Albedo", FVector4{ 0.0f, 0.447f, 1.0f, 1.0f });
		m_cameraMaterial.SetProperty("u_Metallic", 0.0f);
		m_cameraMaterial.SetProperty("u_Roughness", 0.25f);
		m_cameraMaterial.SetProperty("u_BuiltInGammaCorrection", true);
		m_cameraMaterial.SetProperty("u_BuiltInToneMapping", true);
	}

	virtual void Draw(OvRendering::Data::PipelineState p_pso) override
	{
		ZoneScoped;
		TracyGpuZone("DebugCamerasRenderPass");

		// Override the light buffer with fake lights
		m_fakeLightsBuffer->Bind(0);

		// Set up camera matrices in engine UBO
		static_cast<OvCore::Rendering::SceneRenderer&>(m_renderer)._SetCameraUBO(m_renderer.GetFrameDescriptor().camera.value());

		auto& sceneDescriptor = m_renderer.GetDescriptor<OvCore::Rendering::SceneRenderer::SceneDescriptor>();

		for (auto camera : sceneDescriptor.scene.GetFastAccessComponents().cameras)
		{
			auto& actor = camera->owner;

			if (actor.IsActive())
			{
				auto& model = *EDITOR_CONTEXT(editorResources)->GetModel("Camera");
				auto modelMatrix = CalculateUnscaledModelMatrix(actor);

				m_debugModelFeature.DrawModelWithSingleMaterial(p_pso, model, m_cameraMaterial, modelMatrix);
			}
		}

		// Restore the original light buffer
		static_cast<OvCore::Rendering::SceneRenderer&>(m_renderer)._BindLightBuffer();
	}

private:
	OvEditor::Rendering::DebugModelRenderFeature& m_debugModelFeature;
	OvCore::Resources::Material m_cameraMaterial;
	std::unique_ptr<OvRendering::HAL::ShaderStorageBuffer> m_fakeLightsBuffer;
};

// ============================================================
// DebugReflectionProbesRenderPass
// ============================================================

class DebugReflectionProbesRenderPass : public OvRendering::Core::ARenderPass
{
public:
	DebugReflectionProbesRenderPass(
		OvRendering::Core::CompositeRenderer& p_renderer,
		OvEditor::Rendering::DebugModelRenderFeature& p_debugModelFeature
	) : OvRendering::Core::ARenderPass(p_renderer),
		m_debugModelFeature(p_debugModelFeature)
	{
		m_fakeLightsBuffer = CreateDebugLightBuffer();

		m_reflectiveMaterial.SetDepthTest(false);
		m_reflectiveMaterial.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\Standard.ovfx"]);
		m_reflectiveMaterial.SetProperty("u_Albedo", FVector4{ 1.0, 1.0f, 1.0f, 1.0f });
		m_reflectiveMaterial.SetProperty("u_Metallic", 1.0f);
		m_reflectiveMaterial.SetProperty("u_Roughness", 0.0f);
		m_reflectiveMaterial.SetProperty("u_BuiltInGammaCorrection", true);
		m_reflectiveMaterial.SetProperty("u_BuiltInToneMapping", true);
	}

	virtual void Draw(OvRendering::Data::PipelineState p_pso) override
	{
		ZoneScoped;
		TracyGpuZone("DebugReflectionProbesRenderPass");

		// Override the light buffer with fake lights
		m_fakeLightsBuffer->Bind(0);

		// Set up camera matrices in engine UBO
		static_cast<OvCore::Rendering::SceneRenderer&>(m_renderer)._SetCameraUBO(m_renderer.GetFrameDescriptor().camera.value());

		auto& sceneDescriptor = m_renderer.GetDescriptor<OvCore::Rendering::SceneRenderer::SceneDescriptor>();

		for (auto reflectionProbe : sceneDescriptor.scene.GetFastAccessComponents().reflectionProbes)
		{
			auto& actor = reflectionProbe->owner;

			if (actor.IsActive())
			{
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
				m_reflectiveMaterial.SetProperty(
					"_EnvironmentMap",
					reflectionProbe->GetCubemap().get(),
					true
				);
				reflectionProbe->_GetUniformBuffer().Bind(1);

				m_debugModelFeature.DrawModelWithSingleMaterial(p_pso, model, m_reflectiveMaterial, modelMatrix);
			}
		}

		// Restore the original light buffer
		static_cast<OvCore::Rendering::SceneRenderer&>(m_renderer)._BindLightBuffer();
	}

private:
	OvEditor::Rendering::DebugModelRenderFeature& m_debugModelFeature;
	OvCore::Resources::Material m_reflectiveMaterial;
	std::unique_ptr<OvRendering::HAL::ShaderStorageBuffer> m_fakeLightsBuffer;
};

// ============================================================
// DebugLightsRenderPass
// ============================================================

class DebugLightsRenderPass : public OvRendering::Core::ARenderPass
{
public:
	DebugLightsRenderPass(
		OvRendering::Core::CompositeRenderer& p_renderer,
		OvEditor::Rendering::DebugModelRenderFeature& p_debugModelFeature
	) : OvRendering::Core::ARenderPass(p_renderer),
		m_debugModelFeature(p_debugModelFeature)
	{
		m_lightMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("Billboard"));
		m_lightMaterial.SetProperty("u_Diffuse", FVector4{ 1.f, 1.f, 0.5f, 0.5f });
		m_lightMaterial.SetBackfaceCulling(false);
		m_lightMaterial.SetBlendable(true);
		m_lightMaterial.SetDepthTest(false);
	}

	virtual void Draw(OvRendering::Data::PipelineState p_pso) override
	{
		ZoneScoped;
		TracyGpuZone("DebugLightsRenderPass");

		// Set up camera matrices in engine UBO
		static_cast<OvCore::Rendering::SceneRenderer&>(m_renderer)._SetCameraUBO(m_renderer.GetFrameDescriptor().camera.value());

		auto& sceneDescriptor = m_renderer.GetDescriptor<OvCore::Rendering::SceneRenderer::SceneDescriptor>();
		m_lightMaterial.SetProperty("u_Scale", OvEditor::Settings::EditorSettings::LightBillboardScale * 0.1f);

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
			m_lightMaterial.SetProperty("u_DiffuseMap", lightTexture);
			m_lightMaterial.SetProperty("u_Diffuse", OvMaths::FVector4(lightColor.x, lightColor.y, lightColor.z, 0.75f));
			m_debugModelFeature.DrawModelWithSingleMaterial(p_pso, model, m_lightMaterial, modelMatrix);
		}
	}

private:
	OvEditor::Rendering::DebugModelRenderFeature& m_debugModelFeature;
	OvCore::Resources::Material m_lightMaterial;
};

// ============================================================
// DebugActorRenderPass
// ============================================================

class DebugActorRenderPass : public OvRendering::Core::ARenderPass
{
public:
	DebugActorRenderPass(
		OvRendering::Core::CompositeRenderer& p_renderer,
		OvRendering::Features::DebugShapeRenderFeature& p_debugShapeFeature,
		OvEditor::Rendering::OutlineRenderFeature& p_outlineFeature,
		OvEditor::Rendering::GizmoRenderFeature& p_gizmoFeature
	) : OvRendering::Core::ARenderPass(p_renderer),
		m_debugShapeFeature(p_debugShapeFeature),
		m_outlineFeature(p_outlineFeature),
		m_gizmoFeature(p_gizmoFeature)
	{}

	virtual void Draw(OvRendering::Data::PipelineState p_pso) override
	{
		ZoneScoped;
		TracyGpuZone("DebugActorRenderPass");

		// Set up camera matrices in engine UBO
		static_cast<OvCore::Rendering::SceneRenderer&>(m_renderer)._SetCameraUBO(m_renderer.GetFrameDescriptor().camera.value());

		m_renderer.Clear(false, false, true);

		auto& desc = m_renderer.GetDescriptor<OvEditor::Rendering::DebugSceneRenderer::DebugSceneDescriptor>();

		if (desc.selectedActor)
		{
			auto& selected = desc.selectedActor.value();
			const bool isHovered = desc.highlightedActor && desc.highlightedActor->GetID() == selected.GetID();
			DrawActorDebugElements(selected);
			m_outlineFeature.DrawOutline(selected,
				isHovered ? kHoveredOutlineColor : kSelectedOutlineColor, kSelectedOutlineWidth);
			m_renderer.Clear(false, true, false, OvMaths::FVector3::Zero);
			m_gizmoFeature.DrawGizmo(
				selected.transform.GetWorldPosition(),
				selected.transform.GetWorldRotation(),
				desc.gizmoOperation, false,
				desc.highlightedGizmoDirection);
		}

		if (desc.highlightedActor)
		{
			auto& highlighted = desc.highlightedActor.value();
			if (!desc.selectedActor || highlighted.GetID() != desc.selectedActor->GetID())
				m_outlineFeature.DrawOutline(highlighted, kHoveredOutlineColor, kHoveredOutlineWidth);
		}
	}

private:
	OvRendering::Features::DebugShapeRenderFeature& m_debugShapeFeature;
	OvEditor::Rendering::OutlineRenderFeature& m_outlineFeature;
	OvEditor::Rendering::GizmoRenderFeature& m_gizmoFeature;

	const OvMaths::FVector4 kHoveredOutlineColor{ 1.0f, 1.0f, 0.0f, 1.0f };
	const OvMaths::FVector4 kSelectedOutlineColor{ 1.0f, 0.7f, 0.0f, 1.0f };

	void DrawActorDebugElements(OvCore::ECS::Actor& p_actor)
	{
		if (!p_actor.IsActive()) return;
		if (OvEditor::Settings::EditorSettings::ShowGeometryBounds)
		{
			auto mr = p_actor.GetComponent<OvCore::ECS::Components::CModelRenderer>();
			if (mr && mr->GetModel()) DrawBoundingSpheres(*mr);
		}
		if (auto cam = p_actor.GetComponent<OvCore::ECS::Components::CCamera>()) DrawCameraFrustum(*cam);
		if (auto probe = p_actor.GetComponent<OvCore::ECS::Components::CReflectionProbe>())
			if (probe->GetInfluencePolicy() == OvCore::ECS::Components::CReflectionProbe::EInfluencePolicy::LOCAL)
				DrawReflectionProbeInfluenceVolume(*probe);
		if (p_actor.GetComponent<OvCore::ECS::Components::CPhysicalObject>()) DrawActorCollider(p_actor);
		if (auto ab = p_actor.GetComponent<OvCore::ECS::Components::CAmbientBoxLight>()) DrawAmbientBoxVolume(*ab);
		if (auto as = p_actor.GetComponent<OvCore::ECS::Components::CAmbientSphereLight>()) DrawAmbientSphereVolume(*as);
		if (OvEditor::Settings::EditorSettings::ShowLightBounds)
			if (auto light = p_actor.GetComponent<OvCore::ECS::Components::CLight>()) DrawLightBounds(*light);
		for (auto& child : p_actor.GetChildren()) DrawActorDebugElements(*child);
	}

	void DrawFrustumLines(const OvMaths::FVector3& pos, const OvMaths::FVector3& forward,
		float near, float far,
		const OvMaths::FVector3& a, const OvMaths::FVector3& b,
		const OvMaths::FVector3& c, const OvMaths::FVector3& d,
		const OvMaths::FVector3& e, const OvMaths::FVector3& f,
		const OvMaths::FVector3& g, const OvMaths::FVector3& h)
	{
		auto pso = m_renderer.CreatePipelineState();
		auto draw = [&](const FVector3& s, const FVector3& t, float dist) {
			auto off = pos + forward * dist;
			m_debugShapeFeature.DrawLine(pso, off + s, off + t, kFrustumColor, 1.0f, false);
		};
		draw(a,b,near); draw(b,d,near); draw(d,c,near); draw(c,a,near);
		draw(e,f,far);  draw(f,h,far);  draw(h,g,far);  draw(g,e,far);
		draw(a+forward*near, e+forward*far, 0);
		draw(b+forward*near, f+forward*far, 0);
		draw(c+forward*near, g+forward*far, 0);
		draw(d+forward*near, h+forward*far, 0);
	}

	void DrawCameraPerspectiveFrustum(std::pair<uint16_t,uint16_t>& sz, OvCore::ECS::Components::CCamera& cam)
	{
		auto& camera = cam.GetCamera();
		const auto& pos = cam.owner.transform.GetWorldPosition();
		const auto& rot = cam.owner.transform.GetWorldRotation();
		const auto& fwd = cam.owner.transform.GetWorldForward();
		camera.CacheMatrices(sz.first, sz.second);
		const auto proj = FMatrix4::Transpose(camera.GetProjectionMatrix());
		const auto n = camera.GetNear(), fr = camera.GetFar();
		auto a = rot*FVector3{n*(proj.data[2]-1)/proj.data[0], n*(1+proj.data[6])/proj.data[5], 0};
		auto b = rot*FVector3{n*(1+proj.data[2])/proj.data[0], n*(1+proj.data[6])/proj.data[5], 0};
		auto c = rot*FVector3{n*(proj.data[2]-1)/proj.data[0], n*(proj.data[6]-1)/proj.data[5], 0};
		auto d = rot*FVector3{n*(1+proj.data[2])/proj.data[0], n*(proj.data[6]-1)/proj.data[5], 0};
		auto e = rot*FVector3{fr*(proj.data[2]-1)/proj.data[0], fr*(1+proj.data[6])/proj.data[5], 0};
		auto f = rot*FVector3{fr*(1+proj.data[2])/proj.data[0], fr*(1+proj.data[6])/proj.data[5], 0};
		auto g = rot*FVector3{fr*(proj.data[2]-1)/proj.data[0], fr*(proj.data[6]-1)/proj.data[5], 0};
		auto h = rot*FVector3{fr*(1+proj.data[2])/proj.data[0], fr*(proj.data[6]-1)/proj.data[5], 0};
		DrawFrustumLines(pos, fwd, n, fr, a, b, c, d, e, f, g, h);
	}

	void DrawCameraOrthographicFrustum(std::pair<uint16_t,uint16_t>& sz, OvCore::ECS::Components::CCamera& cam)
	{
		auto& camera = cam.GetCamera();
		const float ratio = sz.first / static_cast<float>(sz.second);
		const auto& pos = cam.owner.transform.GetWorldPosition();
		const auto& rot = cam.owner.transform.GetWorldRotation();
		const auto& fwd = cam.owner.transform.GetWorldForward();
		const auto n = camera.GetNear(), fr = camera.GetFar();
		const auto size = cam.GetSize(), right = ratio * size;
		auto a = rot*FVector3{-right, size, 0}; auto b = rot*FVector3{right, size, 0};
		auto c = rot*FVector3{-right,-size, 0}; auto d = rot*FVector3{right,-size, 0};
		DrawFrustumLines(pos, fwd, n, fr, a, b, c, d, a, b, c, d);
	}

	void DrawCameraFrustum(OvCore::ECS::Components::CCamera& cam)
	{
		auto& gameView = EDITOR_PANEL(OvEditor::Panels::GameView, "Game View");
		auto sz = gameView.GetSafeSize();
		if (sz.first == 0 || sz.second == 0) sz = {16, 9};
		switch (cam.GetProjectionMode())
		{
		case OvRendering::Settings::EProjectionMode::ORTHOGRAPHIC: DrawCameraOrthographicFrustum(sz, cam); break;
		case OvRendering::Settings::EProjectionMode::PERSPECTIVE:  DrawCameraPerspectiveFrustum(sz, cam);  break;
		}
	}

	void DrawReflectionProbeInfluenceVolume(OvCore::ECS::Components::CReflectionProbe& probe)
	{
		auto pso = m_renderer.CreatePipelineState(); pso.depthTest = false;
		m_debugShapeFeature.DrawBox(pso, probe.owner.transform.GetWorldPosition(),
			probe.owner.transform.GetWorldRotation(), probe.GetInfluenceSize(), kDebugBoundsColor, 1.0f, false);
	}

	void DrawActorCollider(OvCore::ECS::Actor& actor)
	{
		using namespace OvCore::ECS::Components;
		auto pso = m_renderer.CreatePipelineState(); pso.depthTest = false;
		if (auto box = actor.GetComponent<CPhysicalBox>())
			m_debugShapeFeature.DrawBox(pso, actor.transform.GetWorldPosition(), actor.transform.GetWorldRotation(),
				box->GetSize() * actor.transform.GetWorldScale(), OvMaths::FVector3{0,1,0}, 1.0f, false);
		if (auto sph = actor.GetComponent<CPhysicalSphere>())
		{
			auto s = actor.transform.GetWorldScale();
			m_debugShapeFeature.DrawSphere(pso, actor.transform.GetWorldPosition(), actor.transform.GetWorldRotation(),
				sph->GetRadius() * std::max({s.x,s.y,s.z,0.f}), OvMaths::FVector3{0,1,0}, 1.0f, false);
		}
		if (auto cap = actor.GetComponent<CPhysicalCapsule>())
		{
			auto s = actor.transform.GetWorldScale();
			m_debugShapeFeature.DrawCapsule(pso, actor.transform.GetWorldPosition(), actor.transform.GetWorldRotation(),
				abs(cap->GetRadius()*std::max({s.x,s.z,0.f})), abs(cap->GetHeight()*s.y),
				OvMaths::FVector3{0,1,0}, 1.0f, false);
		}
	}

	void DrawLightBounds(OvCore::ECS::Components::CLight& light)
	{
		auto pso = m_renderer.CreatePipelineState(); pso.depthTest = false;
		auto& d = light.GetData();
		m_debugShapeFeature.DrawSphere(pso, d.transform->GetWorldPosition(), d.transform->GetWorldRotation(),
			d.CalculateEffectRange(), kDebugBoundsColor, 1.0f, false);
	}

	void DrawAmbientBoxVolume(OvCore::ECS::Components::CAmbientBoxLight& ab)
	{
		auto pso = m_renderer.CreatePipelineState(); pso.depthTest = false;
		auto& d = ab.GetData();
		m_debugShapeFeature.DrawBox(pso, ab.owner.transform.GetWorldPosition(), d.transform->GetWorldRotation(),
			{d.constant, d.linear, d.quadratic}, d.CalculateEffectRange(), 1.0f, false);
	}

	void DrawAmbientSphereVolume(OvCore::ECS::Components::CAmbientSphereLight& as)
	{
		auto pso = m_renderer.CreatePipelineState(); pso.depthTest = false;
		auto& d = as.GetData();
		m_debugShapeFeature.DrawSphere(pso, as.owner.transform.GetWorldPosition(),
			as.owner.transform.GetWorldRotation(), d.constant, kLightVolumeColor, 1.0f, false);
	}

	void DrawBoundingSpheres(OvCore::ECS::Components::CModelRenderer& mr)
	{
		using enum OvCore::ECS::Components::CModelRenderer::EFrustumBehaviour;
		auto pso = m_renderer.CreatePipelineState();
		auto fb = mr.GetFrustumBehaviour();
		if (fb == DISABLED) return;
		if (auto model = mr.GetModel())
		{
			auto& actor = mr.owner;
			const auto& s = actor.transform.GetWorldScale();
			const auto& rot = actor.transform.GetWorldRotation();
			const auto& pos = actor.transform.GetWorldPosition();
			const float rs = std::max({s.x, s.y, s.z, 0.f});
			auto drawBounds = [&](const OvRendering::Geometry::BoundingSphere& b) {
				auto off = OvMaths::FQuaternion::RotatePoint(b.position, rot) * rs;
				m_debugShapeFeature.DrawSphere(pso, pos+off, rot, b.radius*rs, kDebugBoundsColor, 1.0f, false);
			};
			if (fb == MESH_BOUNDS) for (auto mesh : model->GetMeshes()) drawBounds(mesh->GetBoundingSphere());
			else drawBounds(fb == CUSTOM_BOUNDS ? mr.GetCustomBoundingSphere() : model->GetBoundingSphere());
		}
	}
};

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

	m_gridPass                = std::make_unique<GridRenderPass>(*this, *m_debugShapeFeature, *m_debugModelFeature);
	m_debugCamerasPass        = std::make_unique<DebugCamerasRenderPass>(*this, *m_debugModelFeature);
	m_debugReflectionProbesPass = std::make_unique<DebugReflectionProbesRenderPass>(*this, *m_debugModelFeature);
	m_debugLightsPass         = std::make_unique<DebugLightsRenderPass>(*this, *m_debugModelFeature);
	m_debugActorPass          = std::make_unique<DebugActorRenderPass>(*this, *m_debugShapeFeature, *m_outlineFeature, *m_gizmoFeature);
	m_pickingPass             = std::make_unique<PickingRenderPass>(*this, *m_debugModelFeature);
}

const OvRendering::Data::FrameInfo& OvEditor::Rendering::DebugSceneRenderer::GetFrameInfo() const
{
	return m_frameInfoFeature->GetFrameInfo();
}

OvEditor::Rendering::PickingRenderPass& OvEditor::Rendering::DebugSceneRenderer::GetPickingPass()
{
	return *m_pickingPass;
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

	struct GridPassData {};
	p_fg.AddPass<GridPassData>("Grid",
		[](FrameGraphBuilder& b, GridPassData&) { b.SetAsOutput({}); },
		[this](const FrameGraphResources&, const GridPassData&) {
			ZoneScoped;
			auto pso = CreatePipelineState();
			m_gridPass->Draw(pso);
		}
	);

	struct DebugCamerasPassData {};
	p_fg.AddPass<DebugCamerasPassData>("DebugCameras",
		[](FrameGraphBuilder& b, DebugCamerasPassData&) { b.SetAsOutput({}); },
		[this](const FrameGraphResources&, const DebugCamerasPassData&) {
			ZoneScoped;
			auto pso = CreatePipelineState();
			m_debugCamerasPass->Draw(pso);
		}
	);

	struct DebugReflectionPassData {};
	p_fg.AddPass<DebugReflectionPassData>("DebugReflectionProbes",
		[](FrameGraphBuilder& b, DebugReflectionPassData&) { b.SetAsOutput({}); },
		[this](const FrameGraphResources&, const DebugReflectionPassData&) {
			ZoneScoped;
			auto pso = CreatePipelineState();
			m_debugReflectionProbesPass->Draw(pso);
		}
	);

	struct DebugLightsPassData {};
	p_fg.AddPass<DebugLightsPassData>("DebugLights",
		[](FrameGraphBuilder& b, DebugLightsPassData&) { b.SetAsOutput({}); },
		[this](const FrameGraphResources&, const DebugLightsPassData&) {
			ZoneScoped;
			auto pso = CreatePipelineState();
			m_debugLightsPass->Draw(pso);
		}
	);

	struct DebugActorPassData {};
	p_fg.AddPass<DebugActorPassData>("DebugActor",
		[](FrameGraphBuilder& b, DebugActorPassData&) { b.SetAsOutput({}); },
		[this](const FrameGraphResources&, const DebugActorPassData&) {
			ZoneScoped;
			auto pso = CreatePipelineState();
			m_debugActorPass->Draw(pso);
		}
	);

	struct PickingPassData {};
	p_fg.AddPass<PickingPassData>("Picking",
		[](FrameGraphBuilder& b, PickingPassData&) { b.SetAsOutput({}); },
		[this](const FrameGraphResources&, const PickingPassData&) {
			ZoneScoped;
			if (!m_pickingPass->IsEnabled()) return;
			auto pso = CreatePipelineState();
			m_pickingPass->Draw(pso);
		}
	);
}