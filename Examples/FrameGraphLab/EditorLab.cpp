// Example host: the production editor, panels and frame submission path.
// No replacement renderer or access-only headers are used.
#include <OvCore/Resources/Material.h>
#include <OvEditor/Core/Editor.h>
#include <OvEditor/Panels/FrameGraph.h>
#include <OvEditor/Panels/GameView.h>
#include <OvEditor/Panels/MenuBar.h>
#include <OvEditor/Settings/EditorSettings.h>
#include <OvCore/ECS/Components/Behaviour.h>
#include <OvCore/ECS/Components/CSkinnedMeshRenderer.h>
#include <OvTools/Time/Clock.h>
#include <tracy/Tracy.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <OvCore/Rendering/SceneRenderExtractor.h>
#include "../../Tests/RenderingCommands/reflection_probe_selection_validation.inl"
#include "../../Tests/RenderingCommands/environment_texture_validation.inl"

namespace {
    std::filesystem::path capturePath;
    int captureWidth = 0, captureHeight = 0;
    void WriteBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!file) throw std::runtime_error("Cannot save capture");
    }
}

// Generated Window.cpp calls this immediately before its normal swap. The
// callback does nothing in interactive mode. Verification reads on the GL owner.
void FrameGraphLabBeforePresent() {
    if (capturePath.empty()) return;
    std::vector<uint8_t> pixels(size_t(captureWidth) * captureHeight * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, captureWidth, captureHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    WriteBytes(capturePath, pixels);
    capturePath.clear();
}

int main(int argc, char** argv) try {
    if (argc < 2) throw std::runtime_error("Expected project path [verification output]");
    const bool verify = argc > 2;
    const auto output = verify ? std::filesystem::path(argv[2]) : std::filesystem::path{};
    if (verify) std::filesystem::create_directories(output);
    OvEditor::Settings::EditorSettings::Load();
    OvEditor::Core::Context context(argv[1]);
    context.uiManager->EnableEditorLayoutSave(false);
    context.window->SetSize(1600, 900);
    context.window->SetPosition(40, 40);
    OvEditor::Core::Editor editor(context);
    editor.SetFrameSubmissionEnabled(true);
    auto& actions = EDITOR_EXEC(GetPanelsManager());
    for (const auto* name : {"Asset Browser", "Hardware Info", "Frame Info", "Console", "Asset View",
        "Hierarchy", "Inspector", "Scene View", "Game View", "Toolbar", "C# Scripts", "Material Editor",
        "Project Settings", "Asset Properties", "Texture Debugger", "Frame Graph"}) {
        auto& panel = actions.GetPanelAs<OvUI::Panels::PanelWindow>(name);
        panel.dockable = false;
        panel.autoSize = false;
        panel.Close();
    }
    const auto place = [&](const char* name, float x, float y, float w, float h) {
        auto& panel = actions.GetPanelAs<OvUI::Panels::PanelWindow>(name);
        panel.SetPosition({x, y}); panel.SetSize({w, h}); panel.Open();
    };
    place("Toolbar", 0, 24, 910, 60);
    place("Game View", 0, 84, 910, 560);
    place("Hierarchy", 0, 644, 330, 256);
    place("Inspector", 330, 644, 580, 256);
    place("Frame Graph", 910, 24, 690, 876);
    auto& game = actions.GetPanelAs<OvEditor::Panels::GameView>("Game View");
    auto& graph = actions.GetPanelAs<OvEditor::Panels::FrameGraph>("Frame Graph");
    game.SetPreviewMode(OvEditor::Panels::GameView::PreviewMode::CUSTOM);
    game.SetCustomResolution(960, 540);
    // Let normal ImGui window creation/positioning settle before the cold capture.
    for (int i = 0; i < 3; ++i) context.uiManager->RecordRenderCommands();
    EDITOR_EXEC(LoadSceneFromDisk("Scenes/FrameGraphLab.ovscene"));
    auto* scene = context.sceneManager.GetCurrentScene();
    if (!scene || scene->GetActors().size() != 100) throw std::runtime_error("Lab scene did not load all 100 actors");
    if (verify) {
        ValidateLabProbeSelection(*scene);
        ValidateLabEnvironment(context, *scene, output);
    }
    auto* controls = scene->FindActorByName("07 Lab Controls");
    if (!controls) throw std::runtime_error("Missing lab controls");
    EDITOR_EXEC(SelectActor(*controls));
    EDITOR_EXEC(StartPlaying());
    auto* behaviour = controls->GetBehaviour("Scripts/LabController.lua");
    if (!behaviour || !behaviour->GetScript() || !behaviour->GetScript()->IsValid())
        throw std::runtime_error("Lab controller failed to load");
    auto& script = behaviour->GetScript().value();
    const auto set = [&](const char* name, bool value) { script.SetProperty(name, value); };
    const auto tick = [&](int count) {
        for (int i = 0; i < count; ++i) {
            editor.PreUpdate(); editor.Update(1.0f / 60.0f); editor.PostUpdate();
            editor.DrainRenderFrames(); FrameMark;
            if (script.IsFaulted()) throw std::runtime_error("Lab controller faulted");
        }
    };
    const auto press = [&](int key) {
        editor.PreUpdate(); context.window->KeyPressedEvent.Invoke(key);
        editor.Update(1.0f / 60.0f); editor.PostUpdate(); editor.DrainRenderFrames(); FrameMark;
    };
    if (!verify) {
        OvTools::Time::Clock clock;
        while (!context.window->ShouldClose()) {
            editor.PreUpdate();
            // Preserve editor text input/graph navigation; controls use Game focus.
            // Resolve again because Stop/Play and script reload replace instances.
            if (auto* current = context.sceneManager.GetCurrentScene())
                if (auto* actor = current->FindActorByName("07 Lab Controls"))
                    if (auto* control = actor->GetBehaviour("Scripts/LabController.lua"))
                        if (auto live = control->GetScript())
                            live->SetProperty("_acceptKeys", game.IsFocused() && !ImGui::GetIO().WantTextInput);
            editor.Update(clock.GetDeltaTime()); editor.PostUpdate();
            clock.Update(); FrameMark;
        }
        editor.DrainRenderFrames();
        return 0;
    }
    set("animate", false);
    set("exposureProgressive", false);
    const auto save = [&](const std::string& name, bool image = false) {
        std::string error;
        if (!graph.SaveReport(output / (name + ".json"), error)) throw std::runtime_error(error);
        if (image) {
            const auto target = game.GetOutputTarget();
            const auto size = target.GetSize();
            if (size.first != 960 || size.second != 540) throw std::runtime_error("Game target has unexpected dimensions");
            std::vector<uint8_t> pixels(size_t(size.first) * size.second * 4);
            const auto read = [&](auto&) { target.ReadRGBA8(pixels); };
            if (context.renderThread) context.renderThread->Invoke(read); else read(*context.driver);
            WriteBytes(output / (name + ".rgba"), pixels);
            std::ofstream(output / (name + "-size.txt")) << size.first << ' ' << size.second;
        }
    };
    tick(2); save("cold", true);
    for (int i = 0; i < 12; ++i) { tick(1); save("progressive-" + std::to_string(i)); }
    save("all-effects", true);
    const auto size = context.window->GetFramebufferSize();
    captureWidth = size.first; captureHeight = size.second;
    std::ofstream(output / "editor-size.txt") << captureWidth << ' ' << captureHeight;
    capturePath = output / "editor.rgba"; tick(1);
    set("_acceptKeys", false); press(49); tick(1);
    if (!std::get<bool>(*script.GetProperty("shadows"))) throw std::runtime_error("Disabled shortcuts accepted a key");
    set("_acceptKeys", true);
    for (const auto& [effect, key] : std::vector<std::pair<const char*, int>>{
        {"shadows", 49}, {"reflections", 50}, {"bloom", 51}, {"exposure", 52}, {"fxaa", 53}, {"environment", 55}, {"glass", 84}, {"gallery", 71}}) {
        press(key); tick(7);
        const auto value = script.GetProperty(effect);
        if (!value || std::get<bool>(*value)) throw std::runtime_error(std::string("Keyboard toggle failed: ") + effect);
        save(std::string("without-") + effect, true);
        press(key); tick(7);
        if (!std::get<bool>(*script.GetProperty(effect))) throw std::runtime_error("Keyboard restore failed");
    }
    press(54); tick(7); save("six-faces", true);
    press(54); tick(7); save("one-face", true);
    press(82); tick(1); save("recapture");
    // Real Lua animation and bone playback, then pause again via the same control.
    press(80); tick(29); save("motion", true);
    const auto* copper = scene->FindActorByName("Animated Copper")->GetComponent<OvCore::ECS::Components::CSkinnedMeshRenderer>();
    if (!copper || !copper->IsPlaying() || copper->GetAnimationCount() == 0 || copper->GetTime() <= 0)
        throw std::runtime_error("Skeletal animation did not advance");
    press(80); tick(7); save("paused", true);
    if (!copper || copper->IsPlaying()) throw std::runtime_error("Pause control did not pause skeletal animation");
    const auto pausedTime = copper->GetTime();
    tick(8); save("paused-stable", true);
    if (copper->GetTime() != pausedTime) throw std::runtime_error("Paused bones continued advancing");
    game.SetCustomResolution(640, 360); tick(8); save("resized");
    game.SetCustomResolution(960, 540); tick(8); save("restored", true);
    std::cout << "FrameGraphLab product editor verification completed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}
