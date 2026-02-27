/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <memory>

#include <OvRendering/Core/ARenderPass.h>
#include <OvRendering/Entities/Camera.h>
#include <OvRendering/Features/DebugShapeRenderFeature.h>
#include <OvRendering/Features/FrameInfoRenderFeature.h>

#include <OvCore/ECS/Actor.h>
#include <OvCore/SceneSystem/SceneManager.h>
#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/Resources/Material.h>
#include <OvCore/ECS/Components/CAmbientBoxLight.h>
#include <OvCore/ECS/Components/CAmbientSphereLight.h>
#include <OvCore/Rendering/SceneRenderer.h>

#include "OvEditor/Core/GizmoBehaviour.h"
#include "OvEditor/Core/Context.h"
#include "OvEditor/Rendering/DebugModelRenderFeature.h"
#include "OvEditor/Rendering/GizmoRenderFeature.h"
#include "OvEditor/Rendering/OutlineRenderFeature.h"
#include "OvEditor/Rendering/PickingRenderPass.h"

namespace OvEditor::Panels { class AView; }

namespace OvEditor::Rendering
{
	/**
	* Provide a debug layer on top of the default scene renderer to see "invisible" entities such as
	* lights, cameras,
	*/
	class DebugSceneRenderer : public OvCore::Rendering::SceneRenderer
	{
	public:
		struct DebugSceneDescriptor
		{
			OvEditor::Core::EGizmoOperation gizmoOperation;
			OvTools::Utils::OptRef<OvCore::ECS::Actor> highlightedActor;
			OvTools::Utils::OptRef<OvCore::ECS::Actor> selectedActor;
			std::optional<OvEditor::Core::GizmoBehaviour::EDirection> highlightedGizmoDirection;
		};

		/**
		* Constructor of the Renderer
		* @param p_driver
		*/
		DebugSceneRenderer(OvRendering::Context::Driver& p_driver);

		/**
		* Returns the frame info from the last rendered frame
		*/
		const OvRendering::Data::FrameInfo& GetFrameInfo() const;

		/**
		* Returns the picking render pass
		*/
		PickingRenderPass& GetPickingPass();

		virtual void EndFrame() override;

	protected:
		virtual void BuildFrameGraph(OvRendering::FrameGraph::FrameGraph& p_fg) override;

	private:
		// Features stored as direct members (not registered with renderer)
		std::unique_ptr<OvRendering::Features::FrameInfoRenderFeature> m_frameInfoFeature;
		std::unique_ptr<OvRendering::Features::DebugShapeRenderFeature> m_debugShapeFeature;
		std::unique_ptr<DebugModelRenderFeature> m_debugModelFeature;
		std::unique_ptr<OutlineRenderFeature> m_outlineFeature;
		std::unique_ptr<GizmoRenderFeature> m_gizmoFeature;

		// Debug passes stored as base-class pointers
		std::unique_ptr<OvRendering::Core::ARenderPass> m_gridPass;
		std::unique_ptr<OvRendering::Core::ARenderPass> m_debugCamerasPass;
		std::unique_ptr<OvRendering::Core::ARenderPass> m_debugReflectionProbesPass;
		std::unique_ptr<OvRendering::Core::ARenderPass> m_debugLightsPass;
		std::unique_ptr<OvRendering::Core::ARenderPass> m_debugActorPass;
		std::unique_ptr<PickingRenderPass> m_pickingPass;
	};
}
