#define main EditorFixtureMain
#include "../Skinning/editor_validation.cpp"
#undef main
#include <OvEditor/Core/Application.h>
#include <OvDebug/Logger.h>
#include <OvRendering/Utils/Profiling.h>
#include <GLFW/glfw3.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <chrono>

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
template<class F> auto OnOwner(OvEditor::Core::Context& context, F&& fn)
{
    if (context.renderThread) return context.renderThread->Invoke(std::forward<F>(fn));
    return fn(*context.driver);
}
struct OwnerChecks
{
    OvEditor::Core::Context& context;
    explicit OwnerChecks(OvEditor::Core::Context& value) : context(value)
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

void SetEditorThreadMode(const std::filesystem::path& project, bool enabled)
{
    OvTools::Filesystem::IniFile settings((project/"SkinningProject.ovproject").string());
    settings.Remove("render_thread"); settings.Add("render_thread",enabled); settings.Rewrite();
}
struct ViewsFixture
{
    OvEditor::Core::Editor& editor;
    OvEditor::Core::Context& context;
    OvUI::Modules::Canvas canvas;
    std::array<OvEditor::Panels::AView*,3> views;
    ViewsFixture(OvEditor::Core::Editor& e, OvEditor::Core::Context& c):editor(e),context(c),views{
        &e.m_panelsManager.GetPanelAs<OvEditor::Panels::AssetView>("Asset View"),
        &e.m_panelsManager.GetPanelAs<OvEditor::Panels::GameView>("Game View"),
        &e.m_panelsManager.GetPanelAs<OvEditor::Panels::SceneView>("Scene View")}
    {
        context.uiManager->EnableEditorLayoutSave(false);
        ImGui::GetIO().ConfigInputTextCursorBlink=false;
        context.window->SetSize(960,320);
        OnOwner(context,[&](auto&){context.device->SetVsync(false);});
        for(auto& [name,panel]:editor.m_panelsManager.m_panels)
            if(auto* window=dynamic_cast<OvUI::Panels::PanelWindow*>(panel.get()))window->Close();
        int index=0;
        for(auto* view:views)
        {view->Open();view->dockable=false;view->autoSize=false;view->SetPosition({float(index++*320),20});view->SetSize({320,260});canvas.AddPanel(*view);}
        context.uiManager->SetCanvas(canvas);
        for(int i=0;i<4;++i)
        {auto commands=context.uiManager->RecordRenderCommands();OnOwner(context,[&](auto& driver){commands.Execute(driver);});}
        Require(context.sceneManager.LoadScene("crowd.ovscene"),"threaded Editor saved scene load");
        auto* model=context.modelManager.GetResource(":Models/Mannequin.fbx");
        auto& asset=*static_cast<OvEditor::Panels::AssetView*>(views[0]);
        asset.SetResource(OvEditor::Panels::AssetView::ViewableResource{model});
        for(auto* stack:asset.GetScene()->GetFastAccessComponents().postProcessStacks)
        {auto settings=stack->GetAutoExposureSettings();settings.progressive=false;stack->SetAutoExposureSettings(settings);}
        for(auto* probe:asset.GetScene()->GetFastAccessComponents().reflectionProbes)
            probe->SetCaptureSpeed(OvCore::ECS::Components::CReflectionProbe::ECaptureSpeed::SIX_FACES);
        for(int i=0;i<240;++i)asset.Update(1.f/60);
        const auto bounds=model->GetBoundingSphere();
        views[2]->GetCamera()->SetPosition(bounds.position+FVector3{0,bounds.radius*7.f,std::max(bounds.radius*13.f,2.f)});
        views[2]->GetCamera()->SetRotation(FQuaternion(FVector3{28,180,0}));
        for(int i=0;i<6;++i){editor.PreUpdate();editor.Update(0);editor.PostUpdate();}
        editor.DrainRenderFrames();
        for(auto* view:views)Require(view->IsVisible(),"thread fixture view not visible");
    }
    ~ViewsFixture(){context.uiManager->SetCanvas(editor.m_canvas);}
};
void VerifyEditorOverlap(OvEditor::Core::Editor& editor,OvEditor::Core::Context& context)
{
    Gate gate;
    List commands;commands.Append([&](auto&){gate.Block();});commands.Seal();
    RenderFrame blocker;blocker.Append(std::move(commands));blocker.Seal();
    auto held=context.renderThread->Submit(std::move(blocker));
    struct DrainGate
    {Gate& gate;OvRendering::Threading::RenderThread& owner;~DrainGate(){ {std::lock_guard lock(gate.mutex);gate.released=true;gate.changed.notify_all();}owner.WaitIdle();}} drain{gate,*context.renderThread};
    {std::unique_lock lock(gate.mutex);Require(gate.changed.wait_for(lock,std::chrono::seconds(5),[&]{return gate.entered;}),"Editor Render gate did not start");}
    std::jthread rescue([&]{std::unique_lock lock(gate.mutex);gate.timeout=!gate.changed.wait_for(lock,std::chrono::seconds(5),[&]{return gate.producerDone;});gate.released=true;gate.changed.notify_all();});
    editor.PreUpdate();editor.Update(.02f);const auto firstInput=editor.m_recordedInteraction;editor.PostUpdate();
    const auto first=editor.m_inFlightFrames.back().completion;
    editor.PreUpdate();editor.Update(.02f);const auto secondInput=editor.m_recordedInteraction;
    const bool pending=!held.IsRetired()&&!first.IsRetired()&&firstInput&&secondInput&&firstInput!=secondInput&&!firstInput->consumed;
    {std::lock_guard lock(gate.mutex);gate.producerDone=true;gate.changed.notify_all();}rescue.join();
    Require(!gate.timeout&&pending,"Editor overwrote pending interaction or synchronized steady recording with Render");
    editor.PostUpdate();editor.DrainRenderFrames();
    Require(firstInput->consumed&&secondInput->consumed&&editor.m_inFlightFrames.empty(),"Editor did not consume ordered per-frame interactions");
    std::puts("PASS: actual Editor advances two updates while Render is held and preserves both interaction/completion pairs");
}
void VerifyEditorInteraction(OvEditor::Core::Editor& editor,OvEditor::Core::Context& context)
{
    auto& view=editor.m_panelsManager.GetPanelAs<OvEditor::Panels::SceneView>("Scene View");
    auto& actions=editor.m_editorActions;actions.UnselectActor();
    view.m_hovered=true;editor.Update(0);auto reference=editor.m_recordedInteraction;editor.PostUpdate();editor.DrainRenderFrames();
    Require(reference&&reference->readback,"threaded picking fixture has no image");
    uint64_t picked=0;OvMaths::FVector2 point;
    const auto [w,h]=reference->readback->GetSize();
    auto& scene=*context.sceneManager.GetCurrentScene();
    for(uint32_t y=0;y<h&&!picked;++y)for(uint32_t x=0;x<w&&!picked;++x)
        if(auto result=reference->readback->Resolve(scene,x,y))
            if(auto* actor=std::get_if<OvTools::Utils::OptRef<OvCore::ECS::Actor>>(&*result))
                if(actor->value().GetComponent<CMaterialRenderer>()){picked=actor->value().GetGUID();point={float(x),float(h-y)};}
    Require(picked!=0,"threaded picking fixture contains no actor");
    auto* material=scene.FindActorByGUID(picked)->GetComponent<CMaterialRenderer>();
    // Keep the product frame result pending while overwriting source pass state.
    view.m_hovered=true;editor.Update(0);auto first=editor.m_recordedInteraction;
    first->mouse=point;first->pressed=true;first->hovered=true;first->resizing=false;
    view.m_image->GetPlugin<OvUI::Plugins::DDTarget<std::pair<std::string,OvUI::Widgets::Layout::Group*>>>()->DataReceivedEvent.Invoke({":Materials/Default.ovmat",nullptr});
    Require(first->materialDrop.has_value(),"threaded drop did not attach to its frame");
    material->RemoveMaterialAtIndex(0);
    editor.PostUpdate();
    // Record another view directly to overwrite source pass/UI state while the
    // product completion still owns the first input. It is intentionally cancelled.
    auto newer=view.RecordRenderCommands();newer.Cancel();view.m_hovered=false;
    editor.DrainRenderFrames();
    Require(first->consumed&&actions.IsAnyActorSelected()&&actions.GetSelectedActor().GetGUID()==picked,"threaded picking lost captured input");
    Require(material->GetMaterialAtIndex(0)!=nullptr,"threaded material drop lost readback association");
    Require(!view.CompleteInteraction(first),"threaded interaction applied twice");
    // Replacing the scene destroys its actors while the old frame is pending;
    // completion must reject the old scene token.
    view.m_hovered=true;editor.Update(0);auto replaced=editor.m_recordedInteraction;editor.PostUpdate();
    context.sceneManager.LoadEmptyScene();editor.DrainRenderFrames();
    Require(replaced->consumed&&!actions.IsAnyActorSelected(),"old threaded scene interaction survived replacement");
    Require(context.sceneManager.LoadScene("crowd.ovscene"),"restore threaded interaction scene");
    context.sceneManager.GetCurrentScene()->Play();
    std::puts("PASS: threaded product picking/material drop retains its readback after newer recording, rejects double application and replaced scenes");
}
#ifndef EDITOR_THREAD_TEST_MAIN
#define EDITOR_THREAD_TEST_MAIN main
#endif
int EDITOR_THREAD_TEST_MAIN(int argc,char** argv) try
{
    producer=std::this_thread::get_id();
    if (argc > 2 && std::string(argv[2]) == "--capture")
    {
        std::puts("TRACE READY"); std::fflush(stdout);
        const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while (!tracy::GetProfiler().IsConnected() && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Require(tracy::GetProfiler().IsConnected(), "Tracy capture did not connect");
    }

    if(const auto result=EditorFixtureMain(argc,argv))return result;
    const auto project=std::filesystem::path(argv[1])/"SkinningProject";
    std::vector<std::vector<std::vector<uint8_t>>> expected;
    std::vector<std::string> poses;
    for(bool threaded:{false,true})
    {
        SetEditorThreadMode(project,threaded);
        OvEditor::Core::Application app(project);
        auto& editor=app.m_editor;auto& context=app.m_context;
        Require(bool(context.renderThread)==threaded,"Editor ignored thread config");
        Require(threaded?!glfwGetCurrentContext():bool(glfwGetCurrentContext()),"Editor context owner incorrect");
        ViewsFixture fixture(editor,context);
        std::vector<std::vector<uint8_t>> captured;
        std::vector<baregl::Framebuffer::Snapshot> targets;
        OwnerChecks checks(context);
        beforePresent=[&]{
            captured.clear();
            for(const auto& target:targets){const auto [w,h]=target.GetSize();captured.emplace_back(size_t(w)*h*4);target.ReadRGBA8(captured.back());}
            glBindFramebuffer(GL_READ_FRAMEBUFFER,0);captured.emplace_back(960*320*4);glReadPixels(0,0,960,320,GL_RGBA,GL_UNSIGNED_BYTE,captured.back().data());
        };
        editor.m_editorActions.StartPlaying();
        for(int i=0;i<16;++i)
        {
            // Deterministic image oracle intentionally drains; overlap is checked separately.
            OnOwner(context,[](auto& driver){driver.OnFrameCompleted();glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);glClearColor(.08f,.12f,.18f,1);glClear(GL_COLOR_BUFFER_BIT);});
            editor.PreUpdate();editor.Update(.02f);
            const auto pose=SceneState(*context.sceneManager.GetCurrentScene());
            if(threaded)Require(pose==poses.at(i),"Editor threaded pose mismatch");else poses.push_back(pose);
            targets.clear();for(auto* view:fixture.views)targets.push_back(view->m_framebuffer.RecordTarget());
            auto interaction=editor.m_recordedInteraction;auto done=editor.m_pendingFrame->GetCompletion();
            editor.PostUpdate();editor.DrainRenderFrames();
            Require(done.Succeeded()&&interaction&&interaction->consumed,"Editor successful frame lost interaction");
            if(threaded)Require(captured==expected.at(i),"Editor threaded view/UI pixel mismatch");else expected.push_back(captured);
        }
        OnOwner(context,[](auto&){beforePresent={};});
        if(threaded)
        {
            VerifyEditorOverlap(editor,context);
            VerifyEditorInteraction(editor,context);
            for(int i=0;i<64;++i){editor.PreUpdate();mainUpdating=true;editor.Update(.02f);mainUpdating=false;editor.PostUpdate();}
            editor.DrainRenderFrames();
            Require(overlappingDraws>0,"Editor did not naturally overlap update and draw");
            RejectProductCall([&]{editor.SetFrameSubmissionEnabled(false);},"threaded Editor accepted immediate mode");
            auto presented=std::make_shared<std::atomic_int>(0);
            const auto listener=OvDebug::Logger::LogEvent += [&](const auto& log){if(log.message=="threaded editor stop"){Require(std::this_thread::get_id()==producer,"Editor close left Main");context.window->SetShouldClose(true);}};
            struct Unsubscribe{decltype(listener) token;~Unsubscribe(){OvDebug::Logger::LogEvent-=token;}} unsubscribe{listener};
            OnOwner(context,[&](auto&){beforePresent=[presented]{if(++*presented==4)OVLOG_INFO("threaded editor stop");};});
            app.Run();Require(*presented>=4&&*presented<=8&&editor.m_inFlightFrames.empty(),"Editor Application.Run did not drain on close");
            OnOwner(context,[](auto&){beforePresent={};});
        }
    }
    size_t transitions=0;for(size_t i=1;i<expected.size();++i)if(expected[i][1]!=expected[i-1][1])++transitions;
    Require(transitions>0,"Editor comparison did not animate");
    for(int cycle=0;cycle<4;++cycle)
    {
        RenderFrame::Completion submitted,abandoned;baregl::Texture::Snapshot survivor;
        {
            OvEditor::Core::Application app(project);auto& editor=app.m_editor;auto& context=app.m_context;
            ViewsFixture fixture(editor,context);OwnerChecks checks(context);
            for(int i=0;i<8;++i){editor.PreUpdate();editor.Update(.02f);submitted=editor.m_pendingFrame->GetCompletion();editor.PostUpdate();}
            editor.Update(.02f);abandoned=editor.m_pendingFrame->GetCompletion();
            survivor=fixture.views[1]->m_framebuffer.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR)->RetainStorage();
        }
        Require(submitted.Succeeded()&&abandoned.IsRetired()&&!abandoned.Succeeded()&&!survivor.GetID(),"Editor restart lost drain/cancel/resource invalidation");
        Require(!glfwGetCurrentContext(),"Editor exit left Main owning GL");
    }
    {
        OvEditor::Core::Application app(project);auto& editor=app.m_editor;auto& context=app.m_context;
        ViewsFixture fixture(editor,context);OwnerChecks checks(context);
        OnOwner(context,[](auto&){beforePresent=[]{throw std::runtime_error("Editor presentation failure");};});
        editor.Update(.02f);auto done=editor.m_pendingFrame->GetCompletion();auto input=editor.m_recordedInteraction;
        editor.PostUpdate();bool failed=false;try{editor.DrainRenderFrames();}catch(const std::exception&){failed=true;}
        Require(failed&&done.IsRetired()&&!done.Succeeded()&&!input->consumed,"Editor failed frame published picking or lost error");
        OnOwner(context,[](auto&){beforePresent={};});
    }
    std::printf("PASS: 16 exact Editor pose/three-view/UI pairs, ordered interactions, 4 application restarts and presentation failure; %zu moving transitions, %zu draws overlap Main Update, %zu owner GPU timestamps\n",transitions,overlappingDraws.load(),queries.load());
    return 0;
}
catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
