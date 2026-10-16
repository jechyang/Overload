/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvEditor/Core/GizmoBehaviour.h>
#include <OvEditor/Panels/AViewControllable.h>
#include <OvEditor/Rendering/PickingReadback.h>
#include <OvTools/Eventing/Event.h>

namespace OvEditor::Panels
{
    class CameraPreview;
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
            std::shared_ptr<const Rendering::PickingReadback> readback;
            OvCore::SceneSystem::Scene::LifetimeToken scene;
            OvMaths::FVector2 mouse, size;
            OvMaths::FVector3 cameraPosition;
            OvMaths::FMatrix4 view, projection;
            uint64_t selectedGUID = 0;
            uint64_t navigationRevision = 0;
            std::vector<uint64_t> selection;
            Core::GizmoBehaviour::Frame gizmoFrame;
            Core::EGizmoOperation operation = Core::EGizmoOperation::TRANSLATE;
            bool pickingAvailable = false, hovered = false, resizing = false, rightPressed = false, pressed = false, released = false;
            Core::GizmoBehaviour::SnapSettings snapping{};
            bool consumed = false;
            bool cancelled = false;
            bool additiveSelection = false;
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

        void ToggleProjection();
        void Set2D(bool p_2d);
        bool Is2D() const;
        enum class DisplayMode { LIT, UNLIT, WIREFRAME };
        void SetDisplayMode(DisplayMode mode);
        DisplayMode GetDisplayMode() const;
        void SetViewDirection(const OvMaths::FVector3& direction);
        bool IsNavigationHovered() const;
        bool IsTransformEditing() const { return m_gizmoOperations.IsPicking(); }
        bool IsSelecting() const { return m_boxPending; }
        void FrameSelection();
        void SetLocalSpace(bool local);
        bool IsLocalSpace() const { return m_localSpace; }
        void SetCenterPivot(bool center);
        bool IsCenterPivot() const { return m_centerPivot; }
        Core::GizmoBehaviour::Frame GetGizmoFrame() const;
        AView* GetCameraPreviewView();
        bool IsCameraPreviewHovered() const;
        std::pair<OvMaths::FVector2,OvMaths::FVector2> GetCameraPreviewRect() const { return {m_previewPosition,m_previewSize}; }

	protected:
		OvCore::Rendering::SceneRenderRequest CreateSceneRequest() override;
        Rendering::EditorOverlayRequest CreateOverlayRequest() override;

	private:
        void DrawNavigationOverlay();
        void DrawViewportTools();
        void DrawCameraPreview();
        void UpdateCameraPreviewRect(std::pair<uint16_t,uint16_t> size);
        std::unique_ptr<CameraPreview> m_cameraPreview;
        OvMaths::FVector2 m_previewPosition{}, m_previewSize{};
        void InvalidateNavigationPicking();
        uint64_t m_navigationRevision = 0;
        bool m_navigationPopupOpen = false;
        bool m_toolsPopupOpen = false;
        bool m_localSpace = true, m_centerPivot = false;
        bool m_boxPending = false, m_boxAdditive = false;
        OvMaths::FVector2 m_boxStart{}, m_boxEnd{}, m_boxSize{};
        OvCore::SceneSystem::Scene::LifetimeToken m_boxScene;
        std::vector<uint64_t> m_boxSelection;
        float m_toolsWidth = 0, m_toolsHeight = 0;
		void OnViewRecorded() override;
        std::shared_ptr<Interaction> CaptureInteraction();
        void ApplyInteraction(const Interaction& input, Rendering::PickingResult result);
        void ApplyMaterialDrop(const std::string& path, Rendering::PickingResult result);
		void HandleActorPicking();
		OvEditor::Rendering::PickingResult GetPickingResult();
		void OnSceneDropped(const std::string& p_path);
		void OnModelDropped(const std::string& p_path);
		void OnMaterialDropped(const std::string& p_path);
		void OnPrefabDropped(const std::string& p_path);

	private:
        std::shared_ptr<Interaction> m_recordedInteraction;
        uint64_t m_destroyedListener = 0, m_gizmoTargetGUID = 0;
        uint64_t m_focusLostListener = 0;
		OvCore::SceneSystem::SceneManager& m_sceneManager;
		OvEditor::Core::GizmoBehaviour m_gizmoOperations;
		OvEditor::Core::EGizmoOperation m_currentOperation = OvEditor::Core::EGizmoOperation::TRANSLATE;
		OvCore::Resources::Material m_fallbackMaterial;
        OvCore::Resources::Material m_displayFallbackMaterial;

		OvTools::Utils::OptRef<OvCore::ECS::Actor> m_highlightedActor;
		std::optional<OvEditor::Core::GizmoBehaviour::EDirection> m_highlightedGizmoDirection;
	};
}
