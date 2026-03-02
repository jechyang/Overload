/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include "OvEditor/Rendering/DebugModelRenderFeature.h"
#include "OvCore/Rendering/EngineDrawableDescriptor.h"
#include "OvCore/Rendering/SceneRenderer.h"
#include "OvRendering/Core/CompositeRenderer.h"

OvEditor::Rendering::DebugModelRenderFeature::DebugModelRenderFeature(
	OvRendering::Core::CompositeRenderer& p_renderer,
	OvRendering::Features::EFeatureExecutionPolicy p_executionPolicy
) :
	ARenderFeature(p_renderer, p_executionPolicy)
{
}

void OvEditor::Rendering::DebugModelRenderFeature::DrawModelWithSingleMaterial(
	OvRendering::Data::PipelineState p_pso,
	OvRendering::Resources::Model& p_model,
	OvRendering::Data::Material& p_material,
	const OvMaths::FMatrix4& p_modelMatrix
)
{
	auto stateMask = p_material.GenerateStateMask();
	auto userMatrix = OvMaths::FMatrix4::Identity;
	auto engineDrawableDescriptor = OvCore::Rendering::EngineDrawableDescriptor{
		p_modelMatrix,
		userMatrix
	};

	// Get engine UBO from SceneRenderer
	auto* sceneRenderer = dynamic_cast<OvCore::Rendering::SceneRenderer*>(&m_renderer);
	if (!sceneRenderer)
	{
		// Fallback: just draw without UBO upload (should not happen in editor)
		for (auto mesh : p_model.GetMeshes())
		{
			OvRendering::Entities::Drawable element;
			element.mesh = *mesh;
			element.material = p_material;
			element.stateMask = stateMask;
			element.AddDescriptor(engineDrawableDescriptor);
			m_renderer.DrawEntity(p_pso, element);
		}
		return;
	}

	auto& engineUBO = sceneRenderer->GetEngineBuffer();

	for (auto mesh : p_model.GetMeshes())
	{
		OvRendering::Entities::Drawable element;
		element.mesh = *mesh;
		element.material = p_material;
		element.stateMask = stateMask;
		element.AddDescriptor(engineDrawableDescriptor);

		// Use helper method to upload matrices and draw
		sceneRenderer->UploadMatricesAndDraw(p_pso, element, engineUBO, p_modelMatrix, userMatrix);
	}
}
