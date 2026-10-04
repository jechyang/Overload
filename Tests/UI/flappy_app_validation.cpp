#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/UI/Frame.h>
#include <OvCore/Resources/Material.h>
#include <OvRendering/Utils/ResourceTracking.h>
#include <OvWindowing/Inputs/InputManager.h>
#if defined(FLAPPY_EDITOR)
#include <OvEditor/Core/Editor.h>
#include <OvEditor/Utils/ProjectManagement.h>
#else
#include <OvGame/Core/Game.h>
#endif
#include <cstdio>
#include <cmath>

static void Require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); std::printf("PASS: %s\n",message); }

int wmain(int argc, wchar_t** argv) try
{
    Require(argc == 2, "isolated FlappyBird app project");
    std::setvbuf(stdout,nullptr,_IONBF,0);
    OvRendering::Utils::ResourceTracking::TrackResources();
#if defined(FLAPPY_EDITOR)
    const std::filesystem::path folder=argv[1];
    const auto local=folder/"FlappyBird.local.ovproject";
    const auto base=folder/"FlappyBird.ovproject";
    using namespace OvEditor::Utils::ProjectManagement;
    Require(IsValidProjectDirectory(local),"editor entry accepts an explicit project file");
    Require(GetProjectFile(folder)==local,"folder opening selects the machine-local configuration");
    Require(GetProjectFile(base)==base,"explicit base project selection is preserved");
    Require(!IsValidProjectDirectory(folder/"missing.ovproject"),"missing project path is rejected without directory iteration");
    OvEditor::Core::Context context(local); context.window->Hide();
    Require(context.projectFolder==folder && context.projectFile==local,"Context uses the selected file and its containing Assets directory");
    OvEditor::Core::Editor editor(context);
    Require(context.sceneManager.LoadScene("Scenes/FlappyBird.ovscene"), "editor opens shipped FlappyBird scene");
    OvCore::UI::ExtractFrame(*context.sceneManager.GetCurrentScene(),{400,600});
    Require(context.sceneManager.GetCurrentScene()->FindActorByName("EventSystem") == nullptr, "editor preview leaves authored scene unchanged");
    auto& actions = OvCore::Global::ServiceLocator::Get<OvEditor::Core::EditorActions>();
    actions.StartPlaying();
    Require(actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::PLAY,"EditorActions enters Play");
#else
    OvGame::Core::Context context; context.window->Hide();
    OvGame::Core::Game game(context);
    Require(context.sceneManager.IsCurrentSceneLoadedFromDisk(),"Game constructor resolves configured start_scene");
#endif
    auto renderer = [&]() {
        auto* scene = context.sceneManager.GetCurrentScene();
        auto* canvas = scene->FindActorByName("Canvas");
        if (!canvas) throw std::runtime_error("product scene has no Canvas");
        auto* result = canvas->GetComponent<OvCore::ECS::Components::CCanvasRenderer>();
        if (!result) throw std::runtime_error("product scene has no managed mesh");
        return result;
    };
    auto pipeX = [&]() {
        float x = 10000;
        for (const auto& v : renderer()->GetVertices())
            if (v.color.x > .20f && v.color.x < .24f && v.color.y > .70f && v.color.y < .74f) x = std::min(x,v.position.x);
        return x;
    };
    auto dead = [&]() { const auto& vertices=renderer()->GetVertices(); return !vertices.empty() && vertices.back().color.w < .96f; };
    using namespace OvWindowing::Inputs;
    auto tick = [&](int press = 0) {
#if !defined(FLAPPY_EDITOR)
        game.PreUpdate();
#endif
        context.window->UIInputEvent.Invoke({UIInputKind::Focus,1});
        if (press) {
            context.window->UIInputEvent.Invoke({press == 1 ? UIInputKind::KeyDown : UIInputKind::PointerDown,press == 1 ? 32 : 0,{100,300}});
            context.window->UIInputEvent.Invoke({press == 1 ? UIInputKind::KeyUp : UIInputKind::PointerUp,press == 1 ? 32 : 0,{100,300}});
        }
#if defined(FLAPPY_EDITOR)
        editor.UpdateCurrentEditorMode(1.f/60);
        context.inputManager->ClearEvents();
#else
        game.Update(1.f/60); game.PostUpdate();
#endif
        OvCore::UI::ExtractFrame(*context.sceneManager.GetCurrentScene(),{400,600});
    };
    tick(); tick();
    const float initial = pipeX();
    tick(1); tick();
    Require(pipeX() < initial && !dead(),"product Play loop starts and scrolls pipes on Space");
    for (int i=0;i<120;++i) tick();
    Require(dead(),"product Play loop reaches Game Over");
    tick(2);
    Require(!dead() && std::abs(pipeX()-initial)<.01f,"product mouse click resets the round");
#if defined(FLAPPY_EDITOR)
    tick(1); tick();
    const float paused=pipeX();
    actions.PauseGame(); tick();
    Require(std::abs(pipeX()-paused)<.01f,"Editor Pause freezes gameplay");
    actions.StopPlaying();
    Require(context.sceneManager.GetCurrentScene()->FindActorByName("EventSystem") == nullptr,"Editor Stop removes runtime input actor");
    actions.StartPlaying(); tick(); tick(1); tick();
    Require(pipeX()<initial && !dead(),"second Editor Play accepts input again");
    actions.StopPlaying(); editor.DrainRenderFrames();
#else
    game.DrainRenderFrames();
#endif
    return 0;
}
catch(const std::exception& error) { std::fprintf(stderr,"FAIL FlappyBird app: %s\n",error.what()); return 1; }
