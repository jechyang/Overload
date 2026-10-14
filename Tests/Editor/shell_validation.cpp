// Verification host for the Unity-style editor shell: production Editor, panels,
// layouts and frame submission; isolated profile, hidden window, back-buffer captures.
#include <OvCore/Resources/Material.h>
#include <OvCore/SceneSystem/Scene.h>
#include <OvEditor/Core/Editor.h>
#include <OvDebug/Logger.h>
#include <OvEditor/Panels/AssetBrowser.h>
#include <OvEditor/Panels/Console.h>
#include <OvEditor/Panels/Hierarchy.h>
#include <OvEditor/Panels/GameView.h>
#include <OvEditor/Panels/Inspector.h>
#include <OvEditor/Settings/EditorSettings.h>
#include <OvUI/Styling/Style.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <tracy/Tracy.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
    std::filesystem::path capturePath;
    int captureWidth = 0, captureHeight = 0;
    int passed = 0;

    void Require(bool condition, const std::string& label) {
        if (!condition) throw std::runtime_error("FAIL: " + label);
        ++passed;
        std::cout << "PASS: " << label << std::endl;
    }
}

// Generated Window.cpp calls this immediately before its normal swap, on the GL owner.
void EditorShellBeforePresent() {
    if (capturePath.empty()) return;
    std::vector<uint8_t> pixels(size_t(captureWidth) * captureHeight * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, captureWidth, captureHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream file(capturePath, std::ios::binary);
    file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
    capturePath.clear();
}

int main(int argc, char** argv) try {
    if (argc < 3) throw std::runtime_error("Expected project path and output folder");
    const std::filesystem::path output = argv[2];
    std::filesystem::create_directories(output);

    OvEditor::Settings::EditorSettings::Load();
    using OvEditor::Settings::EditorSettings;
    Require(EditorSettings::ColorTheme.Get() == static_cast<int>(OvUI::Styling::EStyle::UNITY_DARK), "fresh profile defaults to the Unity Dark theme");

    OvEditor::Core::Context context(argv[1]);
    context.window->SetSize(1600, 900);
    context.window->SetPosition(40, 40);
    OvEditor::Core::Editor editor(context);
    editor.SetFrameSubmissionEnabled(true);

    auto& actions = OvCore::Global::ServiceLocator::Get<OvEditor::Core::EditorActions>();
    auto& panels = actions.GetPanelsManager();
    const auto tick = [&](int count) {
        for (int i = 0; i < count; ++i) {
            editor.PreUpdate(); editor.Update(1.0f / 60.0f); editor.PostUpdate();
            editor.DrainRenderFrames(); FrameMark;
        }
    };
    const auto size = context.window->GetFramebufferSize();
    captureWidth = size.first; captureHeight = size.second;
    std::ofstream(output / "size.txt") << captureWidth << ' ' << captureHeight;
    const auto capture = [&](const std::string& name) {
        capturePath = output / (name + ".rgba"); tick(1);
        Require(capturePath.empty(), "captured " + name);
    };
    const auto window = [&](const char* id) {
        auto& panel = panels.GetPanelAs<OvUI::Panels::PanelWindow>(id);
        return ImGui::FindWindowByName((panel.name + panel.GetPanelID()).c_str());
    };

    tick(4);
    actions.LoadSceneFromDisk("Scenes/FrameGraphLab.ovscene");
    auto* scene = context.sceneManager.GetCurrentScene();
    Require(scene && !scene->GetActors().empty(), "lab scene loaded");
    Require(!actions.IsSceneModified(), "freshly loaded scene is not modified");

    // Default layout: stable IDs, Unity names and docking areas.
    Require(panels.GetPanelAs<OvUI::Panels::PanelWindow>("Asset Browser").GetPanelID() == "###Asset Browser", "panels use stable ImGui IDs");
    Require(panels.GetPanelAs<OvUI::Panels::PanelWindow>("Asset Browser").name == "Project", "Asset Browser is presented as Project");
    tick(3);
    auto* hierarchy = window("Hierarchy"); auto* inspector = window("Inspector");
    auto* sceneWindow = window("Scene View"); auto* project = window("Asset Browser"); auto* console = window("Console");
    Require(hierarchy && inspector && sceneWindow && project && console, "core windows exist");
    Require(hierarchy->DockNode && inspector->DockNode && sceneWindow->DockNode && project->DockNode, "core windows are docked");
    Require(hierarchy->Pos.x < sceneWindow->Pos.x && sceneWindow->Pos.x < inspector->Pos.x, "Hierarchy | Scene | Inspector columns");
    Require(project->Pos.y > sceneWindow->Pos.y && project->DockNode == console->DockNode, "Project and Console share the bottom area");
    Require(inspector->Size.y > sceneWindow->Size.y, "Inspector spans the full height");
    const auto* viewport = ImGui::GetMainViewport();
    Require(viewport->WorkPos.y > ImGui::GetFrameHeight() + 20.0f, "main toolbar reserves space under the menu bar");
    Require(viewport->WorkPos.y + viewport->WorkSize.y < viewport->Pos.y + viewport->Size.y - 10.0f, "status bar reserves space at the bottom");

    if (auto* selected = scene->FindActorByName("20 Reflective Sculptures")) actions.SelectActor(*selected);
    else actions.SelectActor(*scene->GetActors().front());
    tick(8);
    capture("1-default-layout");

    // Unsaved-change tracking follows the real undo history.
    auto& target = actions.GetSelectedActor();
    const auto originalName = target.GetName();
    actions.BeginSceneEdit("Rename entity");
    target.SetName(originalName + " (edited)");
    actions.CommitSceneEdit(true);
    Require(actions.IsSceneModified(), "an edit marks the scene modified");
    tick(2);
    capture("2-modified");
    Require(actions.Undo() && !actions.IsSceneModified(), "undo back to the loaded state clears the modified flag");
    Require(actions.Redo() && actions.IsSceneModified(), "redo marks the scene modified again");

    const auto savedPath = (output / "Saved.ovscene").string();
    Require(actions.SaveSceneToDisk(*context.sceneManager.GetCurrentScene(), savedPath), "scene saves to disk");
    Require(!actions.IsSceneModified(), "saving clears the modified flag");
    Require(context.sceneManager.GetCurrentSceneSourcePath() == savedPath, "saving the open scene repoints it");
    Require(actions.Undo() && actions.IsSceneModified(), "undo past the save point marks the scene modified");
    Require(actions.Redo() && !actions.IsSceneModified(), "redo to the save point is clean again");

    OvCore::SceneSystem::Scene other;
    Require(actions.SaveSceneToDisk(other, (output / "Other.ovscene").string()), "another scene saves to disk");
    Require(context.sceneManager.GetCurrentSceneSourcePath() == savedPath, "saving another scene keeps the open scene's path");
    Require(actions.ConfirmDiscardSceneChanges(), "an unmodified scene is replaced without a prompt");

    // Play mode: tint, toolbar state, and restoration on stop.
    actions.StartPlaying();
    tick(6);
    const auto tint = OvUI::Styling::Style::Get().backgroundTint;
    Require(tint.r < 1.0f || tint.g < 1.0f || tint.b < 1.0f, "play mode tints the editor");
    capture("3-play-mode");
    actions.TogglePause();
    Require(actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::PAUSE, "pause toggles while playing");
    actions.TogglePause();
    Require(actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::PLAY, "pause toggles back to playing");
    actions.TogglePlay();
    tick(3);
    const auto restored = OvUI::Styling::Style::Get().backgroundTint;
    Require(actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::EDIT, "play toggles back to edit mode");
    Require(restored.r == 1.0f && restored.g == 1.0f && restored.b == 1.0f, "stopping clears the tint");
    Require(!actions.IsSceneModified(), "play mode changes do not mark the scene modified");

    // Layout preset: 2 by 3.
    actions.ApplyLayout(OvEditor::Core::EEditorLayout::TWO_BY_THREE);
    tick(6);
    sceneWindow = window("Scene View"); auto* game = window("Game View");
    hierarchy = window("Hierarchy"); project = window("Asset Browser"); inspector = window("Inspector");
    Require(game->DockNode != sceneWindow->DockNode && game->Pos.y > sceneWindow->Pos.y, "2 by 3: Game below Scene");
    Require(sceneWindow->Pos.x < hierarchy->Pos.x && hierarchy->Pos.x < project->Pos.x && project->Pos.x < inspector->Pos.x, "2 by 3: Scene | Hierarchy | Project | Inspector");
    capture("4-two-by-three");

    actions.ApplyLayout(OvEditor::Core::EEditorLayout::DEFAULT);
    tick(6);
    Require(window("Game View")->DockNode == window("Scene View")->DockNode, "default layout restores Scene/Game tabs");

    // Error Pause: an error logged while playing pauses at the next frame boundary.
    auto& consolePanel = panels.GetPanelAs<OvEditor::Panels::Console>("Console");
    consolePanel.SetErrorPause(true);
    actions.StartPlaying(); tick(3);
    OVLOG_WARNING("Shell validation warning");
    tick(2);
    Require(actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::PLAY, "Error Pause ignores warnings");
    OVLOG_ERROR("Shell validation error");
    tick(2);
    Require(actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::PAUSE, "Error Pause pauses on an error");
    actions.StopPlaying(); tick(3);
    consolePanel.SetErrorPause(false);
    actions.StartPlaying(); tick(2);
    OVLOG_ERROR("Shell validation error without pause");
    tick(2);
    Require(actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::PLAY, "errors do not pause with Error Pause off");
    actions.StopPlaying(); tick(3);
    OVLOG_INFO("Shell validation info");
    consolePanel.Open(); consolePanel.Focus(); tick(4);
    capture("7-console");

    // Hierarchy: scene root row plus a search that reports its matches.
    auto& hierarchyPanel = panels.GetPanelAs<OvEditor::Panels::Hierarchy>("Hierarchy");
    hierarchyPanel.SetSearchFilter("probe");
    tick(3);
    capture("8-hierarchy-search");
    hierarchyPanel.SetSearchFilter("");
    tick(2);

    // Game view: Unity-style Stats overlay.
    auto& gameView = panels.GetPanelAs<OvEditor::Panels::GameView>("Game View");
    gameView.SetStatsVisible(true);
    gameView.Focus(); tick(6);
    capture("9-game-stats");
    gameView.SetStatsVisible(false);
    panels.GetPanelAs<OvUI::Panels::PanelWindow>("Scene View").Focus(); tick(3);

    // Project window: two columns, real mouse navigation, thumbnails.
    auto& browser = panels.GetPanelAs<OvEditor::Panels::AssetBrowser>("Asset Browser");
    browser.Focus(); tick(3);
    project = window("Asset Browser");
    ImGuiWindow* content = nullptr; ImGuiWindow* tree = nullptr;
    for (auto* candidate : ImGui::GetCurrentContext()->Windows)
        if (candidate->ParentWindow == project) {
            if (std::string_view(candidate->Name).find("##Content") != std::string_view::npos) content = candidate;
            if (std::string_view(candidate->Name).find("##FolderTree") != std::string_view::npos) tree = candidate;
        }
    Require(content && tree && tree->Pos.x < content->Pos.x, "Project shows a folder tree left of the content area");
    Require(tree->Size.x > 180.0f * OVUI_SCALE, "folder tree recovers its width after the narrow 2 by 3 layout");
    Require(browser.GetCurrentFolder() == std::filesystem::path(context.projectAssetsPath).lexically_normal(), "Project opens on the Assets folder");

    auto& io = ImGui::GetIO();
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(), true); io.AddFocusEvent(true);
    const ImVec2 firstTile{ content->DC.CursorStartPos.x + 4.0f * OVUI_SCALE + 36.0f * OVUI_SCALE, content->DC.CursorStartPos.y + 40.0f * OVUI_SCALE };
    const auto clickOnce = [&](ImVec2 p) { io.AddMousePosEvent(p.x, p.y); tick(1); io.AddMouseButtonEvent(0, true); tick(1); io.AddMouseButtonEvent(0, false); tick(1); };
    io.AddMousePosEvent(firstTile.x, firstTile.y); tick(2);
    io.AddMouseButtonEvent(0, true); tick(1); io.AddMouseButtonEvent(0, false); tick(1);
    io.AddMouseButtonEvent(0, true); tick(1); io.AddMouseButtonEvent(0, false); tick(3);
    Require(browser.GetCurrentFolder().filename() == "Materials", "double-clicking a folder tile opens it (real ImGui input)");
    tick(4);
    capture("5-project-materials");

    browser.ShowFolder(std::filesystem::path(context.engineAssetsPath) / "Textures");
    tick(6);
    capture("6-project-engine-textures");
    (void)clickOnce;
    io.AddMousePosEvent(-10000, -10000); tick(1);

    // Hierarchy: F2 renames in place with real keyboard input; Escape cancels; undo restores.
    scene = context.sceneManager.GetCurrentScene(); // Play/Stop reloaded the scene
    auto& renameTarget = *scene->FindActorByName("03 Cool Point");
    const auto renameGUID = renameTarget.GetGUID();
    const auto pressKey = [&](ImGuiKey key) { io.AddKeyEvent(key, true); tick(1); io.AddKeyEvent(key, false); tick(2); };
    actions.SelectActor(renameTarget); hierarchyPanel.Focus(); tick(3);
    Require(hierarchyPanel.BeginRenameSelection(), "F2 rename starts on the selected entity");
    tick(3);
    Require(io.WantTextInput, "the rename field takes keyboard focus");
    io.AddInputCharactersUTF8("Renamed Light"); tick(2);
    capture("10-hierarchy-rename");
    pressKey(ImGuiKey_Enter);
    Require(scene->FindActorByGUID(renameGUID)->GetName() == "Renamed Light", "Enter commits the inline rename");
    actions.CommitSceneEdit(true);
    Require(hierarchyPanel.BeginRenameSelection(), "rename can start again");
    tick(3); io.AddInputCharactersUTF8("Discarded"); tick(2);
    pressKey(ImGuiKey_Escape);
    Require(scene->FindActorByGUID(renameGUID)->GetName() == "Renamed Light", "Escape cancels the inline rename");
    Require(actions.Undo() && context.sceneManager.GetCurrentScene()->FindActorByGUID(renameGUID)->GetName() == "03 Cool Point", "undo restores the previous name");

    // Console: the selected message's full text shows in the detail area.
    OVLOG_WARNING("Detail line one\nDetail line two\nDetail line three");
    consolePanel.Focus(); tick(3);
    consolePanel.SelectDisplayedLog(static_cast<int>(1e9)); tick(1);
    Require(consolePanel.GetSelectedLogMessage().empty(), "out-of-range selection clears the detail area");
    consolePanel.SelectDisplayedLog(0); tick(1);
    Require(!consolePanel.GetSelectedLogMessage().empty(), "selecting a row exposes its full message");
    for (int index = 0; index < 1000 && consolePanel.GetSelectedLogMessage().find("Detail line two") == std::string::npos; ++index)
        consolePanel.SelectDisplayedLog(index);
    Require(consolePanel.GetSelectedLogMessage().find("Detail line three") != std::string::npos, "the detail area keeps multi-line messages whole");
    tick(3);
    capture("11-console-detail");
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(), false);

    std::cout << passed << " checks passed\n";
    editor.DrainRenderFrames();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}
