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
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/UI/Frame.h>

#include <OvDebug/Assertion.h>
#include <OvEditor/Rendering/EditorOverlayCollector.h>
#include <OvEditor/Rendering/DebugShapeCollector.h>
#include <cstring>
#include <OvCore/ECS/Components/CSkinnedMeshRenderer.h>
#include <OvCore/ECS/Components/CAmbientBoxLight.h>
#include <OvCore/ECS/Components/CAmbientSphereLight.h>

#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Core/EditorResources.h>
#include <OvEditor/Rendering/GizmoCollector.h>
#include <OvEditor/Rendering/OutlineCollector.h>
#include <OvEditor/Core/GizmoMath.h>
#include <unordered_map>
#include <unordered_set>
#include <OvEditor/Settings/EditorSettings.h>

#include <OvRendering/Utils/Profiling.h>
#include <OvUI/Styling/Style.h>

using namespace OvMaths;
using namespace OvRendering::Resources;
using namespace OvCore::Resources;
using namespace OvEditor::Rendering;
struct CollectedSelection { OvEditor::Core::EGizmoOperation gizmoOperation; OvTools::Utils::OptRef<OvCore::ECS::Actor> highlightedActor, selectedActor; std::optional<OvEditor::Core::GizmoBehaviour::EDirection> highlightedGizmoDirection; std::vector<OvCore::ECS::Actor*> actors; std::optional<OvEditor::Core::GizmoBehaviour::Frame> frame; };

namespace
{
	const OvMaths::FVector3 kDebugBoundsColor = { 1.0f, 0.0f, 0.0f };
	const OvMaths::FVector3 kLightVolumeColor = { 1.0f, 1.0f, 0.0f };
	const OvMaths::FVector3 kColliderColor = { 0.0f, 1.0f, 0.0f };
	const OvMaths::FVector3 kFrustumColor = { 1.0f, 1.0f, 1.0f };

	const OvMaths::FVector4 kHoveredOutlineColor{ 1.0f, 1.0f, 0.0f, 1.0f };
	const OvMaths::FVector4 kSelectedOutlineColor{ 1.0f, 0.7f, 0.0f, 1.0f };

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

	std::vector<OvMaths::FMatrix4> CreateDebugLights() { return {CreateDebugDirectionalLight(),CreateDebugAmbientLight()}; }

	OvCore::ECS::Components::CCanvas* FindRootCanvas(OvCore::ECS::Actor& actor)
	{
		OvCore::ECS::Components::CCanvas* result = nullptr;
		for (auto* current = &actor; current; current = current->GetParent())
			if (auto* canvas = current->GetComponent<OvCore::ECS::Components::CCanvas>()) result = canvas;
		return result;
	}

	bool HasEditableTransformGizmo(OvCore::ECS::Actor& actor)
	{
		const auto* rect = actor.GetComponent<OvCore::ECS::Components::CRectTransform>();
		const auto* rootCanvas = FindRootCanvas(actor);
		return !rect || !rootCanvas ||
			(&rootCanvas->owner == &actor && rootCanvas->GetRenderMode() == OvCore::ECS::Components::CCanvas::RenderMode::WorldSpace);
	}
}

class DebugCamerasCollector
{
public:
	DebugCamerasCollector(OvCore::Rendering::ToolDrawCollector& p_collector) : m_collector(p_collector)
	{
		m_fakeLights = CreateDebugLights();

		m_cameraMaterial.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\Standard.ovfx"]);
		m_cameraMaterial.SetProperty("u_Albedo", FVector4{ 0.0f, 0.447f, 1.0f, 1.0f });
		m_cameraMaterial.SetProperty("u_Metallic", 0.0f);
		m_cameraMaterial.SetProperty("u_Roughness", 0.25f);
		m_cameraMaterial.SetProperty("u_BuiltInGammaCorrection", true);
		m_cameraMaterial.SetProperty("u_BuiltInToneMapping", true);
	}

public:
	void Draw(OvRendering::Data::PipelineState p_pso, OvCore::SceneSystem::Scene& scene)
	{
		ZoneScoped;
		OV_GPU_ZONE(m_collector, "DebugCamerasCollector");



		m_collector.SetLights(m_fakeLights);

		for (auto camera : scene.GetFastAccessComponents().cameras)
		{
			auto& actor = camera->owner;

			if (actor.IsActive())
			{
				auto& model = *EDITOR_CONTEXT(editorResources)->GetModel("Camera");
				auto modelMatrix = CalculateUnscaledModelMatrix(actor);

				m_collector
					.CollectModel(p_pso, model, m_cameraMaterial, modelMatrix);
			}
		}

		m_collector.SetLights({});
	}

private:
    OvCore::Rendering::ToolDrawCollector& m_collector;
	OvCore::Resources::Material m_cameraMaterial;
	std::vector<OvMaths::FMatrix4> m_fakeLights;
};

class DebugReflectionProbesCollector
{
public:
	DebugReflectionProbesCollector(OvCore::Rendering::ToolDrawCollector& p_collector) : m_collector(p_collector)
	{
		m_fakeLights = CreateDebugLights();

		m_reflectiveMaterial.SetDepthTest(false);
		m_reflectiveMaterial.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\Standard.ovfx"]);
		m_reflectiveMaterial.SetProperty("u_Albedo", FVector4{ 1.0, 1.0f, 1.0f, 1.0f });
		m_reflectiveMaterial.SetProperty("u_Metallic", 1.0f);
		m_reflectiveMaterial.SetProperty("u_Roughness", 0.0f);
		m_reflectiveMaterial.SetProperty("u_BuiltInGammaCorrection", true);
		m_reflectiveMaterial.SetProperty("u_BuiltInToneMapping", true);
	}

public:
	void Draw(OvRendering::Data::PipelineState p_pso, OvCore::SceneSystem::Scene& scene)
	{
		ZoneScoped;
		OV_GPU_ZONE(m_collector, "DebugReflectionProbesCollector");



		m_collector.SetLights(m_fakeLights);

		for (auto reflectionProbe : scene.GetFastAccessComponents().reflectionProbes)
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

				OvRendering::Data::ReflectionReceiverBindings binding;
                binding.map = reflectionProbe->GetCubemap();
                const auto bytes = reflectionProbe->CaptureUniformData();
                std::memcpy(&binding.constants,bytes.data(),bytes.size());
                m_collector.SetReflection(binding);

				m_collector
					.CollectModelWithTextures(p_pso, model, m_reflectiveMaterial, modelMatrix, {reflectionProbe->GetCubemap()});
			}
		}

		m_collector.SetLights({});
	}

private:
    OvCore::Rendering::ToolDrawCollector& m_collector;
	OvCore::Resources::Material m_reflectiveMaterial;
	std::vector<OvMaths::FMatrix4> m_fakeLights;
};

class DebugLightsCollector
{
public:
	DebugLightsCollector(OvCore::Rendering::ToolDrawCollector& p_collector) : m_collector(p_collector)
	{
		m_lightMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("Billboard"));
		m_lightMaterial.SetProperty("u_Diffuse", FVector4{ 1.f, 1.f, 0.5f, 0.5f });
		m_lightMaterial.SetBackfaceCulling(false);
		m_lightMaterial.SetBlendable(true);
		m_lightMaterial.SetDepthTest(false);
	}

public:
	void Draw(OvRendering::Data::PipelineState p_pso, OvCore::SceneSystem::Scene& scene)
	{
		ZoneScoped;
		OV_GPU_ZONE(m_collector, "DebugLightsCollector");


		if (OvEditor::Settings::EditorSettings::LightBillboardScale <= 0.001f) return;
		m_lightMaterial.SetProperty("u_Scale", OvEditor::Settings::EditorSettings::LightBillboardScale * 48.0f * OVUI_SCALE);
		m_lightMaterial.SetProperty("u_ViewportHeight", static_cast<float>(m_collector.GetView().input.height));

		for (auto light : scene.GetFastAccessComponents().lights)
		{
			auto& actor = light->owner;

			if (actor.IsActive())
			{
				auto& model = *EDITOR_CONTEXT(editorResources)->GetModel("Vertical_Plane");
				auto modelMatrix = OvMaths::FMatrix4::Translation(actor.transform.GetWorldPosition());

				auto lightTypeTextureName = GetLightTypeTextureName(light->GetData().type);

				auto lightTexture =
					lightTypeTextureName ?
					EDITOR_CONTEXT(editorResources)->GetTexture(lightTypeTextureName.value()) :
					nullptr;

				const auto& lightColor = light->GetColor();
				m_lightMaterial.SetProperty("u_DiffuseMap", lightTexture);
				m_lightMaterial.SetProperty("u_Diffuse", OvMaths::FVector4(lightColor.x, lightColor.y, lightColor.z, 0.75f));

				m_collector
					.CollectModel(p_pso, model, m_lightMaterial, modelMatrix);
			}
		}
	}

private:
    OvCore::Rendering::ToolDrawCollector& m_collector;
	OvCore::Resources::Material m_lightMaterial;
};

class DebugActorCollector
{
public:
	DebugActorCollector(OvCore::Rendering::ToolDrawCollector& collector, DebugShapeCollector& shapes, OutlineCollector& outline, GizmoCollector& gizmo) : m_collector(collector), m_debugShapeFeature(shapes), m_outline(outline), m_gizmo(gizmo) {}

public:
	OvCore::Rendering::ToolDrawCollector& m_collector;
    DebugShapeCollector& m_debugShapeFeature;
    OutlineCollector& m_outline;
    GizmoCollector& m_gizmo;
    std::pair<uint16_t,uint16_t> m_gameViewSize{16,9};

	void Draw(OvRendering::Data::PipelineState p_pso, const CollectedSelection& debugSceneDescriptor, const OvEditor::Rendering::EditorOverlayRequest& request)
	{
		m_gameViewSize = request.gameViewSize;
        ZoneScoped;
		OV_GPU_ZONE(m_collector, "DebugActorCollector");

		// Clear stencil buffer for outline rendering
		m_collector.Clear(false, false, true);



        std::unordered_set<OvCore::ECS::Actor*> explicitSelection(debugSceneDescriptor.actors.begin(),debugSceneDescriptor.actors.end());
        if (debugSceneDescriptor.selectedActor) explicitSelection.insert(&debugSceneDescriptor.selectedActor.value());
        std::unordered_set<OvCore::ECS::Actor*> drawn;
        auto roots = debugSceneDescriptor.actors;
        if (roots.empty() && debugSceneDescriptor.selectedActor) roots.push_back(&debugSceneDescriptor.selectedActor.value());
        for (auto* root : roots)
        {
            std::vector<OvCore::ECS::Actor*> pending{root};
            while (!pending.empty())
            {
                auto* actor = pending.back(); pending.pop_back();
                if (!actor->IsAlive() || !actor->IsActive() || !drawn.insert(actor).second) continue;
                auto* owner = actor;
                while (owner && !explicitSelection.contains(owner)) owner = owner->GetParent();
                const bool primary = debugSceneDescriptor.selectedActor && owner == &debugSceneDescriptor.selectedActor.value();
                const OvMaths::FVector4 color = primary ? kSelectedOutlineColor : OvMaths::FVector4{.25f,.65f,1.f,1.f};
                m_collector.Clear(false,false,true);
                m_collector.SetPassName("Editor.Outline");m_outline.DrawOutline(*actor,color,primary ? kSelectedOutlineWidth : 3.f,false);
                if (!actor->GetComponent<OvCore::ECS::Components::CModelRenderer>() && !actor->GetComponent<OvCore::ECS::Components::CCamera>())
                {
                    const auto& view = m_collector.GetView();
                    const auto position = actor->transform.GetWorldPosition();
                    const float radius = OvEditor::Core::GizmoMath::WorldSize(position,view.view,view.projection,float(view.input.height),8.f * OVUI_SCALE);
                    m_debugShapeFeature.DrawSphere(m_collector.CreatePipelineState(),position,OvMaths::FQuaternion::Identity,radius,{color.x,color.y,color.z},2.f,false);
                }
                DrawActorDebugElements(*actor,request);
                for (auto* child : actor->GetChildren()) pending.push_back(child);
            }
        }
        if (debugSceneDescriptor.selectedActor)
        {
            auto& selectedActor = debugSceneDescriptor.selectedActor.value();
            if (auto* canvas = FindRootCanvas(selectedActor)) DrawCanvasRect(*canvas);
            m_collector.SetPassName("Editor.GizmoDepthStencil");m_collector.Clear(false,true,false);
            if (HasEditableTransformGizmo(selectedActor))
                m_gizmo.DrawGizmo(debugSceneDescriptor.frame ? debugSceneDescriptor.frame->position : selectedActor.transform.GetWorldPosition(),
                    debugSceneDescriptor.frame ? debugSceneDescriptor.frame->rotation : selectedActor.transform.GetWorldRotation(),
                    debugSceneDescriptor.gizmoOperation,false,debugSceneDescriptor.highlightedGizmoDirection);
        }

		if (debugSceneDescriptor.highlightedActor)
		{
			auto& highlightedActor = debugSceneDescriptor.highlightedActor.value();

			// Render the outline only if the actor is not already selected (as its outline render should have been handled already).
			if (!drawn.contains(&highlightedActor))
			{
				m_collector.SetPassName("Editor.Outline");m_outline.DrawOutline(highlightedActor, kHoveredOutlineColor, kHoveredOutlineWidth);
			}
		}
	}

	void DrawActorDebugElements(OvCore::ECS::Actor& p_actor, const OvEditor::Rendering::EditorOverlayRequest& request)
	{
		if (p_actor.IsActive())
		{
			/* Render static mesh outline and bounding spheres */
			if (request.geometryBounds)
			{
				auto modelRenderer = p_actor.GetComponent<OvCore::ECS::Components::CModelRenderer>();

				if (modelRenderer && modelRenderer->GetModel())
				{
					DrawBoundingSpheres(*modelRenderer);
				}
			}

			/* Render camera component frustum */
			if (auto cameraComponent = p_actor.GetComponent<OvCore::ECS::Components::CCamera>(); cameraComponent && request.cameraFrustums)
			{
				DrawCameraFrustum(*cameraComponent);
			}

			/* Render camera component frustum */
			if (auto reflectionProbeComponent = p_actor.GetComponent<OvCore::ECS::Components::CReflectionProbe>(); reflectionProbeComponent && request.reflectionIcons)
			{
				if (reflectionProbeComponent->GetInfluencePolicy() == OvCore::ECS::Components::CReflectionProbe::EInfluencePolicy::LOCAL)
				{
					DrawReflectionProbeInfluenceVolume(*reflectionProbeComponent);
				}
			}

			/* Render the actor collider */
			if (request.colliders && p_actor.GetComponent<OvCore::ECS::Components::CPhysicalObject>())
			{
				DrawActorCollider(p_actor);
			}

			/* Render the actor ambient light */
			if (auto ambientBoxComp = p_actor.GetComponent<OvCore::ECS::Components::CAmbientBoxLight>(); ambientBoxComp && request.lightBounds)
			{
				DrawAmbientBoxVolume(*ambientBoxComp);
			}

			if (auto ambientSphereComp = p_actor.GetComponent<OvCore::ECS::Components::CAmbientSphereLight>(); ambientSphereComp && request.lightBounds)
			{
				DrawAmbientSphereVolume(*ambientSphereComp);
			}

			if (request.lightBounds)
			{
				if (auto light = p_actor.GetComponent<OvCore::ECS::Components::CLight>())
				{
					DrawLightBounds(*light);
				}
			}

		}
	}

	void DrawCanvasRect(OvCore::ECS::Components::CCanvas& canvas)
	{
		for (auto* parent = canvas.owner.GetParent(); parent; parent = parent->GetParent())
			if (parent->GetComponent<OvCore::ECS::Components::CCanvas>()) return;
		auto viewport = OvMaths::FVector2{static_cast<float>(m_gameViewSize.first), static_cast<float>(m_gameViewSize.second)};
		if (viewport.x <= 0 || viewport.y <= 0) viewport = {16, 9};
		const auto geometry = OvCore::UI::ResolveCanvasGeometry(canvas, viewport);
		if (!geometry) return;
		const auto end = geometry->rect.min + geometry->rect.size;
		const std::array<OvMaths::FVector2, 4> local{geometry->rect.min,
			OvMaths::FVector2{geometry->rect.min.x, end.y}, end, OvMaths::FVector2{end.x, geometry->rect.min.y}};
		std::array<OvMaths::FVector3, 4> world;
		for (size_t i = 0; i < local.size(); ++i)
		{
			const auto point = geometry->localToWorld * OvMaths::FVector4{local[i].x, local[i].y, 0, 1};
			world[i] = {point.x, point.y, point.z};
		}
		auto pso = m_collector.CreatePipelineState(); pso.depthTest = false;
		const OvMaths::FVector3 color{0.2f, 0.75f, 1.0f};
		for (size_t i = 0; i < world.size(); ++i)
			m_debugShapeFeature.DrawLine(pso, world[i], world[(i + 1) % world.size()], color, 1.5f, false);
	}

	void DrawFrustumLines(
		const OvMaths::FVector3& pos,
		const OvMaths::FVector3& forward,
		float near,
		const float far,
		const OvMaths::FVector3& a,
		const OvMaths::FVector3& b,
		const OvMaths::FVector3& c,
		const OvMaths::FVector3& d,
		const OvMaths::FVector3& e,
		const OvMaths::FVector3& f,
		const OvMaths::FVector3& g,
		const OvMaths::FVector3& h
	)
	{
		auto pso = m_collector.CreatePipelineState();

		// Convenient lambda to draw a frustum line
		auto draw = [&](const FVector3& p_start, const FVector3& p_end, const float planeDistance) {
			auto offset = pos + forward * planeDistance;
			auto start = offset + p_start;
			auto end = offset + p_end;
			m_debugShapeFeature.DrawLine(pso, start, end, kFrustumColor, 1.0f, false);
		};

		// Draw near plane
		draw(a, b, near);
		draw(b, d, near);
		draw(d, c, near);
		draw(c, a, near);

		// Draw far plane
		draw(e, f, far);
		draw(f, h, far);
		draw(h, g, far);
		draw(g, e, far);

		// Draw lines between near and far planes
		draw(a + forward * near, e + forward * far, 0);
		draw(b + forward * near, f + forward * far, 0);
		draw(c + forward * near, g + forward * far, 0);
		draw(d + forward * near, h + forward * far, 0);
	}

	void DrawCameraPerspectiveFrustum(std::pair<uint16_t, uint16_t>& p_size, OvCore::ECS::Components::CCamera& p_camera)
	{
		const auto& owner = p_camera.owner;
		auto& camera = p_camera.GetCamera();

		const auto& cameraPos = owner.transform.GetWorldPosition();
		const auto& cameraRotation = owner.transform.GetWorldRotation();
		const auto& cameraForward = owner.transform.GetWorldForward();

		camera.CacheMatrices(p_size.first, p_size.second); // TODO: We shouldn't cache matrices mid air, we could use another function to get the matrices/calculate
		const auto proj = FMatrix4::Transpose(camera.GetProjectionMatrix());
		const auto near = camera.GetNear();
		const auto far = camera.GetFar();

		const auto nLeft = near * (proj.data[2] - 1.0f) / proj.data[0];
		const auto nRight = near * (1.0f + proj.data[2]) / proj.data[0];
		const auto nTop = near * (1.0f + proj.data[6]) / proj.data[5];
		const auto nBottom = near * (proj.data[6] - 1.0f) / proj.data[5];

		const auto fLeft = far * (proj.data[2] - 1.0f) / proj.data[0];
		const auto fRight = far * (1.0f + proj.data[2]) / proj.data[0];
		const auto fTop = far * (1.0f + proj.data[6]) / proj.data[5];
		const auto fBottom = far * (proj.data[6] - 1.0f) / proj.data[5];

		auto a = cameraRotation * FVector3{ nLeft, nTop, 0 };
		auto b = cameraRotation * FVector3{ nRight, nTop, 0 };
		auto c = cameraRotation * FVector3{ nLeft, nBottom, 0 };
		auto d = cameraRotation * FVector3{ nRight, nBottom, 0 };
		auto e = cameraRotation * FVector3{ fLeft, fTop, 0 };
		auto f = cameraRotation * FVector3{ fRight, fTop, 0 };
		auto g = cameraRotation * FVector3{ fLeft, fBottom, 0 };
		auto h = cameraRotation * FVector3{ fRight, fBottom, 0 };

		DrawFrustumLines(cameraPos, cameraForward, near, far, a, b, c, d, e, f, g, h);
	}

	void DrawCameraOrthographicFrustum(std::pair<uint16_t, uint16_t>& p_size, OvCore::ECS::Components::CCamera& p_camera)
	{
		auto& owner = p_camera.owner;
		auto& camera = p_camera.GetCamera();
		const auto ratio = p_size.first / static_cast<float>(p_size.second);

		const auto& cameraPos = owner.transform.GetWorldPosition();
		const auto& cameraRotation = owner.transform.GetWorldRotation();
		const auto& cameraForward = owner.transform.GetWorldForward();

		const auto near = camera.GetNear();
		const auto far = camera.GetFar();
		const auto size = p_camera.GetSize();

		const auto right = ratio * size;
		const auto left = -right;
		const auto top = size;
		const auto bottom = -top;

		const auto a = cameraRotation * FVector3{ left, top, 0 };
		const auto b = cameraRotation * FVector3{ right, top, 0 };
		const auto c = cameraRotation * FVector3{ left, bottom, 0 };
		const auto d = cameraRotation * FVector3{ right, bottom, 0 };

		DrawFrustumLines(cameraPos, cameraForward, near, far, a, b, c, d, a, b, c, d);
	}

	void DrawCameraFrustum(OvCore::ECS::Components::CCamera& p_camera)
	{
		auto gameViewSize = m_gameViewSize;

		if (gameViewSize.first == 0 || gameViewSize.second == 0)
		{
			gameViewSize = { 16, 9 };
		}

		switch (p_camera.GetProjectionMode())
		{
		case OvRendering::Settings::EProjectionMode::ORTHOGRAPHIC:
			DrawCameraOrthographicFrustum(gameViewSize, p_camera);
			break;

		case OvRendering::Settings::EProjectionMode::PERSPECTIVE:
			DrawCameraPerspectiveFrustum(gameViewSize, p_camera);
			break;
		}
	}

	void DrawReflectionProbeInfluenceVolume(OvCore::ECS::Components::CReflectionProbe& p_reflectionProbe)
	{
		auto pso = m_collector.CreatePipelineState();
		pso.depthTest = false;
		const auto& size = p_reflectionProbe.GetInfluenceSize();
		const auto position = p_reflectionProbe.owner.transform.GetWorldPosition();
		m_debugShapeFeature.DrawBox(
			pso,
			position,
			p_reflectionProbe.owner.transform.GetWorldRotation(),
			size,
			kDebugBoundsColor,
			1.0f,
			false
		);
	}

	void DrawActorCollider(OvCore::ECS::Actor& p_actor)
	{
		using namespace OvCore::ECS::Components;
		using namespace OvPhysics::Entities;

		auto pso = m_collector.CreatePipelineState();
		pso.depthTest = false;

		/* Draw the box collider if any */
		if (auto boxColliderComponent = p_actor.GetComponent<OvCore::ECS::Components::CPhysicalBox>(); boxColliderComponent)
		{
			m_debugShapeFeature.DrawBox(
				pso,
				p_actor.transform.GetWorldPosition(),
				p_actor.transform.GetWorldRotation(),
				boxColliderComponent->GetSize() * p_actor.transform.GetWorldScale(),
				OvMaths::FVector3{ 0.f, 1.f, 0.f },
				1.0f,
				false
			);
		}

		/* Draw the sphere collider if any */
		if (auto sphereColliderComponent = p_actor.GetComponent<OvCore::ECS::Components::CPhysicalSphere>(); sphereColliderComponent)
		{
			FVector3 actorScale = p_actor.transform.GetWorldScale();
			float radius = sphereColliderComponent->GetRadius() * std::max(std::max(std::max(actorScale.x, actorScale.y), actorScale.z), 0.0f);

			m_debugShapeFeature.DrawSphere(
				pso,
				p_actor.transform.GetWorldPosition(),
				p_actor.transform.GetWorldRotation(),
				radius,
				OvMaths::FVector3{ 0.f, 1.f, 0.f },
				1.0f,
				false
			);
		}

		/* Draw the capsule collider if any */
		if (auto capsuleColliderComponent = p_actor.GetComponent<OvCore::ECS::Components::CPhysicalCapsule>(); capsuleColliderComponent)
		{
			FVector3 actorScale = p_actor.transform.GetWorldScale();
			float radius = std::abs(capsuleColliderComponent->GetRadius() * std::max(std::max(actorScale.x, actorScale.z), 0.f));
			float height = std::abs(capsuleColliderComponent->GetHeight() * actorScale.y);

			m_debugShapeFeature.DrawCapsule(
				pso,
				p_actor.transform.GetWorldPosition(),
				p_actor.transform.GetWorldRotation(),
				radius,
				height,
				OvMaths::FVector3{ 0.f, 1.f, 0.f },
				1.0f,
				false
			);
		}
	}

	void DrawLightBounds(OvCore::ECS::Components::CLight& p_light)
	{
		auto pso = m_collector.CreatePipelineState();
		pso.depthTest = false;

		auto& data = p_light.GetData();

		m_debugShapeFeature.DrawSphere(
			pso,
			data.transform->GetWorldPosition(),
			data.transform->GetWorldRotation(),
			data.CalculateEffectRange(),
			kDebugBoundsColor,
			1.0f,
			false
		);
	}

	void DrawAmbientBoxVolume(OvCore::ECS::Components::CAmbientBoxLight& p_ambientBoxLight)
	{
		auto pso = m_collector.CreatePipelineState();
		pso.depthTest = false;

		auto& data = p_ambientBoxLight.GetData();

		m_debugShapeFeature.DrawBox(
			pso,
			p_ambientBoxLight.owner.transform.GetWorldPosition(),
			data.transform->GetWorldRotation(),
			{ data.constant, data.linear, data.quadratic },
			data.CalculateEffectRange(),
			1.0f,
			false
		);
	}

	void DrawAmbientSphereVolume(OvCore::ECS::Components::CAmbientSphereLight& p_ambientSphereLight)
	{
		auto pso = m_collector.CreatePipelineState();
		pso.depthTest = false;

		auto& data = p_ambientSphereLight.GetData();

		m_debugShapeFeature.DrawSphere(
			pso,
			p_ambientSphereLight.owner.transform.GetWorldPosition(),
			p_ambientSphereLight.owner.transform.GetWorldRotation(),
			data.constant,
			kLightVolumeColor,
			1.0f,
			false
		);
	}

	void DrawBoundingSpheres(OvCore::ECS::Components::CModelRenderer& p_modelRenderer)
	{
		using namespace OvCore::ECS::Components;
		using namespace OvPhysics::Entities;
		using enum OvCore::ECS::Components::CModelRenderer::EFrustumBehaviour;

		auto pso = m_collector.CreatePipelineState();

		const auto frustumBehaviour = p_modelRenderer.GetFrustumBehaviour();

		if (frustumBehaviour == DISABLED)
		{
			return; // No bounds to draw
		}

		// Draw the mesh, model, or custom bounding sphere
		if (auto model = p_modelRenderer.GetModel())
		{
			auto& actor = p_modelRenderer.owner;

			const auto& actorScale = actor.transform.GetWorldScale();
			const auto& actorRotation = actor.transform.GetWorldRotation();
			const auto& actorPosition = actor.transform.GetWorldPosition();

			float radiusScale = std::max(std::max(std::max(actorScale.x, actorScale.y), actorScale.z), 0.0f);

			const auto skinnedMeshRenderer = actor.GetComponent<CSkinnedMeshRenderer>();
			if (skinnedMeshRenderer && skinnedMeshRenderer->HasSkinningData())
			{
				radiusScale *= skinnedMeshRenderer->GetMeshBoundsScale();
			}

			auto drawBounds = [&](const OvRendering::Geometry::BoundingSphere& p_bounds) {
				const float scaledRadius = p_bounds.radius * radiusScale;
				const auto sphereOffset = OvMaths::FQuaternion::RotatePoint(
					p_bounds.position,
					actorRotation
				) * radiusScale;

				m_debugShapeFeature.DrawSphere(
					pso,
					actorPosition + sphereOffset,
					actorRotation,
					scaledRadius,
					kDebugBoundsColor,
					1.0f,
					false
				);
			};

			if (frustumBehaviour == MESH_BOUNDS)
			{
				for (auto mesh : model->GetMeshes())
				{
					drawBounds(mesh->GetBoundingSphere());
				}
			}
			else
			{
				drawBounds(
					frustumBehaviour == CUSTOM_BOUNDS ?
					p_modelRenderer.GetCustomBoundingSphere() :
					model->GetBoundingSphere()
				);
			}
		}
	}
};


struct OvEditor::Rendering::EditorOverlayCollector::Impl
{
    OvCore::Rendering::ToolDrawCollector tools;
    DebugShapeCollector shapes{tools};
    OutlineCollector outline{tools};
    GizmoCollector gizmo{tools};
    DebugCamerasCollector cameras{tools};
    DebugReflectionProbesCollector probes{tools};
    DebugLightsCollector lights{tools};
    DebugActorCollector actor{tools,shapes,outline,gizmo};
    OvRendering::Data::Material gridMaterial;
    Impl()
    {
        gridMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("Grid"));
        gridMaterial.SetBlendable(true);gridMaterial.SetBackfaceCulling(false);
        gridMaterial.SetDepthWriting(false);gridMaterial.SetDepthTest(true);
    }
    void Grid(const EditorOverlayRequest& request,const OvRendering::Data::RenderView& view)
    {
        const auto pso=tools.CreatePipelineState();
	constexpr float gridSize = 5000.0f;

	OvMaths::FMatrix4 model =
		OvMaths::FMatrix4::Translation({ view.input.position.x, 0.0f, view.input.position.z }) *
		OvMaths::FMatrix4::Scaling({ gridSize * 2.0f, 1.f, gridSize * 2.0f });

	gridMaterial.SetProperty("u_Color", request.gridColor);

	tools
		.CollectModel(pso, *EDITOR_CONTEXT(editorResources)->GetModel("Plane"), gridMaterial, model);

	constexpr float kLineWidth = 1.0f;

	shapes.DrawLine(pso, OvMaths::FVector3(-gridSize + view.input.position.x, 0.0f, 0.0f), OvMaths::FVector3(gridSize + view.input.position.x, 0.0f, 0.0f), OvMaths::FVector3::Right, kLineWidth);
	shapes.DrawLine(pso, OvMaths::FVector3(0.0f, -gridSize + view.input.position.y, 0.0f), OvMaths::FVector3(0.0f, gridSize + view.input.position.y, 0.0f), OvMaths::FVector3::Up, kLineWidth);
	shapes.DrawLine(pso, OvMaths::FVector3(0.0f, 0.0f, -gridSize + view.input.position.z), OvMaths::FVector3(0.0f, 0.0f, gridSize + view.input.position.z), OvMaths::FVector3::Forward, kLineWidth);

    }
};
OvEditor::Rendering::EditorOverlayCollector::EditorOverlayCollector() : m_impl(std::make_unique<Impl>()) {}
OvEditor::Rendering::EditorOverlayCollector::~EditorOverlayCollector() = default;
OvRendering::Data::RenderCommandList OvEditor::Rendering::EditorOverlayCollector::Extract(
    OvCore::SceneSystem::Scene& scene,const EditorOverlayRequest& request,const OvRendering::Data::RenderView& view,
    OvRendering::Data::PipelineState state,baregl::Framebuffer::Snapshot target,OvRendering::Data::RenderPlan* plan)
{
    if(request.scene != scene.GetLifetimeToken())throw std::invalid_argument("Overlay request belongs to another scene");
    auto& impl=*m_impl;
    impl.tools.Begin(view,state,target,plan);impl.shapes.Begin(view);
    if(request.grid){impl.tools.SetPassName("Editor.Grid");impl.Grid(request,view);}
    if(request.debugObjects)
    {
        impl.tools.SetPassName("Editor.DebugObjects");
        if (request.cameraIcons) impl.cameras.Draw(state,scene);
        if (request.reflectionIcons) impl.probes.Draw(state,scene);
        if (request.lightIcons) impl.lights.Draw(state,scene);
        CollectedSelection selection{request.operation,std::nullopt,std::nullopt,request.highlightedDirection};
        if(auto* actor=scene.FindActorByGUID(request.highlighted))selection.highlightedActor=*actor;
        if(auto* actor=scene.FindActorByGUID(request.selected))selection.selectedActor=*actor;
        for (auto guid : request.selection) if (auto* actor = scene.FindActorByGUID(guid)) selection.actors.push_back(actor);
        selection.frame = request.gizmoFrame;
        impl.actor.Draw(state,selection,request);
    }
    return impl.tools.Finish();
}

const OvRendering::Data::FrameInfo& OvEditor::Rendering::EditorOverlayCollector::GetFrameInfo() const { return m_impl->tools.GetFrameInfo(); }
