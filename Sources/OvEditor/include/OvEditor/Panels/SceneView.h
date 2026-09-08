/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvEditor/Core/GizmoBehaviour.h>
#include <OvEditor/Panels/AViewControllable.h>
#include <OvEditor/Rendering/PickingRenderPass.h>
#include <OvTools/Eventing/Event.h>

namespace OvEditor::Panels
{
	class SceneView : public OvEditor::Panels::AViewControllable
	{
	public:
		/**
		* Constructor
		* @param p_title
		* @param p_opened
		* @param p_windowSettings
		*/
		SceneView(
			const std::string& p_title,
			bool p_opened,
			const OvUI::Settings::PanelWindowSettings& p_windowSettings
		);

		/**
		* Update the scene view
		*/
        struct Interaction
        {
            OvRendering::Data::RenderCommandList::Completion completion;
            std::shared_ptr<const Rendering::PickingRenderPass::Readback> readback;
            OvCore::SceneSystem::Scene::LifetimeToken scene;
            OvMaths::FVector2 mouse, size;
            OvMaths::FVector3 cameraPosition;
            OvMaths::FMatrix4 view, projection;
            uint64_t selectedGUID = 0;
            Core::EGizmoOperation operation = Core::EGizmoOperation::TRANSLATE;
            bool pickingAvailable = false, hovered = false, resizing = false, rightPressed = false, pressed = false, released = false;
            Core::GizmoBehaviour::SnapSettings snapping{};
            bool consumed = false;
            std::optional<std::string> materialDrop;
        };
        ~SceneView() override;
        OvRendering::Data::RenderCommandList RecordRenderCommands(bool p_deferEncoding = false) override;
        std::shared_ptr<Interaction> GetRecordedInteraction() const { return m_recordedInteraction; }
        bool CompleteInteraction(const std::shared_ptr<Interaction>& interaction);
		virtual void Update(float p_deltaTime) override;
		void CompleteRender() override;

		/**
		* Prepare the renderer for rendering
		*/
		virtual void InitFrame() override;

		/**
		* Returns the scene used by this view
		*/
		virtual OvCore::SceneSystem::Scene* GetScene() override;

		/**
		* Set the gizmo operation
		* @param p_operation
		*/
		void SetGizmoOperation(Core::EGizmoOperation p_operation);
	
		/**
		* Returns the current gizmo operation
		*/
		Core::EGizmoOperation GetGizmoOperation() const;

	protected:
		virtual OvCore::Rendering::SceneRenderer::SceneDescriptor CreateSceneDescriptor() override;

	private:
		virtual void DrawFrame() override;
        std::shared_ptr<Interaction> CaptureInteraction();
        void ApplyInteraction(const Interaction& input, Rendering::PickingRenderPass::PickingResult result);
        void ApplyMaterialDrop(const std::string& path, Rendering::PickingRenderPass::PickingResult result);
		void HandleActorPicking();
		OvEditor::Rendering::PickingRenderPass::PickingResult GetPickingResult();
		void OnSceneDropped(const std::string& p_path);
		void OnModelDropped(const std::string& p_path);
		void OnMaterialDropped(const std::string& p_path);
		void OnPrefabDropped(const std::string& p_path);

	private:
        std::shared_ptr<Interaction> m_recordedInteraction;
        uint64_t m_destroyedListener = 0, m_gizmoTargetGUID = 0;
		OvCore::SceneSystem::SceneManager& m_sceneManager;
		OvEditor::Core::GizmoBehaviour m_gizmoOperations;
		OvEditor::Core::EGizmoOperation m_currentOperation = OvEditor::Core::EGizmoOperation::TRANSLATE;
		OvCore::Resources::Material m_fallbackMaterial;

		OvTools::Utils::OptRef<OvCore::ECS::Actor> m_highlightedActor;
		std::optional<OvEditor::Core::GizmoBehaviour::EDirection> m_highlightedGizmoDirection;
	};
}
