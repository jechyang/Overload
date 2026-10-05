/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <memory>
#include <OvRendering/Data/RenderFrame.h>
#include <OvEditor/Panels/SceneView.h>
#include <optional>
#include <deque>

#include <OvEditor/Core/Context.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Core/PanelsManager.h>
#include <OvEditor/Panels/AView.h>
#include <OvEditor/Panels/ItemPicker.h>
#include <OvRendering/Entities/Camera.h>
#include <OvUI/Modules/Canvas.h>

namespace OvEditor::Core
{
	/**
	* Handle the editor logic
	*/
	class Editor
	{
	public:
		/**
		* Constructor of the editor
		* @param p_context
		*/
		Editor(Context& p_context);

		/**
		* Destructor of the editor
		*/
		~Editor();

		/**
		* Handle panels creation and canvas binding
		*/
		void SetupUI();

		/**
		* Prepare the frame (Inputs update, screen clearing)
		*/
		void PreUpdate();

		/**
		* Editor main loop.
		* Render the scene, update panels...
		* @param p_deltaTime
		*/
		void Update(float p_deltaTime);

		/**
		* Handle editor global shortcuts
		*/
		void HandleGlobalShortcuts();

		/**
		* Update the current editor mode
		* @param p_deltaTime
		*/
		void UpdateCurrentEditorMode(float p_deltaTime);

		/**
		* Apply the play mode logic
		* @param p_deltaTime
		*/
		void UpdatePlayMode(float p_deltaTime);

		/**
		* Apply the edit mode logic
		* @param p_deltaTime
		*/
		void UpdateEditMode(float p_deltaTime);

		/**
		* Update editor panels
		* @param p_deltaTime
		*/
		void UpdateEditorPanels(float p_deltaTime);

		// Manual frame handoff accepts the latest result. Product submissions retain
        // a separate interaction/completion entry for every in-flight frame.
		OvRendering::Data::RenderFrame RecordRenderFrame(float p_deltaTime);
		bool CompleteRenderFrame(const OvRendering::Data::RenderFrame::Completion& completion);
        void SetFrameSubmissionEnabled(bool enabled);
        void DrainRenderFrames();
        bool IsFrameSubmissionEnabled() const { return m_frameSubmissionEnabled; }

		/**
		* Actually render the scene (Buffer swapping)
		* and clear input events
		*/
		void PostUpdate();

	private:
        OvCore::Rendering::RenderFrontend m_productFrontend;
        void CollectCompletedFrames();
        struct InFlightFrame
        {
            OvRendering::Data::RenderFrame::Completion completion;
            std::shared_ptr<Panels::SceneView::Interaction> interaction;
        };
        std::deque<InFlightFrame> m_inFlightFrames;
		uint64_t m_elapsedFrames = 0;
		uint64_t m_recordedFrameID = 0;
        bool m_frameSubmissionEnabled = false;
        std::optional<OvRendering::Data::RenderFrame> m_pendingFrame;
        std::shared_ptr<Panels::SceneView::Interaction> m_recordedInteraction;
		OvRendering::Data::RenderFrame::Completion m_latestFrameCompletion;
		OvUI::Modules::Canvas m_canvas;
		OvEditor::Core::Context& m_context;
		OvEditor::Core::PanelsManager m_panelsManager;
		OvEditor::Core::EditorActions m_editorActions;
		OvTools::Utils::OptRef<Panels::AView> m_lastFocusedView;
		std::unique_ptr<OvEditor::Panels::ItemPicker> m_itemPicker;
	};
}
