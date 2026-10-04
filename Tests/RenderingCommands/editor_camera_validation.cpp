#define main UnusedEditorValidationMain
#include "../Skinning/editor_validation.cpp"
#undef main
#include <OvCore/ECS/Components/CUI.h>
#include <GLFW/glfw3.h>
#include <OvUI/Styling/Style.h>

class CameraValidationView : public OvEditor::Panels::SceneView
{
public:
    using SceneView::SceneView;
    bool overlays = true;
protected:
    OvEditor::Rendering::EditorOverlayRequest CreateOverlayRequest() override {
        auto request=SceneView::CreateOverlayRequest();
        if(!overlays) {request.grid=false;request.debugObjects=false;request.picking=false;}
        return request;
    }
};

int main(int argc, char** argv) try
{
    Require(argc > 1, "pass output directory");
    const auto project = std::filesystem::path(argv[1]) / "CameraProject";
    std::filesystem::create_directories(project / "Assets");
    std::ofstream(project / "CameraProject.ovproject") <<
        "gravity=0\nx_resolution=640\ny_resolution=480\nfullscreen=false\nvsync=false\n"
        "multisampling=false\nsamples=1\nbuild_type=0\nwindow_icon=\n";
    OvEditor::Core::Context context(project);
    OvEditor::Core::Editor editor(context);
    context.uiManager->EnableEditorLayoutSave(false);
    context.uiManager->Render();
    auto& actions = editor.m_editorActions;
    OvUI::Settings::PanelWindowSettings viewSettings;viewSettings.dockable=false;
    CameraValidationView view("Camera regression",true,viewSettings);
    OvUI::Modules::Canvas viewCanvas;viewCanvas.AddPanel(view);context.uiManager->SetCanvas(viewCanvas);
    view.autoSize=false;view.SetPosition({0,0});view.SetSize({640,480});
    for(int i=0;i<3;++i)context.uiManager->Render();
    context.sceneManager.LoadEmptyScene();
    auto& scene = *context.sceneManager.GetCurrentScene();
    auto& canvas = scene.CreateActor("Canvas");
    canvas.AddComponent<OvCore::ECS::Components::CCanvas>();
    auto& image = scene.CreateActor("Image"); image.SetParent(canvas);
    image.AddComponent<OvCore::ECS::Components::CUIImage>().SetColor({1, 0, 0, 1});
    auto* rect = image.GetComponent<OvCore::ECS::Components::CRectTransform>();
    rect->SetAnchorMin({0, 0}); rect->SetAnchorMax({0, 0}); rect->SetPivot({0, 0});
    rect->SetAnchoredPosition({8, 8}); rect->SetSizeDelta({40, 20});
    actions.UnselectActor();
    auto& controller = view.GetCameraController();
    view.m_hovered = true;
    glfwSetCursorPos(context.window->GetGlfwWindow(),view.GetPosition().x+20,view.GetPosition().y+ImGui::GetFrameHeight()+20);
    context.window->MouseButtonPressedEvent.Invoke(GLFW_MOUSE_BUTTON_RIGHT);
    view.Update(1.0f / 60);
    Require(controller.IsRightMousePressed(), "first right drag needs an actor selection");
    context.inputManager->ClearEvents();
    context.window->MouseButtonReleasedEvent.Invoke(GLFW_MOUSE_BUTTON_RIGHT);
    view.Update(1.0f / 60);
    Require(!controller.IsOperating(), "right drag did not release");
    context.inputManager->ClearEvents();
    const double navigationX=view.GetPosition().x+view.GetSize().x-120*OVUI_SCALE;
    const double navigationY=view.GetPosition().y+ImGui::GetFrameHeight()+40*OVUI_SCALE;
    glfwSetCursorPos(context.window->GetGlfwWindow(),navigationX,navigationY);
    const auto pointer=context.inputManager->GetMousePosition();
    // GLFW ignores cursor warps when a hidden/headless test window has no focus.
    // Real ImGui button event replay is covered by editor_navigation_checks.h.
    if(std::abs(pointer.first-navigationX)<1 && std::abs(pointer.second-navigationY)<1) {
    Require(view.IsNavigationHovered(),"navigation overlay hit region follows the Scene viewport");
    context.window->MouseButtonPressedEvent.Invoke(GLFW_MOUSE_BUTTON_RIGHT);
    view.Update(1.0f/60);
    Require(!controller.IsOperating(),"orientation overlay must not start FPS camera capture");
    context.inputManager->ClearEvents();
    context.window->MouseButtonReleasedEvent.Invoke(GLFW_MOUSE_BUTTON_RIGHT);view.Update(1.0f/60);context.inputManager->ClearEvents();
    actions.SelectActor(canvas);
    context.window->MouseButtonPressedEvent.Invoke(GLFW_MOUSE_BUTTON_LEFT);view.Render();
    Require(actions.IsAnyActorSelected() && actions.GetSelectedActor().GetGUID()==canvas.GetGUID(),"orientation overlay click must not clear scene selection");
    context.inputManager->ClearEvents();
    context.window->MouseButtonReleasedEvent.Invoke(GLFW_MOUSE_BUTTON_LEFT);view.Update(1.0f/60);context.inputManager->ClearEvents();
    actions.UnselectActor();
    } else std::puts("SKIP: native cursor warp unavailable; navigation buttons covered by ImGui event replay");
    view.m_hovered = false;
    for (bool selected : {false, true})
    {
        if (selected) actions.SelectActor(canvas);
        for (const auto mode : {OvRendering::Settings::EProjectionMode::PERSPECTIVE,OvRendering::Settings::EProjectionMode::ORTHOGRAPHIC})
        for (bool recorded : {false, true})
        {
            view.GetCamera()->SetProjectionMode(mode);view.GetCamera()->SetSize(8);
            view.overlays=true;
            auto render = [&] {
                if (recorded) {
                    auto commands = view.RecordRenderCommands(); commands.Execute(*context.driver); view.CompleteRender();
                } else view.Render();
                const auto [w, h] = view.GetSafeSize();
                std::vector<uint8_t> pixels(size_t(w) * h * 4);
                view.GetOutputTarget().ReadRGBA8(pixels); return pixels;
            };
            controller.SetPosition({-10, 3, 10});
            controller.SetRotation(FQuaternion(FVector3{0, 135, 0}));
            const auto before = render();
            controller.SetRotation(FQuaternion(FVector3{25, 105, 0}));
            const auto after = render();
            Require(before != after, "Scene grid/debug rendering ignored preview camera rotation with OverlayUI");
            const auto [width, height] = view.GetSafeSize();
            const auto pixel = (size_t(16) * width + 16) * 4;
            // Editor grid lines blend on top of OverlayUI. Compare UI-only
            // pixels separately so grid movement cannot contaminate the probe.
            view.overlays=false;
            controller.SetRotation(FQuaternion(FVector3{0,135,0}));const auto uiBefore=render();
            controller.SetRotation(FQuaternion(FVector3{25,105,0}));const auto uiAfter=render();
            Require(uiBefore==uiAfter && uiBefore[pixel] == 255 && uiBefore[pixel+1] == 0,
                "screen-space UI moved with the editor camera");
            std::printf("PASS: UI Scene camera rotates (%s, %s, %s); OverlayUI remains fixed\n",
                selected ? "Canvas selected" : "nothing selected", recorded ? "recorded" : "immediate",
                mode==OvRendering::Settings::EProjectionMode::PERSPECTIVE ? "perspective" : "orthographic");
        }
    }
    Require(glGetError() == GL_NO_ERROR, "camera regression produced GL errors");
    context.uiManager->RemoveCanvas();
    std::puts("PASS: first right capture/release without selection; UI Scene camera rendering");
    return 0;
}
catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
