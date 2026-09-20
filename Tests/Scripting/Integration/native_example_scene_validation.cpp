#include <OvCore/Resources/Material.h>
#include <OvEditor/Core/Editor.h>
#include <OvEditor/Panels/CSharpScripts.h>
#include <OvUI/Widgets/Buttons/Button.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/ECS/Components/CPhysicalBox.h>
#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvRendering/Utils/ResourceTracking.h>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <fstream>

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
    std::printf("PASS: %s\n", message);
}

int wmain(int argc, wchar_t** argv)
{
    try {
        Require(argc == 2 || argc == 3, "isolated example project supplied");
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        OvRendering::Utils::ResourceTracking::TrackResources();
        OvEditor::Core::Context context(argv[1]);
        context.window->Hide();
        OvEditor::Core::Editor editor(context);
        auto& actions = OvCore::Global::ServiceLocator::Get<OvEditor::Core::EditorActions>();
        auto& project = *context.csharpProject;
        const auto pump = [&](int milliseconds) {
            const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
            do {editor.PreUpdate();std::this_thread::sleep_for(std::chrono::milliseconds(20));} while(std::chrono::steady_clock::now()<until);
        };
        const auto settle = [&] {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(4);
            while(project.Busy()) {
                pump(20);
                if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Build timed out: "+project.Diagnostics());
            }
        };
        if(argc==3 && std::wstring(argv[2])==L"first-script") {
            Require(!project.Busy()&&!context.csharpHost,"a project without C# does not start a managed build");
            pump(1200);
            Require(!std::filesystem::exists(context.projectFolder/"GameScripts.csproj"),"Lua-only project remains without C# build files");
            {std::ofstream file(context.projectFolder/"Assets/FirstBehaviour.cs");file<<"using Overload; public sealed class FirstBehaviour : Behaviour {}\n";}
            pump(1100);settle();
            Require(context.csharpHost && !project.AppliedGeneration().empty() && context.scriptEngine->CSharpTypes().size()==1,"saving the first C# file creates and builds the project automatically");
            pump(1400);Require(!project.Busy(),"first script scaffolding does not retrigger a build");
            return 0;
        }
        if(argc==3) {
            Require(!project.watch && project.hotReload && !project.Busy(),"automatic build and runtime reload preferences survive reopening");
            Require(context.csharpHost && !project.AppliedGeneration().empty(),"reopen loads last successful generation");
            project.SetAutomaticBuild(true);settle();
            Require(project.Status()=="C#: up to date","reopen checks unchanged source without reloading");
            return 0;
        }
        Require(!context.csharpHost, "example starts before its first C# build");
        auto& panel = actions.GetPanelsManager().GetPanelAs<OvEditor::Panels::CSharpScripts>("C# Scripts");
        OvUI::Widgets::Buttons::Button* build = nullptr;
        for (auto [widget, mode] : panel.GetWidgets()) {
            (void)mode;
            auto* button = dynamic_cast<OvUI::Widgets::Buttons::Button*>(widget);
            if (button && button->label == "Build / Reload C# (Debug)") build = button;
        }
        Require(build != nullptr, "production C# Build button found");
        Require(project.Busy(), "opening a C# project automatically queues its first build");
        settle();
        if (project.AppliedGeneration().empty()) throw std::runtime_error("Example UI build failed: " + project.Diagnostics());
        Require(context.csharpHost != nullptr && project.Status() == "C#: applied Debug", "automatic worker discovers Python 3 and applies complete generation");
        using namespace OvCore::ECS::Components;
        // Each variant is loaded from disk through the same relative-path entry as the UI.
        for (const auto* file : {"Scenes/CSharpGameplay.ovscene", "Scenes/WithDeclaration.ovscene",
                                 "Scenes/WithComment.ovscene"}) {
            context.sceneManager.LoadEmptyScene();
            actions.LoadSceneFromDisk(file);
            auto current = [&]() { return context.sceneManager.GetCurrentScene()->FindActorByName("CSharp Moving Box"); };
            Require(context.sceneManager.IsCurrentSceneLoadedFromDisk() && current(), "scene loaded from Assets-relative path");
            Require(context.sceneManager.GetCurrentScene()->GetActors().size() == 3, "all scene actors deserialized");
            auto* camera = context.sceneManager.GetCurrentScene()->FindMainCamera();
            Require(camera && camera->GetProjectionMode() == OvRendering::Settings::EProjectionMode::PERSPECTIVE, "perspective camera loaded");
            auto* model = current()->GetComponent<CModelRenderer>();
            auto* materials = current()->GetComponent<CMaterialRenderer>();
            Require(model && model->GetModel() && materials && materials->GetMaterialAtIndex(0)
                && materials->GetMaterialAtIndex(0)->HasShader(), "cube and material resources loaded");
            auto* behaviour = current()->GetBehaviour(OvCore::Scripting::ScriptIdentity::CSharp("GameScripts", "Gameplay.MoveAndResize"));
            Require(behaviour && behaviour->PrepareManagedFields(), "serialized C# attachment resolved");
            actions.StartPlaying();
            editor.UpdateCurrentEditorMode(0.25f);
            Require(current()->transform.GetLocalPosition().x == -0.5f && current()->GetComponent<CPhysicalBox>()->GetSize().x == 1.25f,
                "Play executes C# and updates native Transform and Box");
            actions.StopPlaying();
            Require(current()->transform.GetLocalPosition().x == -1 && current()->GetComponent<CPhysicalBox>()->GetSize().x == 1,
                "Stop restores the loaded scene");
            actions.StartPlaying();
            for (int frame = 0; frame < 3; ++frame) { editor.PreUpdate(); editor.Update(0.01f); editor.PostUpdate(); }
            editor.DrainRenderFrames();
            actions.StopPlaying();
        }
        const auto source=context.projectFolder/"MoveAndResize.cs";
        std::ifstream input(source,std::ios::binary);
        const std::string original{std::istreambuf_iterator<char>(input),{}};input.close();
        const auto save=[&](const std::string& code){std::ofstream stream(source,std::ios::binary);stream<<code;stream.close();pump(1100);settle();};
        const auto requests=[&]{size_t count=0;for(const auto& file:std::filesystem::directory_iterator(context.projectFolder/".overload/requests"))if(file.path().extension()==".log")++count;return count;};
        const auto good=project.AppliedGeneration();
        save(original+"\nthis is invalid CSharp;\n");
        Require(project.AppliedGeneration()==good && project.Status()=="C#: failed; previous version retained" && project.Diagnostics().find("error CS")!=std::string::npos,"save compile failure retains old generation and exposes diagnostics");
        auto count=requests();pump(1400);
        Require(!project.Busy()&&requests()==count,"failed automatic build does not loop");
        save(original+"\n// fixed and saved\n");
        Require(project.AppliedGeneration()!=good && project.Status()=="C#: applied Debug","fix and save automatically recovers");
        count=requests();pump(1400);
        Require(!project.Busy()&&requests()==count,"generated outputs do not retrigger builds");

        actions.LoadSceneFromDisk("Scenes/CSharpGameplay.ovscene");
        actions.StartPlaying();
        const auto beforeStop=project.AppliedGeneration();
        save(original+"\n// build while playing with hot reload disabled\n");
        Require(project.AppliedGeneration()==beforeStop && project.Status()=="C#: built; reload pending Stop","default Play retains current generation until Stop");
        actions.StopPlaying();pump(40);
        Require(project.AppliedGeneration()!=beforeStop,"Stop applies the pending generation at Main safe point");

        project.SetHotReload(true);actions.StartPlaying();
        const auto beforeReload=project.AppliedGeneration();
        auto faster=original;auto pos=faster.find("speed = 2");Require(pos!=std::string::npos,"example speed edit located");faster.replace(pos,9,"speed = 4");
        save(faster);
        Require(project.AppliedGeneration()!=beforeReload && project.Status()=="C#: applied Debug","opt-in Play reload automatically applies changed source");
        auto* actor=context.sceneManager.GetCurrentScene()->FindActorByName("CSharp Moving Box");
        const auto x=actor->transform.GetLocalPosition().x;
        editor.UpdateCurrentEditorMode(0.25f);
        Require(actor->transform.GetLocalPosition().x==x+1,"reloaded C# code executes during Play");
        actions.StopPlaying();

        project.SetAutomaticBuild(false);count=requests();save(original);
        Require(!project.Busy()&&requests()==count,"automatic build can be disabled");
        build->ClickedEvent.Invoke();settle();
        Require(project.Status()=="C#: applied Debug","manual Build remains available when automation is disabled");
        auto& settings=context.projectSettings;settings.Reload();
        settings.Add("csharp_python",std::string("C:/missing-overload-python/python.exe"));settings.Rewrite();
        const auto beforeBadTool=project.AppliedGeneration();build->ClickedEvent.Invoke();settle();
        Require(project.AppliedGeneration()==beforeBadTool&&project.Diagnostics().find("Invalid csharp_python")!=std::string::npos,"invalid explicit tool path gives an actionable error and retains old generation");
        settings.Set("csharp_python",std::string(""));settings.Rewrite();
        build->ClickedEvent.Invoke();settle();
        Require(project.Status()=="C#: up to date","fixing the tool path recovers without restarting the editor");
        editor.DrainRenderFrames();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
