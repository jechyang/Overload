#pragma once
#include <OvEditor/Core/SelectionBounds.h>
#include <OvEditor/Core/GizmoMath.h>
#include <OvEditor/Rendering/PickingCollector.h>
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvEditor/Settings/EditorSettings.h>
#include <OvRendering/Context/Driver.h>
#include "Tests/Scripting/Integration/editor_gizmo_checks.h"

static void ValidateViewport(OvEditor::Core::Context& context, OvEditor::Core::EditorActions& actions)
{
    using namespace OvMaths;
    using namespace OvEditor::Core;
    using namespace OvCore::ECS::Components;
    using OvEditor::Settings::EditorSettings;
    context.sceneManager.LoadEmptyScene();
    auto& scene = *context.sceneManager.GetCurrentScene();
    EditorGizmoChecks(scene);
    auto& a = scene.CreateActor("Primary cube"); auto& b = scene.CreateActor("Secondary cube");
    auto& hidden = scene.CreateActor("Occluded cube");
    for (auto* actor : {&a,&b,&hidden})
    {
        actor->AddComponent<CModelRenderer>().SetModel(context.editorResources->GetModel("Cube"));
        actor->AddComponent<CMaterialRenderer>().FillWithMaterial(*context.materialManager[":Materials\\Default.ovmat"]);
    }
    a.transform.SetWorldPosition({-3,0,0}); b.transform.SetWorldPosition({3,0,0}); hidden.transform.SetWorldPosition({-3,0,-4});
    hidden.transform.SetWorldPosition({-4,0,-4}); hidden.transform.SetWorldScale({.2f,.2f,.2f});
    actions.SetSelection({&a,&b}); actions.ClearHistory();
    auto& view = EDITOR_PANEL(OvEditor::Panels::SceneView,"Scene View");
    view.dockable = false; view.autoSize = false; view.SetPosition({0,0}); view.SetSize({960,700});
    auto& camera = *view.GetCamera(); auto& controller = view.GetCameraController();
    camera.SetProjectionMode(OvRendering::Settings::EProjectionMode::PERSPECTIVE);
    controller.SetPosition({0,0,12}); controller.SetRotation(FQuaternion::LookAt({0,0,-1},FVector3::Up));
    OvUI::Modules::Canvas canvas; canvas.AddPanel(view); context.uiManager->SetCanvas(canvas);
    auto execute = [&](auto& commands, bool clear = false) {
        auto run = [&](auto& driver) { if (clear) { glBindFramebuffer(GL_FRAMEBUFFER,0); glDisable(GL_SCISSOR_TEST); glClearColor(.08f,.08f,.08f,1); glClear(GL_COLOR_BUFFER_BIT); } commands.Execute(driver); };
        if (context.renderThread) context.renderThread->Invoke(run); else run(*context.driver);
    };
    auto draw = [&] {
        auto render = view.RecordRenderCommands(); execute(render);
        auto ui = context.uiManager->RecordRenderCommands(); execute(ui,true);
    };
    draw(); draw();
    view.SetLocalSpace(false); view.SetCenterPivot(true);
    Require(FVector3::Distance(view.GetGizmoFrame().position,{0,0,0}) < .001f,"center pivot uses combined selection bounds");
    a.transform.SetWorldRotation(FQuaternion({0,0,90}));
    view.SetLocalSpace(true);
    Require(FVector3::Distance(view.GetGizmoFrame().rotation * FVector3::Right,FVector3::Up) < .001f,"local gizmo follows primary rotation");
    view.SetLocalSpace(false);
    Require(FVector3::Distance(view.GetGizmoFrame().rotation * FVector3::Right,FVector3::Right) < .001f,"world gizmo remains aligned to world axes");

    const FVector2 size{1280,720}; const auto ortho = FMatrix4::CreateOrthographic(5,size.x/size.y,.1f,1000);
    GizmoBehaviour gizmo; const GizmoBehaviour::SnapSettings noSnap{false,1,15,.1f};
    const auto originalPosition = a.transform.GetWorldPosition();
    a.transform.SetWorldPosition({-2,0,-10}); b.transform.SetWorldPosition({2,0,-10});
    auto apply = [&](FVector2 mouse) { gizmo.SetCurrentMouse(mouse); actions.TransformSelection([&] { gizmo.ApplyOperation(FMatrix4::Identity,ortho,{},size,noSnap); },{FVector3{0,0,-10}},FQuaternion::Identity); };
    actions.BeginTransformEdit();
    gizmo.StartPicking(a,{},EGizmoOperation::TRANSLATE,GizmoBehaviour::EDirection::X,GizmoBehaviour::Frame{{0,0,-10},FQuaternion::Identity});
    apply({640,360}); apply({712,360});
    Require(FVector3::Distance(a.transform.GetWorldPosition(),{-1,0,-10}) < .001f && FVector3::Distance(b.transform.GetWorldPosition(),{3,0,-10}) < .001f,
        "world-axis group translation ignores rotated primary axes");
    gizmo.CancelPicking(); actions.EndTransformEdit(true); actions.CommitSceneEdit(true);
    actions.BeginTransformEdit();
    gizmo.StartPicking(a,{},EGizmoOperation::ROTATE,GizmoBehaviour::EDirection::Z,GizmoBehaviour::Frame{{0,0,-10},FQuaternion::Identity});
    apply({712,360}); apply({640,288});
    Require(FVector3::Distance(a.transform.GetWorldPosition(),{0,-2,-10}) < .001f && FVector3::Distance(b.transform.GetWorldPosition(),{0,2,-10}) < .001f,
        "center rotation orbits primary and secondary symmetrically");
    gizmo.CancelPicking(); actions.EndTransformEdit(true); actions.CommitSceneEdit(true);
    a.transform.SetWorldRotation(FQuaternion::Identity);
    actions.BeginTransformEdit();
    gizmo.StartPicking(a,{},EGizmoOperation::SCALE,GizmoBehaviour::EDirection::X,GizmoBehaviour::Frame{{0,0,-10},FQuaternion::Identity});
    apply({640,360}); apply({740,360});
    Require(FVector3::Distance(a.transform.GetWorldPosition(),{-4,0,-10}) < .001f && FVector3::Distance(b.transform.GetWorldPosition(),{4,0,-10}) < .001f,
        "center scale expands both sides around the shared pivot");
    gizmo.CancelPicking(); actions.EndTransformEdit(true); actions.CommitSceneEdit(true);
    Require(!actions.CanUndo(),"cancelled viewport transform gestures leave no undo entries");
    a.transform.SetWorldPosition(originalPosition); b.transform.SetWorldPosition({3,0,0});

    auto& child = scene.CreateActor("Offset child"); child.SetParent(a); child.transform.SetWorldPosition({18,7,0});
    auto bounds = SelectionBounds::From({&a,&b,&child});
    Require(bounds.max.x >= 18 && bounds.max.y >= 7,"selection bounds include offset children without double counting");
    view.FrameSelection();
    for (int i=0;i<150;++i) controller.HandleInputs(.016f);
    camera.CacheMatrices(960,680);
    const auto center = GizmoMath::Project(bounds.Center(),camera.GetViewMatrix(),camera.GetProjectionMatrix(),{960,680});
    Require(FVector2::Length(center-FVector2{480,340}) < .2f,"frame selection centers the complete group, including distant children");
    view.ToggleProjection(); view.FrameSelection();
    for (int i=0;i<150;++i) controller.HandleInputs(.016f);
    Require(camera.GetSize() >= bounds.Radius(),"orthographic frame selection fits the complete bounds");
    scene.DestroyActor(child); camera.SetProjectionMode(OvRendering::Settings::EProjectionMode::PERSPECTIVE);
    controller.SetPosition({0,0,12}); controller.SetRotation(FQuaternion::LookAt({0,0,-1},FVector3::Up));
    actions.UnselectActor(); draw(); draw();

    const auto [width,height] = view.GetSafeSize();
    OvEditor::Rendering::PickingCollector picking;
    OvEditor::Rendering::EditorOverlayRequest overlay; overlay.scene = scene.GetLifetimeToken();
    OvCore::Rendering::SceneRenderRequest request{{scene,camera,width,height}};
    auto pickCommands = picking.Extract(request,overlay,context.driver->CreatePipelineState(),nullptr); pickCommands.Seal(); execute(pickCommands);
    const auto readback = picking.GetReadback();
    auto visible = readback->ResolveRegion(scene,0,0,width,height);
    Require(visible.size() == 2 && std::find(visible.begin(),visible.end(),&hidden) == visible.end(),"marquee readback includes visible geometry and excludes an occluded object");
    auto event = [&](FVector2 mouse, bool pressed, bool released, bool cancel = false, bool additive = false) {
        draw(); auto pending = view.GetRecordedInteraction();
        pending->hovered = true; pending->pickingAvailable = true; pending->readback = readback;
        pending->mouse = mouse; pending->pressed = pressed; pending->released = released; pending->cancelled = cancel; pending->additiveSelection = additive;
        Require(view.CompleteInteraction(pending),"viewport consumes completed selection interaction");
    };
    event({10,100},true,false);
    Require(view.IsSelecting(),"empty-space press starts marquee without clearing selection early");
    event({float(width-10),float(height-10)},false,false);
    event({float(width-10),float(height-10)},false,true);
    Require(actions.GetSelectedActors().size() == 2 && actions.IsActorSelected(a) && actions.IsActorSelected(b),"drag release selects all visible objects in the rectangle");
    event({10,100},true,false); event({100,200},false,false,true);
    Require(!view.IsSelecting() && actions.GetSelectedActors().size() == 2,"Escape cancels marquee and preserves the original selection");
    event({10,100},true,false);
    context.window->LostFocusEvent.Invoke();
    Require(!view.IsSelecting() && actions.GetSelectedActors().size() == 2,"losing window focus cancels the rectangle without changing selection");
    actions.SetSelection({&hidden}); event({10,100},true,false,false,true); event({float(width-10),float(height-10)},false,true,false,true);
    Require(actions.GetSelectedActors().size() == 3,"Ctrl marquee adds visible objects to the existing selection");
    event({10,100},true,false); event({10,100},false,true);
    Require(actions.GetSelectedActors().empty(),"empty click clears selection on release");

    actions.SetSelection({&a,&b}); view.SetCenterPivot(true); view.SetLocalSpace(false); draw(); draw();
    auto stale = view.GetRecordedInteraction(); view.SetLocalSpace(true);
    stale->pressed = true; stale->hovered = true;
    Require(!view.CompleteInteraction(stale) && actions.GetSelectedActors().size() == 2,"changing gizmo space discards stale asynchronous picking");
    auto& io = ImGui::GetIO(); const auto flags = io.BackendFlags;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),true); io.AddFocusEvent(true);
    auto click = [&](float x,float y) { io.AddMousePosEvent(x,y); draw(); draw(); io.AddMouseButtonEvent(0,true); draw(); io.AddMouseButtonEvent(0,false); draw(); };
    const auto position = view.GetPosition(); const float top = position.y+ImGui::GetFrameHeight();
    click(position.x+92,top+24);
    Require(view.GetGizmoOperation() == EGizmoOperation::ROTATE,"viewport Rotate button changes the active gizmo through real ImGui input");
    click(position.x+212,top+24);
    Require(!view.IsLocalSpace(),"viewport space toggle changes the transform frame");
    click(position.x+280,top+24);
    Require(!view.IsCenterPivot(),"viewport pivot toggle changes the actual pivot mode");
    const bool oldSnap = EditorSettings::SnapEnabled.Get(); click(position.x+25,top+ImGui::GetFrameHeight()+25);
    Require(EditorSettings::SnapEnabled.Get() != oldSnap,"viewport Snap checkbox toggles persistent snapping");
    view.SetSize({400,700}); draw(); draw();
    click(position.x+212,top+24);
    Require(view.IsLocalSpace(),"narrow viewport keeps transform controls clickable");
    const auto projectionBefore = camera.GetProjectionMode();
    click(position.x+400-164+42,top+16+(3*ImGui::GetFrameHeight()+26)+131);
    Require(camera.GetProjectionMode() != projectionBefore,"narrow viewport moves orientation controls below the transform toolbar");
    camera.SetProjectionMode(projectionBefore); view.SetLocalSpace(false);
    view.SetSize({960,700}); io.AddMousePosEvent(-10000,-10000);
    // PanelWindow applies a requested resize during UI drawing; allow the next
    // scene capture to use that committed size before taking the screenshot.
    for (int i=0;i<4;++i) draw();
    auto capture = [&] {
        auto [w,h] = context.window->GetFramebufferSize(); std::vector<uint8_t> pixels(size_t(w)*h*4);
        glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream output(context.renderThread ? "viewport-thread.ppm" : "viewport.ppm",std::ios::binary);
        output << "P6\n" << w << " " << h << "\n255\n";
        for (int y=h-1;y>=0;--y) for (int x=0;x<w;++x) output.write(reinterpret_cast<const char*>(pixels.data()+(size_t(y)*w+x)*4),3);
        Require(output.good(),"multi-selection viewport and tools rendered to screenshot");
    };
    if (context.renderThread) context.renderThread->Invoke([&](auto&) {capture();}); else capture();
    EditorSettings::SnapEnabled = oldSnap; io.BackendFlags = flags;
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),false);
    view.SetLocalSpace(true); view.SetCenterPivot(false); view.SetGizmoOperation(EGizmoOperation::TRANSLATE);
    context.uiManager->RemoveCanvas(); context.sceneManager.LoadEmptyScene();
}
