/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/Rendering/RenderFrontend.h>
#include <OvRendering/Data/FrameInfo.h>

#include <OvGame/Core/Context.h>

#ifdef _DEBUG
#include <OvGame/Debug/DriverInfo.h>
#include <OvGame/Debug/FrameInfo.h>
#endif

#include <OvGame/Utils/FPSCounter.h>

#include <OvUI/Modules/Canvas.h>
#include <OvRendering/Data/RenderFrame.h>
#include <optional>
#include <deque>

namespace OvGame::Core
{
	/**
	* Handle the game logic
	*/
	class Game
	{
	public:
		/**
		* Create the game
		* @param p_context
		*/
		Game(Context& p_context);

		/**
		* Destroy the game
		*/
		~Game();

		/**
		* Pre-update of the game logic
		*/
		void PreUpdate();

		/**
		* Update the game logic
		* @param p_deltaTime
		*/
		void Update(float p_deltaTime);

		/**
		* Post-update of the game logic
		*/
		void PostUpdate();

		// Capture already-updated scene/debug UI; the GL owner submits and presents.
		OvRendering::Data::RenderFrame RecordRenderFrame();
        void SetFrameSubmissionEnabled(bool enabled);
        bool IsFrameSubmissionEnabled() const { return m_frameSubmissionEnabled; }
        void DrainRenderFrames();

	private:
        OvRendering::Data::RenderCommandList RecordSceneCommands();
        void CollectCompletedFrames();
        std::deque<OvRendering::Data::RenderFrame::Completion> m_inFlightFrames;
        bool m_frameSubmissionEnabled = false;
        std::optional<OvRendering::Data::RenderFrame> m_pendingFrame;
		uint64_t m_recordedFrameID = 0;
		OvGame::Core::Context& m_context;
		OvUI::Modules::Canvas m_canvas;

		OvCore::Rendering::RenderFrontend m_frontend;
        OvCore::Rendering::PostProcess::PostProcessPipeline m_postProcess;
        OvRendering::Data::Material m_shadowFallback;
        OvRendering::Data::FrameInfo m_renderStats;
        OvTools::Jobs::JobSystem* m_renderJobs = nullptr;
        std::chrono::high_resolution_clock::time_point m_renderStart=std::chrono::high_resolution_clock::now();

		/* Debug elements */
		OvGame::Utils::FPSCounter	m_fpsCounter;

		#ifdef _DEBUG
		OvGame::Debug::DriverInfo	m_driverInfo;
		OvGame::Debug::FrameInfo	m_frameInfo;
		#endif

		#ifdef _DEBUG
		bool m_showDebugInformation = true;
		#else
		bool m_showDebugInformation = false;
		#endif
	};
}