#include <OvEditor/Rendering/PickingCollector.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Settings/EditorSettings.h>
#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/ECS/Components/CLight.h>
#include <OvCore/ECS/Components/CReflectionProbe.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <OvCore/Rendering/SkinningUtils.h>
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


OvEditor::Rendering::PickingCollector::PickingCollector() :
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


void OvEditor::Rendering::PickingCollector::DrawPickableCameras(
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

			m_collector
				.CollectModel(p_pso, cameraModel, m_actorPickingFallbackMaterial, modelMatrix);
		}
	}
}

void OvEditor::Rendering::PickingCollector::DrawPickableReflectionProbes(OvRendering::Data::PipelineState p_pso, OvCore::SceneSystem::Scene& p_scene)
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

			m_collector
				.CollectModel(p_pso, reflectionProbeModel, m_reflectionProbeMaterial, modelMatrix);
		}
	}
}

void OvEditor::Rendering::PickingCollector::DrawPickableLights(
	OvRendering::Data::PipelineState p_pso,
	OvCore::SceneSystem::Scene& p_scene
)
{
	if (Settings::EditorSettings::LightBillboardScale > 0.001f)
	{
		m_collector.Clear(false, true, false);

		m_lightMaterial.SetProperty("u_Scale", Settings::EditorSettings::LightBillboardScale * 0.1f);

		for (auto light : p_scene.GetFastAccessComponents().lights)
		{
			auto& actor = light->owner;

			if (actor.IsActive())
			{
				PreparePickingMaterial(actor, m_lightMaterial, "u_Diffuse");
				auto& lightModel = *EDITOR_CONTEXT(editorResources)->GetModel("Vertical_Plane");
				auto modelMatrix = OvMaths::FMatrix4::Translation(actor.transform.GetWorldPosition());

				m_collector
					.CollectModel(p_pso, lightModel, m_lightMaterial, modelMatrix);
			}
		}
	}
}

void OvEditor::Rendering::PickingCollector::DrawPickableGizmo(
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

	m_collector
		.CollectModel(p_pso, *arrowModel, m_gizmoPickingMaterial, modelMatrix);
}

OvRendering::Data::RenderCommandList OvEditor::Rendering::PickingCollector::Extract(
    OvCore::Rendering::SceneRenderRequest request,const EditorOverlayRequest& overlay,
    OvRendering::Data::PipelineState state,OvTools::Jobs::JobSystem* jobs)
{
    m_readback.reset(); m_info={};
    if(overlay.scene!=request.scene.GetLifetimeToken())throw std::invalid_argument("Picking request belongs to another scene");
    request.pipeline.stages={OvRendering::Data::RenderStage::Picking};request.pickingFallback=&m_actorPickingFallbackMaterial;
    m_actorPickingFramebuffer.Resize(request.width,request.height);
    auto input=m_frontend.ExtractScene(request);
    auto frame=m_frontend.Prepare(input,state,jobs);
    for(const auto& draw:frame->GetDraws()) {const auto& mesh=frame->GetScene().GetMeshes()[draw.sourceIndex];m_info.Add(mesh.geometry->GetVertexCount(),mesh.geometry->GetIndexCount(),mesh.instances);}
    OvRendering::Data::RenderCommandList commands;
    commands.Append(m_frontend.Record(frame,m_actorPickingFramebuffer.RecordTarget(),jobs));
    m_collector.Begin(frame->GetView(),state,m_actorPickingFramebuffer.RecordTarget());
    DrawPickableCameras(state,request.scene);DrawPickableReflectionProbes(state,request.scene);DrawPickableLights(state,request.scene);
    m_collector.Clear(false,true,false);
    auto* selected=request.scene.FindActorByGUID(overlay.selected);
    if(selected)DrawPickableGizmo(state,selected->transform.GetWorldPosition(),selected->transform.GetWorldRotation(),overlay.operation);
    auto readback=std::make_shared<PickingReadback>();readback->completion=commands.GetCompletion();
    readback->scene=request.scene.GetLifetimeToken();readback->width=request.width;readback->height=request.height;
    readback->gizmoActorGUID=selected?selected->GetGUID():0;readback->gizmoOperation=overlay.operation;
    readback->pixels.resize(size_t(request.width)*request.height*4);
    for(const auto* actor:request.scene.GetActors())readback->actors.emplace(static_cast<uint32_t>(actor->GetID()),actor->GetGUID());
    m_collector.SubmitRenderOperation([readback,target=m_actorPickingFramebuffer.RecordTarget()](auto&){target.ReadRGBA8(readback->pixels);});
    m_info+=m_collector.GetFrameInfo();commands.AppendRecording(m_collector.Finish());m_readback=std::move(readback);return commands;
}
