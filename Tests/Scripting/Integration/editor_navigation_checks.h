#pragma once
#include <OvEditor/Panels/SceneView.h>
#include <OvUI/Modules/Canvas.h>
#include <OvUI/Styling/Style.h>
#include <fstream>
#include <imgui_impl_glfw.h>

static void CaptureNavigation(OvEditor::Core::Context& context)
{
    auto capture=[&] {
        const auto [width,height]=context.window->GetFramebufferSize();
        std::vector<uint8_t> pixels(size_t(width)*height*4);glBindFramebuffer(GL_FRAMEBUFFER,0);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream image(context.renderThread?"navigation-thread.ppm":"navigation.ppm",std::ios::binary);
        image<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for(int y=height-1;y>=0;--y)for(int x=0;x<width;++x)image.write(reinterpret_cast<const char*>(pixels.data()+(size_t(y)*width+x)*4),3);
        Require(image.good(),"scene navigation overlay renders through the real UI command path");
    };
    if(context.renderThread)context.renderThread->Invoke([&](auto&){capture();});else capture();
}

static void EditorNavigationChecks(OvEditor::Core::Context& context)
{
    using namespace OvMaths;
    using enum OvRendering::Settings::EProjectionMode;
    OvUI::Settings::PanelWindowSettings settings;settings.dockable=false;
    OvEditor::Panels::SceneView view("Scene Navigation",true,settings);
    view.autoSize=false;view.SetPosition({0,0});view.SetSize({900,650});
    auto& camera=*view.GetCamera();auto& controller=view.GetCameraController();
    auto& scene=*context.sceneManager.GetCurrentScene();
    auto& target=scene.CreateActor("Navigation target");target.transform.SetWorldPosition({2,3,4});
    controller.LockTargetActor(target);
    controller.SetPosition({2,3,-16});controller.SetRotation(FQuaternion::Identity);
    const FVector2 viewport{900,620};
    auto projectionHeight=[&] {
        camera.CacheMatrices(900,620);
        const auto& matrix=camera.GetProjectionMatrix();
        auto position=target.transform.GetWorldPosition();
        const auto a=OvEditor::Core::GizmoMath::Project(position,camera.GetViewMatrix(),matrix,viewport);
        const auto b=OvEditor::Core::GizmoMath::Project(position+FVector3::Up,camera.GetViewMatrix(),matrix,viewport);
        return std::abs(a.y-b.y);
    };
    const auto original=camera.GetPosition();const float pixels=projectionHeight();
    view.ToggleProjection();
    Require(camera.GetProjectionMode()==ORTHOGRAPHIC && std::abs(projectionHeight()-pixels)<.001f,"perspective-to-orthographic switch preserves focus scale");
    const auto size=camera.GetSize();controller.Zoom(3);
    Require(camera.GetSize()<size && FVector3::Distance(camera.GetPosition(),original)<.001f,"orthographic wheel changes size without moving the camera");
    controller.Zoom(-3);view.ToggleProjection();
    Require(camera.GetProjectionMode()==PERSPECTIVE && FVector3::Distance(camera.GetPosition(),original)<.001f,"projection round trip preserves camera position and scale");
    for(const auto direction : {FVector3::Right,-FVector3::Right,FVector3::Up,-FVector3::Up,FVector3::Forward,-FVector3::Forward}) {
        view.SetViewDirection(direction);
        const auto toward=FVector3::Normalize(target.transform.GetWorldPosition()-camera.GetPosition());
        Require(camera.GetProjectionMode()==ORTHOGRAPHIC && FVector3::Dot(toward,-direction)>.999f &&
            FVector3::Dot(camera.transform->GetWorldForward(),toward)>.999f,"axis view centres the target with a valid camera basis");
    }
    controller.UnlockTargetActor();
    auto& actions=OvCore::Global::ServiceLocator::Get<OvEditor::Core::EditorActions>();
    actions.UnselectActor();
    const auto focus=camera.GetPosition()+camera.transform->GetWorldForward()*20;
    view.SetViewDirection(FVector3::Up);
    Require(FVector3::Distance(camera.GetPosition()+camera.transform->GetWorldForward()*20,focus)<.001f,"axis view without selection preserves the previous focus point");
    controller.SetRotation(FQuaternion({25,135,0}));
    controller.SetPosition(focus-camera.transform->GetWorldForward()*20);
    auto& ui=*context.uiManager;OvUI::Modules::Canvas canvas;canvas.AddPanel(view);ui.SetCanvas(canvas);ui.EnableEditorLayoutSave(false);
    auto execute=[&](auto& commands,bool clear=false) {
        auto run=[&](auto& driver) {
            if(clear) {glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);glClearColor(.08f,.08f,.08f,1);glClear(GL_COLOR_BUFFER_BIT);}
            commands.Execute(driver);
        };
        if(context.renderThread)context.renderThread->Invoke(run);else run(*context.driver);
    };
    auto draw=[&] {
        auto render=view.RecordRenderCommands();execute(render);
        auto commands=ui.RecordRenderCommands();execute(commands,true);
    };
    for(int i=0;i<3;++i)draw();
    auto& io=ImGui::GetIO();const auto backendFlags=io.BackendFlags;
    // The fixture window is hidden: route synthetic pointer events to ImGui's
    // normal window hit testing instead of the OS hovered-viewport extension.
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),true);
    io.AddFocusEvent(true);
    auto click=[&](float x,float y) {
        io.AddMousePosEvent(x,y);draw();draw();
        io.AddMouseButtonEvent(0,true);draw();
        io.AddMouseButtonEvent(0,false);draw();
    };
    const float scale=OVUI_SCALE;
    const auto panelPosition=view.GetPosition();
    const float left=panelPosition.x+view.GetSize().x-164*scale;
    const float top=panelPosition.y+ImGui::GetFrameHeight()+8*scale;
    const auto beforeClick=camera.GetProjectionMode();
    click(left+42*scale,top+131*scale);
    Require(camera.GetProjectionMode()!=beforeClick,"projection button responds to a real ImGui click");
    click(left+78*scale,top+59*scale);
    Require(camera.GetProjectionMode()==beforeClick,"orientation cube centre toggles projection");
    click(left+112*scale,top+131*scale);
    Require(view.IsNavigationHovered(),"open view menu captures scene navigation input");
    click(left+122*scale,top+141*scale);draw();
    Require(camera.GetProjectionMode()==ORTHOGRAPHIC && FVector3::Dot(camera.transform->GetWorldForward(),-FVector3::Right)>.999f,
        "view menu item switches to the requested axis through a real ImGui click");
    Require(!view.IsNavigationHovered(),"selecting a view closes the navigation menu");
    controller.SetRotation(FQuaternion({25,135,0}));controller.SetPosition(focus-camera.transform->GetWorldForward()*20);
    io.BackendFlags=backendFlags;
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),false);
    actions.SelectActor(target);
    auto commands=view.RecordRenderCommands();auto pending=view.GetRecordedInteraction();execute(commands);
    Require(bool(pending),"navigation regression records an actual scene interaction");
    pending->pressed=true;pending->hovered=true;pending->pickingAvailable=true;pending->readback.reset();
    view.ToggleProjection();view.CompleteInteraction(pending);
    Require(actions.IsAnyActorSelected() && actions.GetSelectedActor().GetGUID()==target.GetGUID(),"view switch discards stale selection readback");
    draw();
    CaptureNavigation(context);
    actions.UnselectActor();ui.RemoveCanvas();scene.DestroyActor(target);scene.CollectGarbages();
}
