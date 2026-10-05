#include "product_loop_validation.inl"
#include <OvUI/Plugins/DDTarget.h>
#include <unordered_set>
void EditorProductValidation(OvEditor::Core::Editor& editor,OvEditor::Core::Context& context)
{
    auto& view=editor.m_panelsManager.GetPanelAs<OvEditor::Panels::GameView>("Game View");
    std::vector<std::string> states;std::vector<std::vector<uint8_t>> images;
    for(bool owned:{false,true})
    {
        editor.SetFrameSubmissionEnabled(owned);Require(context.sceneManager.LoadScene("crowd.ovscene"),"product loop scene reload");
        editor.m_editorActions.StartPlaying();int delayed=0;ProductFenceHook hook;
        for(int i=0;i<8;++i)
        {
            editor.m_editorActions.DelayAction([&]{++delayed;},0);
            editor.PreUpdate();editor.Update(.02f);
            const auto state=SceneState(*context.sceneManager.GetCurrentScene());
            if(owned){Require(state==states[i],"product Editor advanced simulation differently");Require(delayed==i,"Editor delayed action ran before owned frame completion");
                RejectProductCall([&]{editor.Update(.02f);},"double Editor update accepted");
                RejectProductCall([&]{editor.SetFrameSubmissionEnabled(false);},"Editor mode switched mid-frame");}
            else states.push_back(state);
            context.window->KeyPressedEvent.Invoke(int(OvWindowing::Inputs::EKey::KEY_SPACE));
            auto completion=owned?editor.m_pendingFrame->GetCompletion():OvRendering::Data::RenderFrame::Completion{};
            editor.PostUpdate();Require(delayed==i+1,"Editor delayed action ran an unexpected number of times");
            Require(!context.inputManager->IsKeyPressed(OvWindowing::Inputs::EKey::KEY_SPACE),"Editor events survived PostUpdate");
            if(owned)Require(completion.Succeeded()&&!editor.m_pendingFrame,"Editor frame did not retire exactly once");
            const auto [w,h]=view.GetSafeSize();std::vector<uint8_t> pixels(size_t(w)*h*4);view.GetOutputTarget().ReadRGBA8(pixels);
            if(owned)Require(pixels==images[i],"product Editor Game view image differs");else images.push_back(std::move(pixels));
        }
        // Both modes execute the same product graph; immediate mode must also
        // retire its transient leases before another frame can reuse them.
        Require(ProductFenceHook::count==8,"Editor did not create exactly one retirement fence per product frame");
        editor.m_editorActions.StopPlaying();
    }
    Require(states.front()!=states.back() && images.front()!=images.back(),"Editor product sequence did not animate");
    editor.SetFrameSubmissionEnabled(false);
    std::puts("PASS: real Editor Update/PostUpdate modes: 8 exact moving pose/view pairs, one fence/frame, event clearing, delayed actions, duplicate/mid-frame switch rejection");
}

void EditorInteractionValidation(OvEditor::Core::Editor& editor,OvEditor::Core::Context& context)
{
    using View=OvEditor::Panels::SceneView;using Picking=OvEditor::Rendering::PickingRenderPass;
    auto& view=editor.m_panelsManager.GetPanelAs<View>("Scene View");
    auto& actions=editor.m_editorActions;actions.UnselectActor();view.m_hovered=false;view.SetCommandRecordingEnabled(false);
    {EditorReadHook hook;view.Render();Require(editorReadCalls==0,"unhovered immediate view performed GPU picking readback");}
    view.m_hovered=true;
    auto commands=view.RecordRenderCommands();auto input=view.GetRecordedInteraction();Require(input&&input->readback,"interaction fixture has no picking image");
    Require(!view.CompleteInteraction(input),"interaction consumed before GPU commands");commands.Execute(*context.driver);
    auto& scene=*context.sceneManager.GetCurrentScene();uint64_t picked=0;
    const auto [w,h]=input->readback->GetSize();
    for(uint32_t y=0;y<h&&!picked;++y)for(uint32_t x=0;x<w&&!picked;++x)
        if(auto result=input->readback->Resolve(scene,x,y))if(auto* actor=std::get_if<OvTools::Utils::OptRef<OvCore::ECS::Actor>>(&*result))
            if(actor->value().GetComponent<CMaterialRenderer>()){picked=actor->value().GetGUID();input->mouse={float(x),float(h-y)};}
    if(!picked) {
        const auto p=view.GetCamera()->GetPosition();
        std::unordered_set<uint32_t> colors;for(size_t i=0;i<input->readback->pixels.size();i+=4)colors.insert(uint32_t(input->readback->pixels[i])|uint32_t(input->readback->pixels[i+1])<<8|uint32_t(input->readback->pixels[i+2])<<16);
        std::printf("PICKING DIAG: %ux%u camera %f,%f,%f ready=%d scene-match=%d actors=%zu colors=%zu\n",w,h,p.x,p.y,p.z,input->readback->IsReady(),input->readback->scene==scene.GetLifetimeToken(),input->readback->actors.size(),colors.size());
        for(auto color:colors)std::printf("PICKING COLOR: %u\n",color);
    }
    Require(picked!=0,"interaction fixture contains no actor pixel");input->pressed=true;input->hovered=true;input->resizing=false;
    view.m_image->GetPlugin<OvUI::Plugins::DDTarget<std::pair<std::string, OvUI::Widgets::Layout::Group*>>>()->DataReceivedEvent.Invoke({":Materials/Default.ovmat",nullptr});Require(input->materialDrop.has_value(),"material drop was not retained with frame");
    auto* renderer=scene.FindActorByGUID(picked)->GetComponent<CMaterialRenderer>();renderer->RemoveMaterialAtIndex(0);
    // A newer pass and UI geometry must not replace the first frame's readback/coordinates.
    view.m_hovered=true;auto newer=view.RecordRenderCommands();auto newerInput=view.GetRecordedInteraction();newer.Cancel();
    view.m_hovered=false;const auto oldPosition=view.GetPosition();view.SetPosition({900,700});
    Require(view.CompleteInteraction(input),"captured interaction was not consumed");
    Require(actions.IsAnyActorSelected()&&actions.GetSelectedActor().GetGUID()==picked,"interaction used latest mouse/hover/readback");
    Require(renderer->GetMaterialAtIndex(0)==context.materialManager.GetResource(":Materials/Default.ovmat"),"deferred material drop lost its actor");
    Require(!view.CompleteInteraction(input)&&!view.CompleteInteraction(newerInput),"repeated/cancelled interaction accepted");view.SetPosition(oldPosition);
    view.m_hovered=true;auto gizmoCommands=view.RecordRenderCommands();auto gizmoInput=view.GetRecordedInteraction();gizmoCommands.Execute(*context.driver);
    std::optional<OvEditor::Core::GizmoBehaviour::EDirection> axis;
    for(uint32_t y=0;y<h&&!axis;++y)for(uint32_t x=0;x<w&&!axis;++x)
        if(auto result=gizmoInput->readback->Resolve(scene,x,y))if(auto* direction=std::get_if<OvEditor::Core::GizmoBehaviour::EDirection>(&*result))
        {axis=*direction;gizmoInput->mouse={float(x),float(h-y)};}
    Require(axis.has_value(),"interaction fixture contains no gizmo pixel");
    auto& actor=*scene.FindActorByGUID(picked);const auto origin=actor.transform.GetWorldPosition();
    const auto rotation=actor.transform.GetWorldRotation();const auto scale=actor.transform.GetWorldScale();
    OvEditor::Core::GizmoBehaviour oracle;oracle.StartPicking(actor,gizmoInput->cameraPosition,gizmoInput->operation,*axis);
    oracle.SetCurrentMouse(gizmoInput->mouse);oracle.ApplyOperation(gizmoInput->view,gizmoInput->projection,gizmoInput->cameraPosition,gizmoInput->size,gizmoInput->snapping);
    const auto draggedMouse=gizmoInput->mouse+OvMaths::FVector2{18,7};oracle.SetCurrentMouse(draggedMouse);
    oracle.ApplyOperation(gizmoInput->view,gizmoInput->projection,gizmoInput->cameraPosition,gizmoInput->size,gizmoInput->snapping);
    const auto expectedPosition=actor.transform.GetWorldPosition();const auto expectedRotation=actor.transform.GetWorldRotation();const auto expectedScale=actor.transform.GetWorldScale();
    actor.transform.SetWorldPosition(origin);actor.transform.SetWorldRotation(rotation);actor.transform.SetWorldScale(scale);
    gizmoInput->pressed=true;gizmoInput->hovered=true;gizmoInput->resizing=false;
    Require(view.CompleteInteraction(gizmoInput)&&view.m_gizmoOperations.IsPicking(),"captured gizmo press did not start drag");
    auto drag=std::make_shared<View::Interaction>(*gizmoInput);drag->consumed=false;drag->pressed=false;drag->mouse=draggedMouse;
    const auto cameraPosition=view.GetCamera()->GetPosition();view.GetCamera()->SetPosition({901,902,903});
    Require(view.CompleteInteraction(drag),"captured gizmo drag not applied");view.GetCamera()->SetPosition(cameraPosition);
    Require(OvMaths::FVector3(actor.transform.GetWorldPosition())==expectedPosition&&OvMaths::FQuaternion(actor.transform.GetWorldRotation())==expectedRotation&&OvMaths::FVector3(actor.transform.GetWorldScale())==expectedScale,"gizmo read current camera instead of captured matrices");
    auto release=std::make_shared<View::Interaction>(*drag);release->consumed=false;release->released=true;
    Require(view.CompleteInteraction(release)&&!view.m_gizmoOperations.IsPicking(),"captured release did not stop gizmo");
    auto press=std::make_shared<View::Interaction>(*gizmoInput);press->consumed=false;Require(view.CompleteInteraction(press)&&view.m_gizmoOperations.IsPicking(),"gizmo deletion fixture did not start");
    // Deleted targets are resolved from live scene identity, never from a captured Actor pointer.
    auto deleted=std::make_shared<View::Interaction>(*input);deleted->consumed=false;deleted->pressed=false;deleted->materialDrop.reset();
    actions.DestroyActor(*scene.FindActorByGUID(picked));scene.CollectGarbages();Require(!view.m_gizmoOperations.IsPicking(),"deleted gizmo target remained active");Require(view.CompleteInteraction(deleted),"deleted-target interaction did not retire");
    Require(!view.m_highlightedActor,"deleted target remained highlighted");
    auto replaced=std::make_shared<View::Interaction>(*input);replaced->consumed=false;
    context.sceneManager.LoadEmptyScene();Require(!view.CompleteInteraction(replaced),"interaction from old scene accepted");
    Require(context.sceneManager.LoadScene("crowd.ovscene"),"restore interaction scene");
    std::puts("PASS: captured Scene View mouse/hover/readback, material drop and gizmo press/drag/release survive newer UI/pass/camera; pending/cancelled/double/deleted/replaced results guarded");
}
