/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvCore/ECS/Actor.h>
#include <OvCore/ECS/Components/CAmbientBoxLight.h>
#include <OvCore/ECS/Components/CAmbientSphereLight.h>
#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/Resources/Material.h>
#include <OvCore/Rendering/SceneRenderer.h>
#include <OvCore/SceneSystem/SceneManager.h>

#include <OvEditor/Core/Context.h>
#include <OvEditor/Core/GizmoBehaviour.h>

#include <OvRendering/Entities/Camera.h>
#include <OvRendering/Features/DebugShapeRenderFeature.h>

namespace OvEditor::Rendering
{
	/**
	* Draw the scene for actor picking
	*/
	class PickingRenderPass : public OvRendering::Core::ARenderPass
	{
	public:
		bool SupportsCommandRecording() const override { return typeid(*this) == typeid(PickingRenderPass); }
		using PickingResult =
			std::optional<
			std::variant<OvTools::Utils::OptRef<OvCore::ECS::Actor>,
			OvEditor::Core::GizmoBehaviour::EDirection>
		>;

        class Readback
        {
            friend class PickingRenderPass;
        public:
            bool IsReady() const { return completion.Succeeded(); }
            std::pair<uint32_t, uint32_t> GetSize() const { return {width, height}; }
            uint64_t GetGizmoActorGUID() const { return gizmoActorGUID; }
            OvEditor::Core::EGizmoOperation GetGizmoOperation() const { return gizmoOperation; }
            PickingResult Resolve(const OvCore::SceneSystem::Scene& p_scene, uint32_t p_x, uint32_t p_y) const;
        private:
            OvRendering::Data::RenderCommandList::Completion completion;
            OvCore::SceneSystem::Scene::LifetimeToken scene;
            uint32_t width = 0, height = 0;
            uint64_t gizmoActorGUID = 0;
            OvEditor::Core::EGizmoOperation gizmoOperation = OvEditor::Core::EGizmoOperation::TRANSLATE;
            std::vector<uint8_t> pixels;
            std::unordered_map<uint32_t, uint64_t> actors;
        };
        std::shared_ptr<const Readback> GetRecordedReadback() const { return m_readback; }

		/**
		* Constructor
		* @param p_renderer
		*/
		PickingRenderPass(OvRendering::Core::CompositeRenderer& p_renderer);

		/**
		* Return the picking result at the given position
		* @param p_scene
		* @param p_x
		* @param p_y
		*/
		PickingResult ReadbackPickingResult(
			const OvCore::SceneSystem::Scene& p_scene,
			uint32_t p_x,
			uint32_t p_y
		);

	private:
		virtual void Draw(OvRendering::Data::PipelineState p_pso) override;
		void DrawPickableModels(OvRendering::Data::PipelineState p_pso, OvCore::SceneSystem::Scene& p_scene);
		void DrawPickableCameras(OvRendering::Data::PipelineState p_pso, OvCore::SceneSystem::Scene& p_scene);
		void DrawPickableReflectionProbes(OvRendering::Data::PipelineState p_pso, OvCore::SceneSystem::Scene& p_scene);
		void DrawPickableLights(OvRendering::Data::PipelineState p_pso, OvCore::SceneSystem::Scene& p_scene);
		void DrawPickableGizmo(
			OvRendering::Data::PipelineState p_pso,
			const OvMaths::FVector3& p_position,
			const OvMaths::FQuaternion& p_rotation,
			OvEditor::Core::EGizmoOperation p_operation
		);

	private:
		std::shared_ptr<Readback> m_readback;
		baregl::Framebuffer m_actorPickingFramebuffer;
		OvCore::Resources::Material m_actorPickingFallbackMaterial;
		OvCore::Resources::Material m_reflectionProbeMaterial;
		OvCore::Resources::Material m_lightMaterial;
		OvCore::Resources::Material m_gizmoPickingMaterial;
	};
}
