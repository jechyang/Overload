#define wmain S1ValidationMain
#include "native_host_validation.cpp"
#undef wmain
#pragma warning(push)
#pragma warning(disable: 4100 4201)
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <tinyxml2.h>
#pragma warning(pop)

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        if (argc != 5) throw std::runtime_error("Null validation requires runtime/managed/project/mode");
        OvCore::Scripting::ScriptEngine scripts(argv[3], argv[3]);
        OvCore::Global::ServiceLocator::Provide(scripts);
        Require(GetModuleHandleW(L"coreclr.dll") == nullptr && scripts.GetDefaultExtension() == ".ovscript", "Null facade works without CLR or Lua backend");
        Engine engine(false);
        auto& actor = engine.scenes.GetCurrentScene()->CreateActor("Null");
        auto& behaviour = actor.AddBehaviour("legacy.lua");
        Require(!behaviour.GetScript() && behaviour.Status() == OvCore::Scripting::ScriptStatus::MissingBackend, "disabled Lua retains empty Behaviour");
        tinyxml2::XMLDocument fields;
        fields.Parse("<data flag='keep'><script_properties><n type='uint64'>18446744073709551615</n><future><child/></future></script_properties></data>");
        behaviour.OnDeserialize(fields, fields.RootElement());
        engine.scenes.GetCurrentScene()->Play(); engine.scenes.GetCurrentScene()->Update(0.5f);
        scripts.Reload(); behaviour.RemoveScript();
        tinyxml2::XMLDocument saved; auto* data = saved.NewElement("data"); saved.InsertEndChild(data); behaviour.OnSerialize(saved, data);
        tinyxml2::XMLPrinter before, after; fields.RootElement()->Accept(&before); data->Accept(&after);
        Require(std::string(before.CStr()) == after.CStr(), "Null instance lifecycle and serialization preserve raw field tree");
        OvReflection::MainThread main; RuntimeHost host(main);
        host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(), argv[1], argv[2]));
        const std::filesystem::path root = argv[3];
        Require(scripts.LoadCSharpProject(host, root, root / "original.dll", "Game"), "CSharp discovery works with Null legacy backend");
        auto& cs = actor.AddBehaviour(OvCore::Scripting::ScriptIdentity::CSharp("Game", "First.Same"));
        Require(cs.Status() == OvCore::Scripting::ScriptStatus::TypeReady, "Null/CSharp backend dispatch resolves type");
        engine.scenes.UnloadCurrentScene(); host.Shutdown();
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "Null FAIL: %s\n", error.what()); return 1; }
}
