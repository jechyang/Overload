/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/


#include <OvCore/ECS/Components/CAmbientSphereLight.h>
#include <OvCore/ECS/Components/CDirectionalLight.h>
#include <OvCore/ECS/Components/CReflectionProbe.h>
#include <OvCore/Rendering/SceneRenderer.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Panels/AssetView.h>
#include <OvEditor/Rendering/DebugModelRenderFeature.h>
#include <OvRendering/Features/DebugShapeRenderFeature.h>
#include <OvRendering/Features/FrameInfoRenderFeature.h>
#include <OvRendering/FrameGraph/FrameGraph.h>
#include <OvRendering/FrameGraph/FrameGraphBuilder.h>
#include <OvRendering/FrameGraph/FrameGraphResources.h>
#include <tracy/Tracy.hpp>
#include <OvTools/Utils/PathParser.h>
#include <OvUI/Plugins/DDTarget.h>

namespace
{
	class AssetViewRenderer : public OvCore::Rendering::SceneRenderer
	{
	public:
		AssetViewRenderer(OvRendering::Context::Driver& p_driver)
			: OvCore::Rendering::SceneRenderer(p_driver)
		{
			using namespace OvRendering::Features;
			using enum EFeatureExecutionPolicy;

			m_debugModelFeature = std::make_unique<OvEditor::Rendering::DebugModelRenderFeature>(*this, NEVER);
			m_debugShapeFeature = std::make_unique<DebugShapeRenderFeature>(*this, NEVER);
			m_frameInfoFeature  = std::make_unique<FrameInfoRenderFeature>(*this, ALWAYS);
		}

		const OvRendering::Data::FrameInfo& GetFrameInfo() const
		{
			return m_frameInfoFeature->GetFrameInfo();
		}

		void EndFrame() override
		{
			m_frameInfoFeature->OnEndFrame();
			OvCore::Rendering::SceneRenderer::EndFrame();
		}

	protected:
		void BuildFrameGraph(OvRendering::FrameGraph::FrameGraph& p_fg) override
		{
			m_frameInfoFeature->OnBeginFrame(m_frameDescriptor);
			SceneRenderer::BuildFrameGraph(p_fg);

			// Grid Pass - inlined from GridRenderPass
			struct GridPassData
			{
				OvCore::Resources::Material gridMaterial;
			};

			p_fg.AddPass<GridPassData>("Grid",
				[this](OvRendering::FrameGraph::FrameGraphBuilder& builder, GridPassData& data) {
					// Initialize grid material
					data.gridMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("Grid"));
					data.gridMaterial.SetBlendable(true);
					data.gridMaterial.SetBackfaceCulling(false);
					data.gridMaterial.SetDepthWriting(false);
					data.gridMaterial.SetDepthTest(true);
					builder.SetAsOutput({});
				},
				[this](const OvRendering::FrameGraph::FrameGraphResources&, GridPassData& data) {
					ZoneScoped;

					auto& frameDescriptor = m_frameDescriptor;
					auto pso = CreatePipelineState();

					// Set up camera matrices
					auto viewPos = frameDescriptor.camera->transform->GetWorldPosition();
					auto transposedView = OvMaths::FMatrix4::Transpose(frameDescriptor.camera->GetViewMatrix());
					auto proj = frameDescriptor.camera->GetProjectionMatrix();

					// Upload camera matrices to engine UBO
					auto& engineUBO = GetEngineBuffer();
					engineUBO.Upload(&transposedView, OvRendering::HAL::BufferMemoryRange{ .offset = sizeof(OvMaths::FMatrix4), .size = sizeof(OvMaths::FMatrix4) });
					engineUBO.Upload(&proj, OvRendering::HAL::BufferMemoryRange{ .offset = sizeof(OvMaths::FMatrix4) * 2, .size = sizeof(OvMaths::FMatrix4) });
					engineUBO.Bind(0);

					constexpr float gridSize = 5000.0f;
					OvMaths::FMatrix4 model =
						OvMaths::FMatrix4::Translation({ viewPos.x, 0.0f, viewPos.z }) *
						OvMaths::FMatrix4::Scaling({ gridSize * 2.0f, 1.f, gridSize * 2.0f });

					data.gridMaterial.SetProperty("u_Color", OvMaths::FVector3{ 0.0f, 0.447f, 1.0f });

					m_debugModelFeature->DrawModelWithSingleMaterial(
						pso,
						*EDITOR_CONTEXT(editorResources)->GetModel("Plane"),
						data.gridMaterial,
						model
					);

					constexpr float kLineWidth = 1.0f;
					m_debugShapeFeature->DrawLine(pso, OvMaths::FVector3(-gridSize + viewPos.x, 0.0f, 0.0f), OvMaths::FVector3(gridSize + viewPos.x, 0.0f, 0.0f), OvMaths::FVector3::Right, kLineWidth);
					m_debugShapeFeature->DrawLine(pso, OvMaths::FVector3(0.0f, -gridSize + viewPos.y, 0.0f), OvMaths::FVector3(0.0f, gridSize + viewPos.y, 0.0f), OvMaths::FVector3::Up, kLineWidth);
					m_debugShapeFeature->DrawLine(pso, OvMaths::FVector3(0.0f, 0.0f, -gridSize + viewPos.z), OvMaths::FVector3(0.0f, 0.0f, gridSize + viewPos.z), OvMaths::FVector3::Forward, kLineWidth);
				}
			);
		}

	private:
		std::unique_ptr<OvEditor::Rendering::DebugModelRenderFeature> m_debugModelFeature;
		std::unique_ptr<OvRendering::Features::DebugShapeRenderFeature> m_debugShapeFeature;
		std::unique_ptr<OvRendering::Features::FrameInfoRenderFeature> m_frameInfoFeature;
	};
}

OvEditor::Panels::AssetView::AssetView
(
	const std::string& p_title,
	bool p_opened,
	const OvUI::Settings::PanelWindowSettings& p_windowSettings
) : AViewControllable(p_title, p_opened, p_windowSettings)
{
	m_renderer = std::make_unique<AssetViewRenderer>(*EDITOR_CONTEXT(driver));

	m_camera.SetFar(5000.0f);

	m_scene.AddDefaultLights();
	m_scene.AddDefaultPostProcessStack();
	m_scene.AddDefaultAtmosphere();
	m_scene.AddDefaultReflections();

	m_assetActor = &m_scene.CreateActor("Asset");
	m_modelRenderer = &m_assetActor->AddComponent<OvCore::ECS::Components::CModelRenderer>();
	m_modelRenderer->SetFrustumBehaviour(OvCore::ECS::Components::CModelRenderer::EFrustumBehaviour::DISABLED);
	m_materialRenderer = &m_assetActor->AddComponent<OvCore::ECS::Components::CMaterialRenderer>();
	m_materialRenderer->SetVisibilityFlags(OvCore::Rendering::EVisibilityFlags::GEOMETRY | OvCore::Rendering::EVisibilityFlags::SHADOW);

	m_cameraController.LockTargetActor(*m_assetActor);
	
	/* Default Material */
	m_defaultMaterial.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\Standard.ovfx"]);
	m_defaultMaterial.SetProperty("u_Metallic", 0.0f);
	m_defaultMaterial.SetProperty("u_Roughness", 0.5f);

	/* Texture Material */
	m_textureMaterial.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\Unlit.ovfx"]);
	m_textureMaterial.SetProperty("u_Diffuse", OvMaths::FVector4(1.f, 1.f, 1.f, 1.f));
	m_textureMaterial.SetBackfaceCulling(false);
	m_textureMaterial.SetBlendable(true);
	m_textureMaterial.SetProperty("u_DiffuseMap", static_cast<OvRendering::Resources::Texture*>(nullptr));

	m_image->AddPlugin<OvUI::Plugins::DDTarget<std::pair<std::string, OvUI::Widgets::Layout::Group*>>>("File").DataReceivedEvent += [this](auto p_data)
	{
		std::string path = p_data.first;

		switch (OvTools::Utils::PathParser::GetFileType(path))
		{
		case OvTools::Utils::PathParser::EFileType::MODEL:
			if (auto resource = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::ModelManager>().GetResource(path); resource)
				SetModel(*resource);
			break;
		case OvTools::Utils::PathParser::EFileType::TEXTURE:
			if (auto resource = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::TextureManager>().GetResource(path); resource)
				SetTexture(*resource);
			break;

		case OvTools::Utils::PathParser::EFileType::MATERIAL:
			if (auto resource = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::MaterialManager>().GetResource(path); resource)
				SetMaterial(*resource);
			break;

		default:
			break;
		}
	};
}

OvCore::SceneSystem::Scene* OvEditor::Panels::AssetView::GetScene()
{
	return &m_scene;
}

void OvEditor::Panels::AssetView::SetResource(ViewableResource p_resource)
{
	if (auto pval = std::get_if<OvRendering::Resources::Model*>(&p_resource); pval && *pval)
	{
		SetModel(**pval);
	}
	else if (auto pval = std::get_if<OvRendering::Resources::Texture*>(&p_resource); pval && *pval)
	{
		SetTexture(**pval);
	}
	else if (auto pval = std::get_if<OvCore::Resources::Material*>(&p_resource); pval && *pval)
	{
		SetMaterial(**pval);
	}
}

void OvEditor::Panels::AssetView::ClearResource()
{
	m_resource = static_cast<OvRendering::Resources::Texture*>(nullptr);
	m_modelRenderer->SetModel(nullptr);
}

void OvEditor::Panels::AssetView::SetTexture(OvRendering::Resources::Texture& p_texture)
{
	m_resource = &p_texture;
	m_assetActor->transform.SetLocalRotation(OvMaths::FQuaternion({ -90.0f, 0.0f, 0.0f }));
	m_assetActor->transform.SetLocalScale(OvMaths::FVector3::One * 3.0f);
	m_modelRenderer->SetModel(EDITOR_CONTEXT(editorResources)->GetModel("Plane"));
	m_textureMaterial.SetProperty("u_DiffuseMap", &p_texture);
	m_materialRenderer->FillWithMaterial(m_textureMaterial);

	m_cameraController.MoveToTarget(*m_assetActor);
}

void OvEditor::Panels::AssetView::SetModel(OvRendering::Resources::Model& p_model)
{
	m_resource = &p_model;
	m_assetActor->transform.SetLocalRotation(OvMaths::FQuaternion::Identity);
	m_assetActor->transform.SetLocalScale(OvMaths::FVector3::One);
	m_modelRenderer->SetModel(&p_model);
	m_materialRenderer->FillWithMaterial(m_defaultMaterial);

	m_cameraController.MoveToTarget(*m_assetActor);
}

void OvEditor::Panels::AssetView::SetMaterial(OvCore::Resources::Material& p_material)
{
	m_resource = &p_material;
	m_assetActor->transform.SetLocalRotation(OvMaths::FQuaternion::Identity);
	m_assetActor->transform.SetLocalScale(OvMaths::FVector3::One);
	m_modelRenderer->SetModel(EDITOR_CONTEXT(editorResources)->GetModel("Sphere"));
	m_materialRenderer->FillWithMaterial(p_material);

	m_cameraController.MoveToTarget(*m_assetActor);
}

const OvEditor::Panels::AssetView::ViewableResource& OvEditor::Panels::AssetView::GetResource() const
{
	return m_resource;
}

const OvRendering::Data::FrameInfo& OvEditor::Panels::AssetView::GetFrameInfo() const
{
	return static_cast<const AssetViewRenderer&>(*m_renderer).GetFrameInfo();
}
