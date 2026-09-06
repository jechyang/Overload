#define EDITOR_THREAD_TEST_MAIN EditorThreadFixtureMain
#include "editor_thread_validation.cpp"
#undef EDITOR_THREAD_TEST_MAIN
#include "window_minimize_validation.inl"

void TinyViewValidation(const std::filesystem::path& project)
{
    std::vector<std::vector<uint8_t>> expected;
    const std::array<std::pair<uint16_t,uint16_t>,5> sizes{{{1,1},{1,64},{64,1},{4,3},{32,5}}};
    for(bool threaded:{false,true})
    {
        SetEditorThreadMode(project,threaded);
        OvEditor::Core::Application app(project);auto& editor=app.m_editor;auto& context=app.m_context;
        ViewsFixture fixture(editor,context);OwnerChecks checks(context);
        size_t index=0;
        for(const auto [w,h]:sizes)
        {
            fixture.views[0]->SetSize({float(w),float(h)+ImGui::GetFrameHeight()});
            editor.Update(0);auto target=fixture.views[0]->m_framebuffer.RecordTarget();
            Require(target.GetSize()==std::pair{w,h},"tiny view did not exercise requested dimensions");
            editor.PostUpdate();editor.DrainRenderFrames();
            std::vector<uint8_t> pixels(size_t(w)*h*4);OnOwner(context,[&](auto&){target.ReadRGBA8(pixels);Require(glGetError()==GL_NO_ERROR,"tiny Bloom GL error");});
            if(threaded)Require(pixels==expected.at(index),"tiny Bloom threaded image differs");else expected.push_back(std::move(pixels));
            ++index;
        }
    }
    std::puts("PASS: five tiny/thin actual Asset View Bloom targets (including 1x1) match across Main/Render contexts");
}
void QueuedGizmoValidation(const std::filesystem::path& project)
{
    OvEditor::Core::Application app(project);auto& editor=app.m_editor;auto& context=app.m_context;
    ViewsFixture fixture(editor,context);OwnerChecks checks(context);
    auto& view=*static_cast<OvEditor::Panels::SceneView*>(fixture.views[2]);auto& actions=editor.m_editorActions;
    view.m_hovered=true;editor.Update(0);auto pick=editor.m_recordedInteraction;editor.PostUpdate();editor.DrainRenderFrames();
    auto& scene=*context.sceneManager.GetCurrentScene();uint64_t selected=0;
    const auto [w,h]=pick->readback->GetSize();
    for(uint32_t y=0;y<h&&!selected;++y)for(uint32_t x=0;x<w&&!selected;++x)
        if(auto result=pick->readback->Resolve(scene,x,y))if(auto* actor=std::get_if<OvTools::Utils::OptRef<OvCore::ECS::Actor>>(&*result))
            if(actor->value().GetComponent<CMaterialRenderer>())selected=actor->value().GetGUID();
    Require(selected!=0,"queued Gizmo has no visible actor");
    auto& actor=*scene.FindActorByGUID(selected);actions.SelectActor(actor);
    view.m_hovered=true;editor.Update(0);auto reference=editor.m_recordedInteraction;editor.PostUpdate();editor.DrainRenderFrames();
    std::optional<OvEditor::Core::GizmoBehaviour::EDirection> axis;OvMaths::FVector2 mouse;
    for(uint32_t y=0;y<h&&!axis;++y)for(uint32_t x=0;x<w&&!axis;++x)
        if(auto result=reference->readback->Resolve(scene,x,y))if(auto* direction=std::get_if<OvEditor::Core::GizmoBehaviour::EDirection>(&*result))
        {axis=*direction;mouse={float(x),float(h-y)};}
    Require(axis.has_value(),"queued Gizmo has no visible axis");
    const auto origin=actor.transform.GetWorldPosition();const auto rotation=actor.transform.GetWorldRotation();const auto scale=actor.transform.GetWorldScale();
    const auto moved=mouse+OvMaths::FVector2{18,7};
    OvEditor::Core::GizmoBehaviour oracle;oracle.StartPicking(actor,reference->cameraPosition,reference->operation,*axis);
    oracle.SetCurrentMouse(mouse);oracle.ApplyOperation(reference->view,reference->projection,reference->cameraPosition,reference->size,reference->snapping);
    oracle.SetCurrentMouse(moved);oracle.ApplyOperation(reference->view,reference->projection,reference->cameraPosition,reference->size,reference->snapping);
    const auto expectedPosition=actor.transform.GetWorldPosition();const auto expectedRotation=actor.transform.GetWorldRotation();const auto expectedScale=actor.transform.GetWorldScale();
    actor.transform.SetWorldPosition(origin);actor.transform.SetWorldRotation(rotation);actor.transform.SetWorldScale(scale);
    Gate gate;List commands;commands.Append([&](auto&){gate.Block();});commands.Seal();RenderFrame blocker;blocker.Append(std::move(commands));blocker.Seal();
    context.renderThread->Submit(std::move(blocker));
    struct DrainGate{Gate& gate;OvRendering::Threading::RenderThread& owner;~DrainGate(){ {std::lock_guard lock(gate.mutex);gate.released=true;gate.changed.notify_all();}owner.WaitIdle();}} drain{gate,*context.renderThread};
    {std::unique_lock lock(gate.mutex);Require(gate.changed.wait_for(lock,std::chrono::seconds(5),[&]{return gate.entered;}),"Gizmo gate not entered");}
    std::jthread rescue([&]{std::unique_lock lock(gate.mutex);gate.timeout=!gate.changed.wait_for(lock,std::chrono::seconds(5),[&]{return gate.producerDone;});gate.released=true;gate.changed.notify_all();});
    context.window->MouseButtonPressedEvent.Invoke(GLFW_MOUSE_BUTTON_LEFT);view.m_hovered=true;editor.Update(0);auto press=editor.m_recordedInteraction;
    Require(press->pressed&&!press->released,"product failed to capture press event");press->mouse=mouse;press->resizing=false;press->hovered=true;
    editor.PostUpdate();Require(!context.inputManager->IsMouseButtonPressed(OvWindowing::Inputs::EMouseButton::MOUSE_BUTTON_LEFT),"press event was not cleared");
    view.m_hovered=true;editor.Update(0);auto drag=editor.m_recordedInteraction;drag->mouse=moved;drag->hovered=true;drag->resizing=false;
    const bool pending=!press->consumed&&!drag->consumed&&!drag->pressed;
    {std::lock_guard lock(gate.mutex);gate.producerDone=true;gate.changed.notify_all();}rescue.join();
    Require(!gate.timeout&&pending,"Gizmo press and drag were not independently captured in flight");
    const auto camera=view.GetCamera()->GetPosition();view.GetCamera()->SetPosition({901,902,903});editor.PostUpdate();
    context.window->MouseButtonReleasedEvent.Invoke(GLFW_MOUSE_BUTTON_LEFT);editor.Update(0);auto release=editor.m_recordedInteraction;
    Require(release->released&&!release->pressed,"product failed to capture release event");view.GetCamera()->SetPosition(camera);
    editor.PostUpdate();editor.DrainRenderFrames();
    Require(press->consumed&&drag->consumed&&release->consumed&&!view.m_gizmoOperations.IsPicking(),"queued Gizmo input order or release failed");
    Require(OvMaths::FVector3(actor.transform.GetWorldPosition())==expectedPosition&&OvMaths::FQuaternion(actor.transform.GetWorldRotation())==expectedRotation&&OvMaths::FVector3(actor.transform.GetWorldScale())==expectedScale,"queued Gizmo differs from captured-input oracle");
    std::puts("PASS: actual product press/drag/release across in-flight frames matches Gizmo oracle despite newer camera state");
}
int main(int argc,char** argv) try
{
    if(const auto result=EditorThreadFixtureMain(argc,argv))return result;
    const auto project=std::filesystem::path(argv[1])/"SkinningProject";
    TinyViewValidation(project);
    QueuedGizmoValidation(project);
    OvEditor::Core::Application app(project);auto& editor=app.m_editor;auto& context=app.m_context;
    OwnerChecks checks(context);
    context.uiManager->EnableEditorLayoutSave(false);
    OnOwner(context,[&](auto&){context.device->SetVsync(false);});
    Require(context.renderThread&&!glfwGetCurrentContext(),"full Editor did not use Render owner");
    Require(context.sceneManager.LoadScene("crowd.ovscene"),"full Editor scene load");
    auto& actions=editor.m_editorActions;
    auto& sceneView=editor.m_panelsManager.GetPanelAs<OvEditor::Panels::SceneView>("Scene View");
    auto& gameView=editor.m_panelsManager.GetPanelAs<OvEditor::Panels::GameView>("Game View");
    auto& assetView=editor.m_panelsManager.GetPanelAs<OvEditor::Panels::AssetView>("Asset View");
    assetView.SetResource(OvEditor::Panels::AssetView::ViewableResource{context.modelManager.GetResource(":Models/Mannequin.fbx")});
    editor.m_panelsManager.GetPanelAs<OvEditor::Panels::MaterialEditor>("Material Editor").SetTarget(*context.materialManager.GetResource(":Materials/Default.ovmat"));
    editor.m_panelsManager.GetPanelAs<OvEditor::Panels::AssetProperties>("Asset Properties").SetTarget(":Models/Mannequin.fbx");
    std::vector<OvUI::Panels::PanelWindow*> windows;
    for(auto& [name,panel]:editor.m_panelsManager.m_panels)
        if(auto* window=dynamic_cast<OvUI::Panels::PanelWindow*>(panel.get()))
        {windows.push_back(window);window->Open();window->dockable=false;window->autoSize=false;window->SetPosition({float(windows.size()%4*180),float(windows.size()%3*100)+20});window->SetSize({320,260});}
    // The real complete editor canvas remains installed, including menu/toolbar.
    std::vector<RenderFrame::Completion> frames;
    size_t delayed=0,visible=0;
    for(int i=0;i<96;++i)
    {
        if(i%8==0)
        {
            context.window->SetSize(uint16_t(800+(i%3)*80),uint16_t(600+(i%5)*20));
            for(auto* view:std::array<OvEditor::Panels::AView*,3>{&assetView,&gameView,&sceneView})view->SetSize({float(288+(i%5)*8),float(240+(i%3)*10)});
            if(actions.GetCurrentEditorMode()!=OvEditor::Core::EditorActions::EEditorMode::EDIT)actions.StopPlaying();
            Require(context.sceneManager.LoadScene(i%16?"fragmented.ovscene":"crowd.ovscene"),"full UI scene switch");
            actions.SelectActor(*context.sceneManager.GetCurrentScene()->GetActors().back());
            actions.StartPlaying();
        }
        if(i%8==3)actions.PauseGame();
        if(i%8==4)actions.NextFrame();
        if(i%8==5)actions.StartPlaying();
        auto* active=windows.at(size_t(i)%windows.size());active->Open();active->Focus();
        if(i%12==6)assetView.Close();if(i%12==7)assetView.Open();
        editor.PreUpdate();mainUpdating=true;editor.Update(.02f);mainUpdating=false;
        frames.push_back(editor.m_pendingFrame->GetCompletion());
        editor.m_editorActions.DelayAction([&]{++delayed;},0);
        context.window->KeyPressedEvent.Invoke(int(OvWindowing::Inputs::EKey::KEY_SPACE));
        editor.PostUpdate();
        Require(delayed==size_t(i+1),"full UI delayed actions did not follow Main frame cadence");
        Require(!context.inputManager->IsKeyPressed(OvWindowing::Inputs::EKey::KEY_SPACE),"full UI input events persisted");
        if(active->IsVisible())++visible;
    }
    editor.DrainRenderFrames();
    for(const auto& frame:frames)Require(frame.Succeeded(),"full UI frame failed");
    Require(visible>80&&editor.m_inFlightFrames.empty(),"full UI panels missing or queue did not drain");
    WindowMinimizeValidation(editor,context);
    OnOwner(context,[](auto&){Require(glGetError()==GL_NO_ERROR,"full UI GL error");});
    std::printf("PASS: 96 actual full-canvas Editor frames, %zu visible panel visits, 12 scene/window/view changes, play/pause/step/resume, ordered Main delayed actions and owner GL checks\n",visible);
    return 0;
}
catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
