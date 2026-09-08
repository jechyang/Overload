/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <tracy/Tracy.hpp>
#include <baregl/detail/glad/glad.h>

#include <OvDebug/Logger.h>
#include <OvGame/Core/Game.h>
#include <OvTools/Utils/PathParser.h>
#include <OvUI/Widgets/Texts/Text.h>

#ifdef _DEBUG
#include <OvRendering/Features/FrameInfoRenderFeature.h>
#endif

OvGame::Core::Game::Game(Context & p_context) :
	m_context(p_context),
	m_sceneRenderer(*p_context.driver),
	m_fpsCounter(*p_context.window)
	#ifdef _DEBUG
	,
	m_driverInfo(*m_context.driver, *m_context.window),
	m_frameInfo(*m_context.window)
	#endif
{
	m_context.uiManager->SetCanvas(m_canvas);
	m_canvas.AddPanel(m_fpsCounter);
	#ifdef _DEBUG
	m_canvas.AddPanel(m_driverInfo);
	m_canvas.AddPanel(m_frameInfo);
	m_sceneRenderer.AddFeature<
		OvRendering::Features::FrameInfoRenderFeature,
		OvRendering::Features::EFeatureExecutionPolicy::ALWAYS
	>();
	#endif

	std::string startupScenePath = m_context.projectSettings.Get<std::string>("start_scene");
	startupScenePath = OvTools::Utils::PathParser::MakeNonWindowsStyle(startupScenePath);
	m_context.sceneManager.LoadScene(startupScenePath);
	m_context.sceneManager.GetCurrentScene()->Play();
    if (m_context.renderThread) SetFrameSubmissionEnabled(true);
}

OvGame::Core::Game::~Game()
{
    try
    {
        if (m_context.renderThread && m_pendingFrame)
        {
            m_context.renderThread->Discard(std::move(*m_pendingFrame));
            m_pendingFrame.reset();
        }
        DrainRenderFrames();
    }
    catch (const std::exception& error) { OVLOG_ERROR(error.what()); }
	m_context.sceneManager.UnloadCurrentScene();
}

void OvGame::Core::Game::PreUpdate()
{
	ZoneScoped;
	OvDebug::Logger::DispatchPendingEvents();
    CollectCompletedFrames();

	m_context.device->PollEvents();
}

void RenderCurrentScene(
	OvCore::Rendering::SceneRenderer& p_renderer,
	const OvGame::Core::Context& p_context
)
{
	ZoneScoped;

	if (auto currentScene = p_context.sceneManager.GetCurrentScene())
	{
		if (auto camera = currentScene->FindMainCamera())
		{
			auto [windowWidth, windowHeight] = p_context.window->GetSize();

			p_renderer.AddDescriptor<OvCore::Rendering::SceneRenderer::SceneDescriptor>({
				*currentScene,
			});

			OvRendering::Data::FrameDescriptor frameDescriptor;
			frameDescriptor.renderWidth = windowWidth;
			frameDescriptor.renderHeight = windowHeight;
			frameDescriptor.camera = camera->GetCamera();
			frameDescriptor.outputBuffer = *p_context.framebuffer;

			p_renderer.BeginFrame(frameDescriptor);
			p_renderer.DrawFrame();
			p_renderer.EndFrame();
		}
		else
		{
			p_renderer.Clear(true, true, true);
		}
	}
	else
	{
		p_renderer.Clear(true, true, true);
	}
}

void OvGame::Core::Game::Update(float p_deltaTime)
{
    ZoneScopedN("Game Update");
    if (m_pendingFrame) throw std::logic_error("Previous Game frame has not been submitted");
    std::optional<OvRendering::Data::RenderFrame> frame;
    if (m_frameSubmissionEnabled) frame.emplace(++m_recordedFrameID);
	if (auto currentScene = m_context.sceneManager.GetCurrentScene())
	{
		{
			#ifdef _DEBUG
			ZoneScopedN("Physics Update");
			#endif

			if (m_context.physicsEngine->Update(p_deltaTime))
				currentScene->FixedUpdate(p_deltaTime);
		}

		{
			#ifdef _DEBUG
			ZoneScopedN("Scene Update");
			#endif
			currentScene->Update(p_deltaTime, *m_context.jobSystem);
			currentScene->LateUpdate(p_deltaTime);
		}

		{
			#ifdef _DEBUG
			ZoneScopedN("Audio Update");
			#endif
			m_context.audioEngine->Update();
		}

        if (frame) frame->Append(RecordSceneCommands());
        else
        {
            RenderCurrentScene(m_sceneRenderer, m_context);
            const auto [windowWidth, windowHeight] = m_context.window->GetSize();
            m_context.framebuffer->BlitToBackBuffer(windowWidth, windowHeight);
        }
	}

	m_context.sceneManager.Update();

	if  (m_context.inputManager->IsKeyPressed(OvWindowing::Inputs::EKey::KEY_F12))
		m_showDebugInformation = !m_showDebugInformation;

	if (m_showDebugInformation)
	{
		m_fpsCounter.Update(p_deltaTime);
		#ifdef _DEBUG
		auto& frameInfoRenderFeature = m_sceneRenderer.GetFeature<OvRendering::Features::FrameInfoRenderFeature>();
		auto& frameInfo = frameInfoRenderFeature.GetFrameInfo();
		m_frameInfo.Update(frameInfo);
		#endif
        if (frame) frame->Append(m_context.uiManager->RecordRenderCommands());
        else m_context.uiManager->Render();
	}
    if (frame) { frame->Seal(); m_pendingFrame.emplace(std::move(*frame)); }
}

void OvGame::Core::Game::PostUpdate()
{
    ZoneScoped;
    CollectCompletedFrames();
    if (m_frameSubmissionEnabled)
    {
        if (!m_pendingFrame) throw std::logic_error("Game frame was not prepared");
        auto frame = std::move(*m_pendingFrame); m_pendingFrame.reset();
        if (m_context.renderThread)
        {
            m_context.window->ApplyPendingCursor();
            // Allocate completion storage before admission transfers ownership.
            m_inFlightFrames.push_back(frame.GetCompletion());
            try
            {
                m_context.renderThread->Submit(std::move(frame), [window = m_context.window.get()] { window->Present(); });
            }
            catch (...)
            {
                m_inFlightFrames.pop_back();
                m_context.renderThread->Discard(std::move(frame));
                throw;
            }
            m_context.inputManager->ClearEvents();
        }
        else
        {
            frame.Submit(*m_context.driver, [&] { m_context.window->SwapBuffers(); });
            frame.Wait();
            m_context.inputManager->ClearEvents();
            frame.GetCompletion().RethrowIfFailed();
        }
    }
    else
    {
        m_context.window->SwapBuffers();
        m_context.inputManager->ClearEvents();
        m_context.driver->OnFrameCompleted();
    }
}

void OvGame::Core::Game::SetFrameSubmissionEnabled(bool enabled)
{
    if (m_pendingFrame) throw std::logic_error("Cannot switch Game submission mode during a frame");
    if (!enabled && m_context.renderThread) throw std::logic_error("Threaded Game requires command submission");
    DrainRenderFrames();
    m_sceneRenderer.SetDrawRecordingEnabled(enabled);
    m_sceneRenderer.SetCommandRecordingJobs(enabled && m_context.projectSettings.GetOrDefault<bool>("parallel_recording", false) ? m_context.jobSystem.get() : nullptr);
    m_context.uiManager->SetCommandRecordingEnabled(enabled);
    m_frameSubmissionEnabled = enabled;
}

OvRendering::Data::RenderFrame OvGame::Core::Game::RecordRenderFrame()
{
    OvRendering::Data::RenderFrame frame(++m_recordedFrameID);
    frame.Append(RecordSceneCommands());
    if (m_showDebugInformation) frame.Append(m_context.uiManager->RecordRenderCommands());
    frame.Seal();
    return frame;
}

OvRendering::Data::RenderCommandList OvGame::Core::Game::RecordSceneCommands()
{
    m_sceneRenderer.SetDrawRecordingEnabled(true);
    m_sceneRenderer.BeginCommandRecording();
    try
    {
        if (m_context.sceneManager.GetCurrentScene())
        {
            RenderCurrentScene(m_sceneRenderer, m_context);
            const auto [width, height] = m_context.window->GetSize();
            const auto target = m_context.framebuffer->RecordTarget();
            m_sceneRenderer.SubmitRenderOperation([target, width, height](auto&) {
                const auto [sourceWidth, sourceHeight] = target.GetSize();
                glBlitNamedFramebuffer(target.GetID(), 0, 0, 0, sourceWidth, sourceHeight,
                    0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            });
        }
        return m_sceneRenderer.FinishCommandRecording();
    }
    catch (...)
    {
        const auto error = std::current_exception();
        try { if (m_sceneRenderer.IsDrawing()) m_sceneRenderer.EndFrame(); } catch (...) {}
        m_sceneRenderer.CancelCommandRecording();
        std::rethrow_exception(error);
    }
}

void OvGame::Core::Game::CollectCompletedFrames()
{
    while (!m_inFlightFrames.empty() && m_inFlightFrames.front().IsRetired())
    {
        auto completion = m_inFlightFrames.front(); m_inFlightFrames.pop_front();
        completion.RethrowIfFailed();
    }
    if (m_context.renderThread) m_context.renderThread->RethrowIfFailed();
}

void OvGame::Core::Game::DrainRenderFrames()
{
    if (m_context.renderThread) m_context.renderThread->WaitIdle();
    CollectCompletedFrames();
}
