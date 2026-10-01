#define wmain HostValidationMain
#include "Tests/Scripting/Integration/native_host_validation.cpp"
#undef wmain
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/ResourceManagement/FontManager.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/UI/Frame.h>
#include <string>

using namespace OvCore::ECS::Components;
using OvCore::Scripting::ScriptIdentity;

static Behaviour& Add(OvCore::ECS::Actor& actor, const char* name)
{ return actor.AddBehaviour(ScriptIdentity::CSharp("GameScripts", name)); }

int wmain(int argc, wchar_t** argv) try
{
    Require(argc == 4, "flappy arguments: runtime, host, GameScripts.dll");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    OvReflection::MainThread authority;
    RuntimeHost host(authority);
    host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(), argv[1], argv[2]));
    std::filesystem::path assembly = argv[3], project = assembly.parent_path();
    OvCore::ResourceManagement::FontManager::ProvideAssetPaths(
        project, std::filesystem::current_path() / "Resources" / "Engine");
    OvCore::Scripting::ScriptEngine scripts(project, project);
    OvCore::Global::ServiceLocator::Provide(scripts);
    Engine engine(false);
    OvCore::Global::ServiceLocator::Provide(engine.scenes);
    Require(scripts.LoadCSharpProject(host, project, assembly, "GameScripts"), "load GameScripts");

    auto& scene = *engine.scenes.GetCurrentScene();
    auto& canvas = scene.CreateActor("Canvas");
    canvas.AddComponent<CCanvas>();
    canvas.AddComponent<CRectTransform>();
    auto& game = Add(canvas, "Flappy.FlappyBirdGame");
    OvCore::UI::ExtractFrame(scene, {400, 600});
    Require(scripts.PrepareCSharp(game), "prepare FlappyBirdGame");
    Require(game.GetScript() && game.GetScript()->IsValid(), "FlappyBirdGame prepared");

    // Edit-mode UI rebuild should already materialize the preview via Graphic mesh.
    OvCore::UI::ExtractFrame(scene, {400, 600});
    Require(game.GetScript() && game.GetScript()->IsValid(), "FlappyBirdGame prepared");
    bool hasMesh = false;
    for (auto* actor : scene.GetActors())
        if (actor->GetComponent<CCanvasRenderer>()) hasMesh = true;
    Require(hasMesh, "edit-mode canvas renderer exists");

    scene.Play();
    for (int i = 0; i < 8; i++) scene.Update(1.0f / 60.0f);
    OvCore::UI::ExtractFrame(scene, {400, 600});
    hasMesh = false;
    for (auto* actor : scene.GetActors())
        if (actor->GetComponent<CCanvasRenderer>()) hasMesh = true;
    Require(hasMesh, "canvas renderer survives simulation ticks");

    scripts.ClearCSharpProject();
    engine.scenes.UnloadCurrentScene();
    host.Shutdown();
    std::puts("PASS FlappyBird uGUI smoke");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "FlappyBird smoke failed: %s\n", error.what());
    return 1;
}
