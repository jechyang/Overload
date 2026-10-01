#define wmain HostValidationMain
#include "Tests/Scripting/Integration/native_host_validation.cpp"
#undef wmain
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/ECS/Components/CUI.h>
#include <string>

using namespace OvCore::ECS::Components;
using OvCore::Scripting::ScriptIdentity;

static int Int(Behaviour& b, const char* name)
{
    for (const auto& field : b.GetScript()->ManagedFields())
        if (field.name == name) {
            auto value = b.GetScript()->ReadManagedField(field.token);
            return value ? value->scalar.int32_value : -999;
        }
    throw std::runtime_error(name);
}

static std::string Str(Behaviour& b, const char* name)
{
    for (const auto& field : b.GetScript()->ManagedFields())
        if (field.name == name) {
            auto value = b.GetScript()->ReadManagedField(field.token);
            return value ? value->text : "";
        }
    throw std::runtime_error(name);
}

int wmain(int argc, wchar_t** argv) try
{
    Require(argc == 4, "coroutine arguments: runtime, host, CoroutineFixture.dll");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    OvReflection::MainThread authority;
    RuntimeHost host(authority);
    host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(), argv[1], argv[2]));
    std::filesystem::path assembly = argv[3], project = assembly.parent_path();
    OvCore::Scripting::ScriptEngine scripts(project, project);
    OvCore::Global::ServiceLocator::Provide(scripts);
    Engine engine(false);
    OvCore::Global::ServiceLocator::Provide(engine.scenes);
    Require(scripts.LoadCSharpProject(host, project, assembly, "CoroutineFixture"), "load CoroutineFixture");
    auto& scene = *engine.scenes.GetCurrentScene();
    auto& actor = scene.CreateActor("CoroutineProbe");
    actor.AddComponent<CRectTransform>();
    auto& probe = actor.AddBehaviour(ScriptIdentity::CSharp("CoroutineFixture", "CoroutineFixture.CoroutineProbe"));
    Require(scripts.PrepareCSharp(probe), "prepare CoroutineProbe");
    scene.Play();
    // WaitForSeconds(0.05) plus several frames; TickUI runs in LateUpdate.
    for (int i = 0; i < 12; ++i) {
        scene.Update(0.05f);
        scene.LateUpdate(0.05f);
    }
    int passed = Int(probe, "passed");
    std::string err = Str(probe, "lastError");
    std::fprintf(stderr, "frames=%d waits=%d nested=%d stopped=%d async=%d passed=%d err=%s\n",
        Int(probe, "frames"), Int(probe, "waits"), Int(probe, "nested"),
        Int(probe, "stopped"), Int(probe, "asyncFrames"), passed, err.c_str());
    if (passed != 1) return 1;
    scripts.ClearCSharpProject();
    engine.scenes.UnloadCurrentScene();
    host.Shutdown();
    std::puts("PASS Coroutine + AsyncYield");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "Coroutine validation failed: %s\n", error.what());
    return 1;
}
