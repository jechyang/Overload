#include <OvRendering/Context/Driver.h>
/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <tracy/Tracy.hpp>

#include <OvDebug/Logger.h>
#include <OvGame/Core/Game.h>
#include <OvRendering/Data/PortableCommands.h>
#include <OvTools/Utils/PathParser.h>
#include <OvUI/Widgets/Texts/Text.h>

namespace {
    void AddWindowPresentation(OvRendering::Data::RenderPlan& plan,std::pair<uint16_t,uint16_t> size)
    {
        using namespace OvRendering::Data;
        if(!size.first||!size.second)return;
        RenderPlanPass pass;pass.name="Window.Present";pass.view=RenderPlanPass::NoView;
        pass.accesses.push_back({plan.ImportBackbuffer(size.first,size.second,true),{},ResourceAccessType::Present});
        pass.operation=[] {return [](auto&) {};};plan.passes.push_back(std::move(pass));
    }
}

OvGame::Core::Game::Game(Context & p_context) :
	m_context(p_context),
	m_fpsCounter(*p_context.window)
	#ifdef _DEBUG
	,
	m_driverInfo(*m_context.driver, *m_context.window),
	m_frameInfo(*m_context.window)
	#endif
{
    m_frontend.GetResourcePool()->SetFrameReserve(m_context.renderThread?2:1);
	m_shadowFallback.SetShader(m_context.shaderManager[":Shaders\\ShadowFallback.ovfx"]);
	m_context.uiManager->SetCanvas(m_canvas);
	m_canvas.AddPanel(m_fpsCounter);
	#ifdef _DEBUG
	m_canvas.AddPanel(m_driverInfo);
	m_canvas.AddPanel(m_frameInfo);

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

void OvGame::Core::Game::Update(float p_deltaTime)
{
    ZoneScopedN("Game Update");
    if (m_pendingFrame) throw std::logic_error("Previous Game frame has not been submitted");
    std::optional<OvRendering::Data::RenderFrame> frame;
    if (m_frameSubmissionEnabled) frame.emplace(++m_recordedFrameID);
    std::unique_ptr<OvCore::Rendering::SceneFrame> product;
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

        product=BuildSceneFrame();
	}

	m_context.sceneManager.Update();

	if  (m_context.inputManager->IsKeyPressed(OvWindowing::Inputs::EKey::KEY_F12))
		m_showDebugInformation = !m_showDebugInformation;

	if (m_showDebugInformation)
	{
		m_fpsCounter.Update(p_deltaTime);
		#ifdef _DEBUG
		m_frameInfo.Update(m_renderStats);
		#endif
        if(!product)product=m_frontend.ComposeFrame({},{});
        m_context.uiManager->RecordPlan(product->ComposePlan());
	}
    if(!product)product=m_frontend.ComposeFrame({},{});
    AddWindowPresentation(product->ComposePlan(),m_context.window->GetSize());
    product=m_frontend.PrepareFrame(std::move(product),m_context.driver->CreatePipelineState(),m_renderJobs);
    m_renderStats=product->GetFrameInfo();
    auto commands=m_frontend.RecordFrame(std::move(product),m_renderJobs,nullptr,true);
    if(frame){frame->AppendRecording(std::move(commands));frame->Seal(m_renderJobs);m_pendingFrame.emplace(std::move(*frame));}
    else {commands.Seal(m_renderJobs);commands.Execute(*m_context.driver);}
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
    m_renderJobs = enabled && m_context.projectSettings.GetOrDefault<bool>("parallel_recording", false) ? m_context.jobSystem.get() : nullptr;
    m_context.uiManager->SetCommandRecordingEnabled(enabled);
    m_frameSubmissionEnabled = enabled;
}

OvRendering::Data::RenderFrame OvGame::Core::Game::RecordRenderFrame()
{
    auto product=BuildSceneFrame();
    if(m_showDebugInformation)m_context.uiManager->RecordPlan(product->ComposePlan());
    AddWindowPresentation(product->ComposePlan(),m_context.window->GetSize());
    product=m_frontend.PrepareFrame(std::move(product),m_context.driver->CreatePipelineState(),m_renderJobs);m_renderStats=product->GetFrameInfo();
    OvRendering::Data::RenderFrame frame(++m_recordedFrameID);
    frame.AppendRecording(m_frontend.RecordFrame(std::move(product),m_renderJobs,nullptr,true));frame.Seal(m_renderJobs);return frame;
}
std::unique_ptr<OvCore::Rendering::SceneFrame> OvGame::Core::Game::BuildSceneFrame()
{
    using namespace OvRendering::Data;
    auto* scene=m_context.sceneManager.GetCurrentScene();const auto [width,height]=m_context.window->GetSize();
    auto product=m_frontend.ComposeFrame({},{});
    if(scene&&width&&height) {
        if(auto camera=scene->FindMainCamera()) {
            OvCore::Rendering::SceneRenderRequest request{{*scene,camera->GetCamera(),width,height,0,0,
                std::chrono::duration<float>(std::chrono::high_resolution_clock::now()-m_renderStart).count()}};
            const auto state=m_context.driver->CreatePipelineState();
            OvCore::Rendering::SceneFrameRequest input{request,m_context.framebuffer->RecordTarget(),m_shadowFallback};
            input.postProcess=&m_postProcess;input.postProcessTarget=m_context.framebuffer.get();input.postProcessState=state;
            product=m_frontend.ExtractFrame(input);
        } else {
            const auto target=m_context.framebuffer->RecordTarget();RenderPlanPass clear;clear.name="Game.EmptySceneClear";clear.view=RenderPlanPass::NoView;clear.target=target;
            for(const auto& attachment:target.GetTextureAttachments())if(attachment.writable) {
                const auto handle=product->ComposePlan().Import("Game.Color",attachment.storage,ResourceOrigin::CurrentFrame);
                clear.accesses.push_back({handle,{},ResourceAccessType::Attachment,AttachmentLoad::Clear});
            }
            for(const auto& attachment:target.GetRenderbufferAttachments()) {
                const auto handle=product->ComposePlan().Import("Game.DepthStencil",attachment.storage,ResourceOrigin::CurrentFrame);
                const auto aspect=attachment.attachment==baregl::types::EFramebufferAttachment::DEPTH?ResourceAspect::Depth:ResourceAspect::Stencil;
                clear.accesses.push_back({handle,{aspect},ResourceAccessType::Attachment,AttachmentLoad::Clear});
            }
            clear.operation=[target] {return ClearTarget(target,true,true,true);};product->ComposePlan().passes.push_back(std::move(clear));
        }
        auto& plan=product->ComposePlan();RenderPlanPass blit;blit.name="Game.BackbufferBlit";blit.view=RenderPlanPass::NoView;
        for(const auto& attachment:m_context.framebuffer->RecordTarget().GetTextureAttachments())if(attachment.writable) {
            const auto handle=plan.Find(attachment.storage);if(!handle)throw std::logic_error("Game output has no producer");
            blit.accesses.push_back({*handle,{},ResourceAccessType::CopySource});
        }
        blit.accesses.push_back({plan.ImportBackbuffer(width,height,false),{},ResourceAccessType::CopyDestination,AttachmentLoad::DontCare,AttachmentStore::Store,true});
        const auto target=m_context.framebuffer->RecordTarget();blit.operation=[target,width,height] {return PresentColor(target,width,height);};plan.passes.push_back(std::move(blit));
    }
    return product;
}
OvRendering::Data::RenderCommandList OvGame::Core::Game::RecordSceneCommands()
{
    auto product=BuildSceneFrame();AddWindowPresentation(product->ComposePlan(),m_context.window->GetSize());
    product=m_frontend.PrepareFrame(std::move(product),m_context.driver->CreatePipelineState(),m_renderJobs);m_renderStats=product->GetFrameInfo();
    return m_frontend.RecordFrame(std::move(product),m_renderJobs);
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
