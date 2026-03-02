/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <memory>

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

		// Grid descriptor for grid pass
		struct GridDescriptor
		{
			OvMaths::FVector3 gridColor;
			OvMaths::FVector3 viewPosition;
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
		* Return the picking result at the given position
		* @param p_scene
		* @param p_x
		* @param p_y
		*/
		using PickingResult = std::optional<std::variant<OvTools::Utils::OptRef<OvCore::ECS::Actor>, OvEditor::Core::GizmoBehaviour::EDirection>>;
		PickingResult ReadbackPickingResult(const OvCore::SceneSystem::Scene& p_scene, uint32_t p_x, uint32_t p_y);

		/**
		* Enable or disable the picking pass
		* @param p_enabled
		*/
		void SetPickingEnabled(bool p_enabled);

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

		// Picking resources (migrated from PickingRenderPass)
		OvRendering::HAL::Framebuffer m_pickingFramebuffer;
		OvCore::Resources::Material m_pickingFallbackMaterial;
		OvCore::Resources::Material m_reflectionProbeMaterial;
		OvCore::Resources::Material m_lightMaterial;
		OvCore::Resources::Material m_gizmoPickingMaterial;
		bool m_pickingEnabled = true;
	};
}
