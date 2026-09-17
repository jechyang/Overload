#define main UnusedEditorValidationMain
#include "../Skinning/editor_validation.cpp"
#undef main
#include <OvCore/ECS/Components/CUI.h>
#include <GLFW/glfw3.h>

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
    auto& view = editor.m_panelsManager.GetPanelAs<OvEditor::Panels::SceneView>("Scene View");
    view.Open(); view.SetSize({320, 260});
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
    context.window->MouseButtonPressedEvent.Invoke(GLFW_MOUSE_BUTTON_RIGHT);
    view.Update(1.0f / 60);
    Require(controller.IsRightMousePressed(), "first right drag needs an actor selection");
    context.inputManager->ClearEvents();
    context.window->MouseButtonReleasedEvent.Invoke(GLFW_MOUSE_BUTTON_RIGHT);
    view.Update(1.0f / 60);
    Require(!controller.IsOperating(), "right drag did not release");
    context.inputManager->ClearEvents();
    view.m_hovered = false;
    for (bool selected : {false, true})
    {
        if (selected) actions.SelectActor(canvas);
        for (bool recorded : {false, true})
        {
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
            Require(before[pixel] == 255 && before[pixel+1] == 0 &&
                after[pixel] == 255 && after[pixel+1] == 0,
                "screen-space UI moved with the editor camera");
            std::printf("PASS: UI Scene camera rotates (%s, %s); OverlayUI remains fixed\n",
                selected ? "Canvas selected" : "nothing selected", recorded ? "recorded" : "immediate");
        }
    }
    Require(glGetError() == GL_NO_ERROR, "camera regression produced GL errors");
    std::puts("PASS: first right capture/release without selection; UI Scene camera rendering");
    return 0;
}
catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
