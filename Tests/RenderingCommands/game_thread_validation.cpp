#define main GameFixtureMain
#include "game_frame_validation.cpp"
#undef main
#include <OvGame/Core/Application.h>
#include <OvDebug/Logger.h>
#include <OvRendering/Utils/Profiling.h>
#include <GLFW/glfw3.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <chrono>
#include "window_minimize_validation.inl"

using RenderFrame = OvRendering::Data::RenderFrame;
using List = OvRendering::Data::RenderCommandList;
std::function<void()> beforePresent;
// Called only by the current Window.cpp test copy immediately before glfwSwapBuffers.
void GameThreadBeforePresent() { if (beforePresent) beforePresent(); }
std::atomic_bool mainUpdating{false};
std::atomic_size_t overlappingDraws{0}, queries{0};
std::thread::id renderOwner, producer;
PFNGLDRAWELEMENTSINSTANCEDPROC originalDraw;
PFNGLDRAWELEMENTSPROC originalSingleDraw;
PFNGLDRAWELEMENTSBASEVERTEXPROC originalBaseVertex;
PFNGLQUERYCOUNTERPROC originalQuery;
void APIENTRY OwnerDraw(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instances)
{
    Require(std::this_thread::get_id() == renderOwner && glfwGetCurrentContext(), "product draw escaped Render owner");
    if (renderOwner != producer && mainUpdating.load()) ++overlappingDraws;
    originalDraw(mode, count, type, indices, instances);
}
void APIENTRY OwnerSingleDraw(GLenum mode, GLsizei count, GLenum type, const void* indices)
{
    Require(std::this_thread::get_id() == renderOwner && glfwGetCurrentContext(), "product draw escaped Render owner");
    if (renderOwner != producer && mainUpdating.load()) ++overlappingDraws;
    originalSingleDraw(mode,count,type,indices);
}
void APIENTRY OwnerBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint baseVertex)
{
    Require(std::this_thread::get_id() == renderOwner && glfwGetCurrentContext(), "UI draw escaped Render owner");
    if (renderOwner != producer && mainUpdating.load()) ++overlappingDraws;
    originalBaseVertex(mode,count,type,indices,baseVertex);
}
void APIENTRY OwnerQuery(GLuint id, GLenum target)
{
    Require(std::this_thread::get_id() == renderOwner && glfwGetCurrentContext(), "GPU profiling escaped Render owner");
    ++queries; originalQuery(id, target);
}
template<class F> auto OnOwner(OvGame::Core::Context& context, F&& fn)
{
    if (context.renderThread) return context.renderThread->Invoke(std::forward<F>(fn));
    return fn(*context.driver);
}
struct OwnerChecks
{
    OvGame::Core::Context& context;
    explicit OwnerChecks(OvGame::Core::Context& value) : context(value)
    {
        OnOwner(context, [](auto&) {
            renderOwner = std::this_thread::get_id();
            originalDraw = glad_glDrawElementsInstanced; glad_glDrawElementsInstanced = OwnerDraw;
            originalSingleDraw = glad_glDrawElements; glad_glDrawElements = OwnerSingleDraw;
            originalBaseVertex = glad_glDrawElementsBaseVertex; glad_glDrawElementsBaseVertex = OwnerBaseVertex;
            originalQuery = glad_glQueryCounter; glad_glQueryCounter = OwnerQuery;
        });
    }
    ~OwnerChecks()
    {
        if (context.renderThread) context.renderThread->WaitIdle();
        OnOwner(context, [](auto&) {
            beforePresent = {};
            glad_glDrawElementsInstanced = originalDraw; glad_glDrawElements = originalSingleDraw; glad_glDrawElementsBaseVertex = originalBaseVertex; glad_glQueryCounter = originalQuery;
        });
    }
};
struct Gate
{
    std::mutex mutex; std::condition_variable changed; bool entered=false, released=false, producerDone=false, timeout=false;
    void Block() { std::unique_lock lock(mutex); entered=true; changed.notify_all(); changed.wait(lock,[&]{return released;}); }
};
void VerifyOverlap(OvGame::Core::Game& game, OvGame::Core::Context& context)
{
    // FPS text changes after the zero-delta recording matrix. ImGui 1.92 bakes
    // glyphs lazily: a previously unseen digit legitimately creates a new font
    // generation on the GL owner. This gate tests steady, already-warm inputs.
    const auto text=game.m_fpsCounter.m_text.content;
    game.m_fpsCounter.m_text.content="0123456789 FPS";
    const auto allocations=game.m_frontend.GetResourcePool()->GetStatistics().allocations;
    game.PreUpdate();game.Update(0);game.PostUpdate();game.DrainRenderFrames();
    game.m_fpsCounter.m_text.content=text;
    Require(game.m_frontend.GetResourcePool()->GetStatistics().allocations==allocations,"Game warm-up unexpectedly allocated pool targets");
    std::puts("PASS: Game overlap prewarmed actual FPS digit text; unchanged pool allocations");
    Gate gate;
    List commands; commands.Append([&](auto&) { gate.Block(); }); commands.Seal();
    RenderFrame blocker; blocker.Append(std::move(commands)); blocker.Seal();
    auto held = context.renderThread->Submit(std::move(blocker));
    struct DrainGate
    {
        Gate& gate; OvRendering::Threading::RenderThread& owner;
        ~DrainGate()
        {
            { std::lock_guard lock(gate.mutex); gate.released=true; gate.changed.notify_all(); }
            owner.WaitIdle();
        }
    } drain{gate,*context.renderThread};
    { std::unique_lock lock(gate.mutex); Require(gate.changed.wait_for(lock,std::chrono::seconds(5),[&]{return gate.entered;}),"Render gate did not start"); }
    std::jthread rescue([&] {
        std::unique_lock lock(gate.mutex);
        gate.timeout = !gate.changed.wait_for(lock,std::chrono::seconds(5),[&]{return gate.producerDone;});
        gate.released = true; gate.changed.notify_all();
    });
    game.PreUpdate(); game.Update(.02f); game.PostUpdate();
    const auto first = game.m_inFlightFrames.back();
    game.PreUpdate(); game.Update(.02f);
    const bool unretired = !held.IsRetired() && !first.IsRetired();
    { std::lock_guard lock(gate.mutex); gate.producerDone=true; gate.changed.notify_all(); }
    rescue.join();
    Require(!gate.timeout && unretired, "Game Update/PostUpdate waited for Render despite available capacity");
    game.PostUpdate(); game.DrainRenderFrames();
    Require(game.m_inFlightFrames.empty(), "Game did not collect every in-flight completion");
    std::puts("PASS: actual Game advances two updates and admits a frame while Render is held; two-frame capacity and final completion drain");
}
void VerifyScopes(OvGame::Core::Context& context)
{
    static constexpr tracy::SourceLocationData source{"Recorded nested GPU scope", __FUNCTION__, __FILE__, __LINE__, 0};
    List commands;
    const auto before = queries.load();
    {
        OvRendering::Utils::GpuScope outer(&commands, &source);
        commands.Append([](auto&) {});
        { OvRendering::Utils::GpuScope inner(&commands, &source); commands.Append([](auto&) { throw std::runtime_error("scope failure"); }); }
    }
    commands.Seal(); Require(queries == before, "recording issued GPU timestamps");
    const bool connected = tracy::GetProfiler().IsConnected();
    bool failed = false;
    try { context.renderThread->Invoke([&](auto& driver) { commands.Execute(driver); }); }
    catch (const std::runtime_error&) { failed = true; }
    Require(failed, "scoped operation lost failure");
    if (connected) Require(queries == before + 4, "nested failing GPU scopes did not close on owner");
    List cancelled;
    { OvRendering::Utils::GpuScope scope(&cancelled,&source); cancelled.Append([](auto&) {}); }
    const auto after = queries.load(); cancelled.Cancel(); Require(queries == after, "cancelled scope touched GPU");
    List abandoned;
    try { OvRendering::Utils::GpuScope scope(&abandoned,&source); abandoned.Append([](auto&) {}); throw std::runtime_error("record failure"); }
    catch (const std::runtime_error&) {}
    Require(abandoned.GetState() == List::State::FAILED, "recording unwind left a usable partial scope");
    std::puts("PASS: producer scope recording/cancellation emits no GPU calls; nested consumer failure unwinds and failed recording cannot seal");
}
void VerifyGameRecordingModes(OvGame::Core::Game& game,OvGame::Core::Context& context)
{
    OvTools::Jobs::JobSystem one(1),two(2);std::vector<uint8_t> expected,actual(320*240*4);
    OnOwner(context,[&](auto&){beforePresent=[&]{glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());};});
    for(bool submitted:{false,true}) {
        if(!submitted&&context.renderThread)continue;
        game.SetFrameSubmissionEnabled(submitted);
        for(auto* jobs:std::array<OvTools::Jobs::JobSystem*,3>{nullptr,&one,&two}) {
            game.m_renderJobs=jobs;game.PreUpdate();game.Update(0);game.PostUpdate();game.DrainRenderFrames();
            if(expected.empty())expected=actual;else Require(actual==expected,"Game serial/one/two Worker or immediate/submitted product pixels differ");
        }
    }
    OnOwner(context,[](auto&){beforePresent={};});game.SetFrameSubmissionEnabled(true);
    std::puts("PASS: actual Game product graph: serial/one/two Worker exact scene/UI images, immediate and submitted Main plus Render owner");
}
void SetThreadMode(const std::filesystem::path& project, bool enabled)
{
    OvTools::Filesystem::IniFile settings((project/"Game.ini").string());
    settings.Remove("render_thread"); settings.Add("render_thread", enabled); settings.Remove("parallel_recording"); settings.Add("parallel_recording",true); settings.Rewrite();
}
#ifndef GAME_THREAD_TEST_MAIN
#define GAME_THREAD_TEST_MAIN main
#endif
int GAME_THREAD_TEST_MAIN(int argc, char** argv) try
{
    producer = std::this_thread::get_id();
    if (argc > 2 && std::string(argv[2]) == "--capture")
    {
        std::puts("TRACE READY"); std::fflush(stdout);
        const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while (!tracy::GetProfiler().IsConnected() && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Require(tracy::GetProfiler().IsConnected(), "Tracy capture did not connect");
    }
    if (const auto result = GameFixtureMain(argc,argv)) return result;
    const auto project = std::filesystem::path(argv[1])/"GameProject";
    std::vector<std::vector<uint8_t>> expected;
    std::vector<std::string> poses;
    for (bool threaded : {false, true})
    {
        SetThreadMode(project, threaded);
        OvGame::Core::Application app;
        auto& game = app.m_game; auto& context = app.m_context;
        std::mutex mutex;
        std::deque<size_t> pending;
        size_t captured = 0;
        std::vector<RenderFrame::Completion> completions;
        OwnerChecks checks(context);
        Require(bool(context.renderThread) == threaded, "product ignored render_thread setting");
        Require(threaded ? !glfwGetCurrentContext() : bool(glfwGetCurrentContext()), "product context ownership incorrect");
        ImGui::GetIO().ConfigInputTextCursorBlink = false;
        beforePresent = [&] {
            Require(std::this_thread::get_id() == renderOwner, "presentation escaped owner");
            size_t index;
            { std::lock_guard lock(mutex); Require(!pending.empty(),"unexpected product presentation"); index=pending.front(); pending.pop_front(); }
            glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
            std::vector<uint8_t> image(320*240*4); glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
            if (threaded) Require(image == expected.at(index), "threaded Game scene/UI image differs from same-thread product");
            else expected.push_back(std::move(image));
            ++captured;
        };
        for (int i=0;i<32;++i)
        {
            game.m_showDebugInformation = i>=16;
            game.PreUpdate(); mainUpdating=true; game.Update(.02f); mainUpdating=false;
            std::ostringstream pose(std::ios::binary);
            for (auto* actor : context.sceneManager.GetCurrentScene()->GetActors()) if (auto* skin=actor->GetComponent<Skin>()) Snapshot(pose,*skin);
            if (threaded) Require(pose.str() == poses.at(i), "threaded Game pose differs"); else poses.push_back(pose.str());
            completions.push_back(game.m_pendingFrame->GetCompletion());
            { std::lock_guard lock(mutex); pending.push_back(size_t(i)); }
            context.window->KeyPressedEvent.Invoke(int(OvWindowing::Inputs::EKey::KEY_SPACE));
            game.PostUpdate();
            Require(!context.inputManager->IsKeyPressed(OvWindowing::Inputs::EKey::KEY_SPACE), "threaded Game did not clear input");
        }
        game.DrainRenderFrames();
        Require(captured==32 && pending.empty(), "Game lost a presentation");
        for (const auto& completion:completions) Require(completion.Succeeded() && completion.PresentationRequested(), "product frame did not present/retire successfully");
        OnOwner(context, [](auto&) { beforePresent={}; Require(glGetError()==GL_NO_ERROR,"threaded product GL error"); });
        VerifyGameRecordingModes(game,context);
        if (threaded)
        {
            VerifyScopes(context); VerifyOverlap(game,context);
            Require(context.sceneManager.LoadScene("frame.ovscene"), "threaded scene reload failed"); context.sceneManager.GetCurrentScene()->Play();
            for(int i=0;i<64;++i) { game.PreUpdate(); mainUpdating=true; game.Update(.02f); mainUpdating=false; game.PostUpdate(); }
            game.DrainRenderFrames();
            WindowMinimizeValidation(game,context);
            RejectProductCall([&]{game.SetFrameSubmissionEnabled(false);}, "threaded Game accepted immediate GL execution on Main");
            auto presented = std::make_shared<std::atomic_int>(0);
            const auto listener=OvDebug::Logger::LogEvent += [&](const auto& log) {
                if(log.message=="threaded application stop") { Require(std::this_thread::get_id()==producer,"window close left Main");context.window->SetShouldClose(true); }
            };
            context.renderThread->Invoke([&](auto&) { beforePresent=[presented] { if(++*presented==4) OVLOG_INFO("threaded application stop"); }; });
            struct Unsubscribe { decltype(listener) token; ~Unsubscribe() { OvDebug::Logger::LogEvent-=token; } } unsubscribe{listener};
            app.Run();
            Require(*presented>=4 && *presented<=8 && game.m_inFlightFrames.empty(), "Application.Run failed to drain on Main-thread close");
            context.renderThread->Invoke([](auto&) { beforePresent={}; });
            game.Update(.02f); // Prepared but unsubmitted frame is cancelled during actual Application destruction.
        }
    }
    for (int iteration=0;iteration<4;++iteration)
    {
        RenderFrame::Completion submitted, abandoned;
        baregl::Texture::Snapshot survivor;
        {
            OvGame::Core::Application app; auto& game=app.m_game; auto& context=app.m_context;
            OwnerChecks checks(context);
            for(int i=0;i<8;++i) { game.PreUpdate(); game.Update(.02f); submitted=game.m_pendingFrame->GetCompletion(); game.PostUpdate(); }
            game.Update(.02f); abandoned=game.m_pendingFrame->GetCompletion();
            survivor=context.framebuffer->RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR)->RetainStorage();
        }
        Require(submitted.Succeeded() && abandoned.IsRetired() && !abandoned.Succeeded() && !survivor.GetID(),
            "Application destruction failed to drain/cancel/invalidate resources");
        Require(!glfwGetCurrentContext(), "threaded application exit left Main owning GL");
    }
    {
        OvGame::Core::Application app; auto& game=app.m_game; auto& context=app.m_context;
        OwnerChecks checks(context);
        context.renderThread->Invoke([](auto&) { beforePresent=[] { throw std::runtime_error("Game presentation failure"); }; });
        game.Update(.02f); const auto failedFrame=game.m_pendingFrame->GetCompletion(); game.PostUpdate();
        bool failed=false;
        try { game.DrainRenderFrames(); } catch(const std::runtime_error& error) { failed=std::string(error.what())=="Game presentation failure"; }
        Require(failed && failedFrame.IsRetired() && !failedFrame.Succeeded(), "Game lost its Render failure or did not drain it");
        context.renderThread->Invoke([](auto&) { beforePresent={}; });
    }
    std::puts("PASS: four actual Application restarts drain submitted frames, cancel prepared frames and invalidate retained textures; Render presentation failure reaches Main after GPU retirement");
    Require(overlappingDraws > 0, "natural product run did not show Draw/Main Update overlap");
    size_t movingImages=0;
    for(size_t i=1;i<16;++i) if(expected[i]!=expected[i-1])++movingImages;
    Require(movingImages==15 && poses.front()!=poses.at(7), "product comparison did not animate");
    std::printf("PASS: 32 exact actual Game pose/scene/UI pairs across Main and Render contexts, 15 moving scene transitions, ordered presentation/completion, scene reload, Application.Run/exit; %zu draw calls overlap Main Update, %zu owner GPU timestamps\n",overlappingDraws.load(),queries.load());
    return 0;
}
catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
