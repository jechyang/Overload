/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <ranges>
#include <string>

#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvCore/ECS/Components/CSkinnedMeshRenderer.h>
#include <OvCore/Rendering/EngineDrawableDescriptor.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <OvCore/Rendering/SkinningDrawableDescriptor.h>
#include <OvCore/Rendering/SkinningUtils.h>

#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Rendering/DebugModelRenderFeature.h>
#include <OvEditor/Rendering/DebugSceneRenderer.h>
#include <OvEditor/Rendering/PickingRenderPass.h>
#include <OvEditor/Settings/EditorSettings.h>

#include <OvRendering/Utils/Profiling.h>

namespace
{
	const std::string kPickingPassName = "PICKING_PASS";
	const std::string kSkinningFeatureName = std::string{ OvCore::Rendering::SkinningUtils::kFeatureName };

	void PreparePickingMaterial(
		const OvCore::ECS::Actor& p_actor,
		OvRendering::Data::Material& p_material,
		const std::string& p_uniformName = "_PickingColor"
	)
	{
		uint32_t actorID = static_cast<uint32_t>(p_actor.GetID());

		auto bytes = reinterpret_cast<uint8_t*>(&actorID);
		auto color = OvMaths::FVector4{ bytes[0] / 255.0f, bytes[1] / 255.0f, bytes[2] / 255.0f, 1.0f };

		// Set the picking color property if it exists
		if (p_material.GetProperty(p_uniformName))
		{
			p_material.SetProperty(p_uniformName, color, true);
		}
	}
}

OvEditor::Rendering::PickingRenderPass::PickingRenderPass(OvRendering::Core::CompositeRenderer& p_renderer) :
	OvRendering::Core::ARenderPass(p_renderer),
	m_actorPickingFramebuffer("ActorPicking")
{
	OvCore::Rendering::FramebufferUtil::SetupFramebuffer(
		m_actorPickingFramebuffer, 1, 1, true, false, false
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
	m_actorPickingFallbackMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("PickingFallback"));
}

OvEditor::Rendering::PickingRenderPass::PickingResult OvEditor::Rendering::PickingRenderPass::ReadbackPickingResult(
	const OvCore::SceneSystem::Scene& p_scene,
	uint32_t p_x,
	uint32_t p_y
)
{
    if (m_readback) return m_readback->Resolve(p_scene, p_x, p_y);
    if (m_renderer.IsRecordingCommands()) return std::nullopt;
    const auto [width, height] = m_actorPickingFramebuffer.GetSize();
    if (p_x >= width || p_y >= height) return std::nullopt;
	uint8_t pixel[4]{};

	m_actorPickingFramebuffer.ReadPixels(
		p_x, p_y, 1, 1,
		baregl::types::EPixelDataFormat::RGBA,
		baregl::types::EPixelDataType::UNSIGNED_BYTE,
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

OvEditor::Rendering::PickingRenderPass::PickingResult OvEditor::Rendering::PickingRenderPass::Readback::Resolve(
    const OvCore::SceneSystem::Scene& p_scene, uint32_t p_x, uint32_t p_y) const
{
    if (!IsReady() || scene != p_scene.GetLifetimeToken() || p_x >= width || p_y >= height) return std::nullopt;
    const auto* pixel = pixels.data() + (size_t(p_y) * width + p_x) * 4;
    const uint32_t actorID = (uint32_t(pixel[2]) << 16) | (uint32_t(pixel[1]) << 8) | pixel[0];
    if (const auto found = actors.find(actorID); found != actors.end())
    {
        if (auto* actor = p_scene.FindActorByID(actorID); actor && actor->GetGUID() == found->second)
            return OvTools::Utils::OptRef(*actor);
        return std::nullopt;
    }
    if (pixel[0] == 255 && pixel[1] == 255 && pixel[2] >= 252 && pixel[2] <= 254 &&
        gizmoActorGUID && p_scene.FindActorByGUID(gizmoActorGUID))
        return static_cast<OvEditor::Core::GizmoBehaviour::EDirection>(pixel[2] - 252);
    return std::nullopt;
}

void OvEditor::Rendering::PickingRenderPass::Draw(OvRendering::Data::PipelineState p_pso)
{
	ZoneScoped;
	OV_GPU_ZONE(m_renderer, "PickingRenderPass");

	using namespace OvCore::Rendering;

	OVASSERT(m_renderer.HasDescriptor<SceneRenderer::SceneDescriptor>(), "Cannot find SceneDescriptor attached to this renderer");
	OVASSERT(m_renderer.HasDescriptor<DebugSceneRenderer::DebugSceneDescriptor>(), "Cannot find DebugSceneDescriptor attached to this renderer");

	auto& sceneDescriptor = m_renderer.GetDescriptor<SceneRenderer::SceneDescriptor>();
	auto& debugSceneDescriptor = m_renderer.GetDescriptor<DebugSceneRenderer::DebugSceneDescriptor>();
	auto& frameDescriptor = m_renderer.GetFrameDescriptor();
	auto& scene = sceneDescriptor.scene;

	m_readback.reset();
	m_actorPickingFramebuffer.Resize(frameDescriptor.renderWidth, frameDescriptor.renderHeight);

	m_renderer.BindTarget(&m_actorPickingFramebuffer);
	
	auto pso = m_renderer.CreatePipelineState();

	m_renderer.Clear(true, true, true);

	DrawPickableModels(pso, scene);
	DrawPickableCameras(pso, scene);
	DrawPickableReflectionProbes(pso, scene);
	DrawPickableLights(pso, scene);

	// Clear depth, gizmos are rendered on top of everything else
	m_renderer.Clear(false, true, false);

	if (debugSceneDescriptor.selectedActor)
	{
		auto& selectedActor = debugSceneDescriptor.selectedActor.value();

		DrawPickableGizmo(
			pso,
			selectedActor.transform.GetWorldPosition(),
			selectedActor.transform.GetWorldRotation(),
			debugSceneDescriptor.gizmoOperation
		);
	}

    if (m_renderer.IsRecordingCommands())
    {
        auto readback = std::make_shared<Readback>();
        readback->completion = m_renderer.GetCommandCompletion();
        readback->scene = scene.GetLifetimeToken();
        readback->gizmoActorGUID = debugSceneDescriptor.selectedActor ? debugSceneDescriptor.selectedActor->GetGUID() : 0;
        readback->gizmoOperation = debugSceneDescriptor.gizmoOperation;
        readback->width = frameDescriptor.renderWidth;
        readback->height = frameDescriptor.renderHeight;
        readback->pixels.resize(size_t(readback->width) * readback->height * 4);
        for (const auto* actor : scene.GetActors())
            readback->actors.emplace(static_cast<uint32_t>(actor->GetID()), actor->GetGUID());
        const auto target = m_actorPickingFramebuffer.RecordTarget();
        m_renderer.SubmitRenderOperation([readback, target](auto&) { target.ReadRGBA8(readback->pixels); });
        m_readback = std::move(readback);
    }

	m_renderer.BindTarget(nullptr);

	if (auto output = frameDescriptor.outputBuffer)
	{
		m_renderer.BindTarget(&output.value());
	}
}

void OvEditor::Rendering::PickingRenderPass::DrawPickableModels(
	OvRendering::Data::PipelineState p_pso,
	OvCore::SceneSystem::Scene& p_scene
)
{
	const auto& filteredDrawables = m_renderer.GetDescriptor<OvCore::Rendering::SceneRenderer::SceneFilteredDrawablesDescriptor>();

	auto drawPickableModels = [&](auto drawables) {
		for (auto& drawable : drawables)
		{
			const auto& actor = drawable.template GetDescriptor<OvCore::Rendering::SceneRenderer::SceneDrawableDescriptor>().actor;
			const auto skinnedRenderer = actor.template GetComponent<OvCore::ECS::Components::CSkinnedMeshRenderer>();
			const bool hasSkinningDescriptor = drawable.template HasDescriptor<OvCore::Rendering::SkinningDrawableDescriptor>();
			const bool skinningEnabled = hasSkinningDescriptor &&
				skinnedRenderer &&
				m_actorPickingFallbackMaterial.SupportsFeature(kSkinningFeatureName);

			if (skinningEnabled)
			{
				auto& targetMaterial = m_actorPickingFallbackMaterial;

				PreparePickingMaterial(actor, targetMaterial);

				OvRendering::Entities::Drawable finalDrawable = drawable;
				finalDrawable.material = &targetMaterial;
				finalDrawable.stateMask = targetMaterial.GenerateStateMask();
				finalDrawable.stateMask.frontfaceCulling = false;
				finalDrawable.stateMask.backfaceCulling = false;
				finalDrawable.pass = kPickingPassName;

				OvCore::Rendering::SkinningUtils::ApplyToDrawable(finalDrawable, *skinnedRenderer, &targetMaterial.GetFeatures());
				m_renderer.DrawEntity(p_pso, finalDrawable);
				continue;
			}

			auto& targetMaterial =
				drawable.material &&
				drawable.material->IsValid() &&
				drawable.material->HasPass(kPickingPassName) ?
				drawable.material.value() :
				m_actorPickingFallbackMaterial;

			PreparePickingMaterial(actor, targetMaterial);

			OvRendering::Entities::Drawable finalDrawable = drawable;
			finalDrawable.material = &targetMaterial;
			finalDrawable.stateMask = targetMaterial.GenerateStateMask();
			finalDrawable.stateMask.frontfaceCulling = false;
			finalDrawable.stateMask.backfaceCulling = false;
			finalDrawable.pass = kPickingPassName;
			finalDrawable.featureSetOverride = std::nullopt;

			m_renderer.DrawEntity(p_pso, finalDrawable);
		}
	};

	drawPickableModels(filteredDrawables.opaques | std::views::values);
	drawPickableModels(filteredDrawables.transparents | std::views::values);
	drawPickableModels(filteredDrawables.ui | std::views::values);
}

void OvEditor::Rendering::PickingRenderPass::DrawPickableCameras(
	OvRendering::Data::PipelineState p_pso,
	OvCore::SceneSystem::Scene& p_scene
)
{
	for (auto camera : p_scene.GetFastAccessComponents().cameras)
	{
		auto& actor = camera->owner;

		if (actor.IsActive())
		{
			PreparePickingMaterial(actor, m_actorPickingFallbackMaterial);
			auto& cameraModel = *EDITOR_CONTEXT(editorResources)->GetModel("Camera");
			auto translation = OvMaths::FMatrix4::Translation(actor.transform.GetWorldPosition());
			auto rotation = OvMaths::FQuaternion::ToMatrix4(actor.transform.GetWorldRotation());
			auto modelMatrix = translation * rotation;

			m_renderer.GetFeature<DebugModelRenderFeature>()
				.DrawModelWithSingleMaterial(p_pso, cameraModel, m_actorPickingFallbackMaterial, modelMatrix);
		}
	}
}

void OvEditor::Rendering::PickingRenderPass::DrawPickableReflectionProbes(OvRendering::Data::PipelineState p_pso, OvCore::SceneSystem::Scene& p_scene)
{
	for (auto reflectionProbe : p_scene.GetFastAccessComponents().reflectionProbes)
	{
		auto& actor = reflectionProbe->owner;

		if (actor.IsActive())
		{
			PreparePickingMaterial(actor, m_reflectionProbeMaterial);
			auto& reflectionProbeModel = *EDITOR_CONTEXT(editorResources)->GetModel("Sphere");
			const auto translation = OvMaths::FMatrix4::Translation(
				actor.transform.GetWorldPosition() +
				reflectionProbe->GetCapturePosition()
			);
			const auto rotation = OvMaths::FQuaternion::ToMatrix4(actor.transform.GetWorldRotation());
			const auto scaling = OvMaths::FMatrix4::Scaling(
				OvMaths::FVector3::One * OvEditor::Settings::EditorSettings::ReflectionProbeScale
			);
			auto modelMatrix = translation * rotation * scaling;

			m_renderer.GetFeature<DebugModelRenderFeature>()
				.DrawModelWithSingleMaterial(p_pso, reflectionProbeModel, m_reflectionProbeMaterial, modelMatrix);
		}
	}
}

void OvEditor::Rendering::PickingRenderPass::DrawPickableLights(
	OvRendering::Data::PipelineState p_pso,
	OvCore::SceneSystem::Scene& p_scene
)
{
	if (Settings::EditorSettings::LightBillboardScale > 0.001f)
	{
		m_renderer.Clear(false, true, false);

		m_lightMaterial.SetProperty("u_Scale", Settings::EditorSettings::LightBillboardScale * 0.1f);

		for (auto light : p_scene.GetFastAccessComponents().lights)
		{
			auto& actor = light->owner;

			if (actor.IsActive())
			{
				PreparePickingMaterial(actor, m_lightMaterial, "u_Diffuse");
				auto& lightModel = *EDITOR_CONTEXT(editorResources)->GetModel("Vertical_Plane");
				auto modelMatrix = OvMaths::FMatrix4::Translation(actor.transform.GetWorldPosition());

				m_renderer.GetFeature<DebugModelRenderFeature>()
					.DrawModelWithSingleMaterial(p_pso, lightModel, m_lightMaterial, modelMatrix);
			}
		}
	}
}

void OvEditor::Rendering::PickingRenderPass::DrawPickableGizmo(
	OvRendering::Data::PipelineState p_pso,
	const OvMaths::FVector3& p_position,
	const OvMaths::FQuaternion& p_rotation,
	OvEditor::Core::EGizmoOperation p_operation
)
{
	auto modelMatrix =
		OvMaths::FMatrix4::Translation(p_position) *
		OvMaths::FQuaternion::ToMatrix4(OvMaths::FQuaternion::Normalize(p_rotation));

	auto arrowModel = EDITOR_CONTEXT(editorResources)->GetModel("Arrow_Picking");

	m_renderer.GetFeature<DebugModelRenderFeature>()
		.DrawModelWithSingleMaterial(p_pso, *arrowModel, m_gizmoPickingMaterial, modelMatrix);
}
