#pragma warning(push)
#pragma warning(disable: 4100 4201)
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/ECS/Components/CPhysicalBox.h>
#include <OvCore/Resources/Material.h>
#include <OvRendering/Utils/ResourceTracking.h>
#if defined(S3_EDITOR)
#include <OvEditor/Core/Editor.h>
#else
#include <OvGame/Core/Game.h>
#endif
#pragma warning(pop)
#include <cstdio>
#include <fstream>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#undef APIENTRY
#include <Windows.h>
static void Require(bool test, const char* text)
{ if (!test) throw std::runtime_error(text); std::printf("PASS: %s\n", text); }
using Box = OvCore::ECS::Components::CPhysicalBox;
using OvCore::Scripting::ScriptIdentity;
static OvCore::ECS::Actor& Add(OvCore::SceneSystem::Scene& scene)
{
    auto& actor = scene.CreateActor("app-mixed");
    auto& box = actor.AddComponent<Box>(); box.SetMass(0); box.SetSize({1,1,1});
    actor.AddBehaviour(ScriptIdentity::CSharp("S3", "Gameplay.MoveAndResize"));
    actor.AddBehaviour(ScriptIdentity::CSharp("S3", "S3.Trace"));
    actor.AddBehaviour("s3.lua");
    return actor;
}
int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        Require(argc == 2, "app fixture has isolated project directory");
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        OvRendering::Utils::ResourceTracking::TrackResources();
#if defined(S3_EDITOR)
        OvEditor::Core::Context context(argv[1]); context.window->Hide();
        OvEditor::Core::Editor editor(context);
        context.sceneManager.LoadEmptyScene();
        auto& actor = Add(*context.sceneManager.GetCurrentScene()); const auto guid = actor.GetGUID();
        auto& actions = OvCore::Global::ServiceLocator::Get<OvEditor::Core::EditorActions>();
        auto current = [&]() -> OvCore::ECS::Actor& { return *context.sceneManager.GetCurrentScene()->FindActorByGUID(guid); };
        editor.UpdateCurrentEditorMode(0.25f);
        std::printf("Editor observed position=%f box=%f\n",current().transform.GetLocalPosition().x,current().GetComponent<Box>()->GetSize().x);
        Require(current().transform.GetLocalPosition().x == 0, "real Editor edit mode runs no gameplay");
        actions.StartPlaying();
        Require(actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::PLAY, "real EditorActions starts Play");
        editor.UpdateCurrentEditorMode(0.25f);
        Require(current().transform.GetLocalPosition().x == 0.5f && current().GetComponent<Box>()->GetSize().x == 1.25f,
            "real Editor UpdatePlayMode drives CSharp transform and box");
        actions.PauseGame(); editor.UpdateCurrentEditorMode(0.25f);
        Require(current().transform.GetLocalPosition().x == 0.5f, "real Editor Pause does not advance scripts");
        actions.NextFrame(); editor.UpdateCurrentEditorMode(0.25f);
        Require(current().transform.GetLocalPosition().x == 1 && actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::PAUSE,
            "real Editor single-step runs exactly one frame and returns to Pause");
        editor.UpdateCurrentEditorMode(0.25f);
        Require(current().transform.GetLocalPosition().x == 1, "paused single-step cannot run twice");
        actions.StopPlaying();
        Require(current().transform.GetLocalPosition().x == 0 && current().GetComponent<Box>()->GetSize().x == 1,
            "real Editor Stop restores scene native values and CSharp attachment");
        actions.StartPlaying(); editor.UpdateCurrentEditorMode(0.25f);
        Require(current().transform.GetLocalPosition().x == 0.5f, "second Editor Play creates fresh managed instance");
        actions.StopPlaying(); editor.DrainRenderFrames();
#else
        OvGame::Core::Context context; context.window->Hide();
        OvGame::Core::Game game(context);
        auto& actor = Add(*context.sceneManager.GetCurrentScene());
        for (int i = 0; i < 2; ++i) { game.PreUpdate(); game.Update(0.25f); game.PostUpdate(); }
        game.DrainRenderFrames();
        Require(actor.transform.GetLocalPosition().x == 1 && actor.GetComponent<Box>()->GetSize().x == 1.5f,
            "real Game PreUpdate Update PostUpdate drives CSharp through scene phases");
        auto* lua = actor.GetBehaviour("s3.lua");
        Require(lua && std::get<double>(lua->GetScript()->GetProperty("ticks").value()) == 0.5,
            "real Game loop keeps Lua and CSharp in the same scene");
#endif
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
}
