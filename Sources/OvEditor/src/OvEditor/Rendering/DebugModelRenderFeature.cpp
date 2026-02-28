/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include "OvEditor/Rendering/DebugModelRenderFeature.h"
#include "OvCore/Rendering/EngineDrawableDescriptor.h"
#include "OvCore/Rendering/SceneRenderer.h"
#include "OvRendering/Core/CompositeRenderer.h"

namespace
{
	constexpr size_t kUBOSize =
		sizeof(OvMaths::FMatrix4) +  // Model matrix
		sizeof(OvMaths::FMatrix4) +  // View matrix
		sizeof(OvMaths::FMatrix4) +  // Projection matrix
		sizeof(OvMaths::FVector3) +  // Camera position
		sizeof(float) +              // Elapsed time
		sizeof(OvMaths::FMatrix4);   // User matrix
}

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

	// Pre-compute transposed model matrix for UBO upload
	const auto transposedModelMatrix = OvMaths::FMatrix4::Transpose(p_modelMatrix);

	// Get engine UBO from SceneRenderer and upload matrices
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

		// Upload model/user matrices to engine UBO before draw
		engineUBO.Upload(&transposedModelMatrix, OvRendering::HAL::BufferMemoryRange{
			.offset = 0,
			.size = sizeof(transposedModelMatrix)
		});
		engineUBO.Upload(&userMatrix, OvRendering::HAL::BufferMemoryRange{
			.offset = kUBOSize - sizeof(transposedModelMatrix),
			.size = sizeof(userMatrix)
		});
		engineUBO.Bind(0);

		m_renderer.DrawEntity(p_pso, element);
	}
}
