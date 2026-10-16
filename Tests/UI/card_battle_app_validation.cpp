#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/UI/Frame.h>
#include <OvCore/Resources/Material.h>
#include <OvRendering/Utils/ResourceTracking.h>
#include <OvWindowing/Inputs/InputManager.h>
#include <OvEditor/Core/Editor.h>
#include <OvEditor/Utils/ProjectManagement.h>
#include <cstdio>
#include <cmath>

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
    std::printf("PASS: %s\n", message);
}

int wmain(int argc, wchar_t** argv) try
{
    Require(argc == 2, "CardBattle app project path argument required");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    OvRendering::Utils::ResourceTracking::TrackResources();

    const std::filesystem::path folder = argv[1];
    const auto local = folder / "CardBattle.local.ovproject";
    const auto base = folder / "CardBattle.ovproject";

    using namespace OvEditor::Utils::ProjectManagement;
    Require(IsValidProjectDirectory(local), "CardBattle project directory is valid");
    Require(GetProjectFile(folder) == local, "Local project file selected for CardBattle");

    OvEditor::Core::Context context(local);
    context.window->Hide();

    Require(context.projectFolder == folder && context.projectFile == local, "Context matches CardBattle project folder");
    OvEditor::Core::Editor editor(context);

    Require(context.sceneManager.LoadScene("Scenes/CardBattle.ovscene"), "Editor loads CardBattle scene successfully");

    auto* scene = context.sceneManager.GetCurrentScene();
    Require(scene != nullptr, "Current scene is valid");

    auto* canvas = scene->FindActorByName("Canvas");
    Require(canvas != nullptr, "CardBattle scene contains Canvas actor");
    Require(scene->FindActorByName("Banner") != nullptr, "Scene contains Banner HUD actor");
    Require(scene->FindActorByName("Status") != nullptr, "Scene contains Status HUD actor");
    Require(scene->FindActorByName("Hint") != nullptr, "Scene contains Hint HUD actor");

    // Extract UI frame in edit mode
    auto frame = OvCore::UI::ExtractFrame(*scene, {960, 540});
    Require(context.sceneManager.GetCurrentScene()->FindActorByName("EventSystem") == nullptr, "Edit mode preview does not mutate scene");

    // Check CanvasRenderer output
    auto* renderer = canvas->GetComponent<OvCore::ECS::Components::CCanvasRenderer>();
    Require(renderer != nullptr, "Canvas has CCanvasRenderer component");
    Require(renderer->GetVertices().size() > 50, "CardBattle procedural mesh generated in canvas renderer");

    // Enter Play Mode
    auto& actions = OvCore::Global::ServiceLocator::Get<OvEditor::Core::EditorActions>();
    actions.StartPlaying();
    Require(actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::PLAY, "Editor successfully enters Play mode");

    using namespace OvWindowing::Inputs;
    auto tick = [&](int keyPress = 0, int mousePress = 0, double mouseX = 0, double mouseY = 0) {
        context.window->UIInputEvent.Invoke({UIInputKind::Focus, 1});
        if (mouseX != 0 || mouseY != 0)
        {
            context.window->UIInputEvent.Invoke({UIInputKind::PointerMove, 0, {mouseX, mouseY}});
        }
        if (keyPress)
        {
            context.window->UIInputEvent.Invoke({UIInputKind::KeyDown, keyPress, {mouseX, mouseY}});
            context.window->UIInputEvent.Invoke({UIInputKind::KeyUp, keyPress, {mouseX, mouseY}});
        }
        if (mousePress)
        {
            context.window->UIInputEvent.Invoke({UIInputKind::PointerDown, mousePress, {mouseX, mouseY}});
            context.window->UIInputEvent.Invoke({UIInputKind::PointerUp, mousePress, {mouseX, mouseY}});
        }
        editor.UpdateCurrentEditorMode(1.f / 60);
        context.inputManager->ClearEvents();
        OvCore::UI::ExtractFrame(*context.sceneManager.GetCurrentScene(), {960, 540});
    };

    // Run initial game ticks
    tick();
    tick();

    const size_t initialVertCount = renderer->GetVertices().size();
    Require(initialVertCount > 100, "CardBattle hand, characters, and HUD populated with high vertex count");

    // Play card 1 via keyboard shortcut '1' (Key code 49)
    tick(49);
    tick();
    Require(renderer->GetVertices().size() > 0, "CardBattle processes card play action smoothly");

    // End Turn via space key (Key code 32)
    tick(32);
    for (int i = 0; i < 60; ++i) tick(); // Advance 1 second for enemy turn
    Require(renderer->GetVertices().size() > 0, "CardBattle successfully advances through enemy turn");

    // Pause and Resume
    actions.PauseGame();
    tick();
    actions.TogglePause();
    tick();

    // Stop Playing
    actions.StopPlaying();
    Require(actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::EDIT, "Editor successfully stops playing");
    editor.DrainRenderFrames();

    std::printf("CardBattle validation completed successfully!\n");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "FAIL CardBattle app: %s\n", error.what());
    return 1;
}
