#pragma warning(push)
// Existing engine headers use unused virtual arguments and anonymous POD unions.
#pragma warning(disable: 4100 4201)
#include <OvCore/Scripting/CSharp/RuntimeHost.h>
#include "OvCore/Reflection.generated.h"
#include "OvCore/Script.generated.h"
#include <OvCore/Scripting/ScriptBridge.h>
#include <OvCore/SceneSystem/SceneManager.h>
#include <OvPhysics/Core/PhysicsEngine.h>
#include <OvRendering/Context/Driver.h>
#include <OvRendering/Threading/RenderThread.h>
#include <OvWindowing/Context/Device.h>
#include <OvWindowing/Window.h>
#include <GLFW/glfw3.h>
#pragma warning(pop)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#undef APIENTRY
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace OvCore::Scripting::CSharp;
using Box = OvCore::ECS::Components::CPhysicalBox;
using SmokeFn = OvCsStatus(OVCS_CALL*)(OvCsSessionHandle, uint64_t, uint64_t, uint32_t);
static void Require(bool test, const char* name)
{ if (!test) throw std::runtime_error(name); std::printf("PASS: %s\n", name); }
static OvCsUtf8View View(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
static int rejectedCalls = 0;
static OvScriptStatus OV_SCRIPT_CALL RejectR6(uint32_t, const uint8_t*, uint64_t, OvScriptOvCoreTable*)
{ ++rejectedCalls; return OV_SCRIPT_ABI_MISMATCH; }

struct Engine
{
    OvWindowing::Context::Device device;
    OvWindowing::Window window;
    std::unique_ptr<OvRendering::Context::Driver> driver;
    std::unique_ptr<OvRendering::Threading::RenderThread> render;
    OvPhysics::Core::PhysicsEngine physics{OvPhysics::Settings::PhysicsSettings{}};
    OvCore::SceneSystem::SceneManager scenes;
    static OvWindowing::Settings::DeviceSettings DeviceSettings()
    { OvWindowing::Settings::DeviceSettings s; s.contextMajorVersion=4; s.contextMinorVersion=5; return s; }
    static OvWindowing::Settings::WindowSettings WindowSettings()
    { OvWindowing::Settings::WindowSettings s; s.visible=false; s.focused=false; s.width=64; s.height=64; s.samples=1; return s; }
    explicit Engine(bool threaded) : device(DeviceSettings()), window(device, WindowSettings())
    {
        if (threaded) {
            glfwMakeContextCurrent(nullptr);
            render = std::make_unique<OvRendering::Threading::RenderThread>([this] {
                window.MakeCurrentContext();
                return std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{});
            }, [] { glfwMakeContextCurrent(nullptr); });
        } else {
            window.MakeCurrentContext();
            driver = std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{});
        }
    }
    ~Engine() { scenes.UnloadCurrentScene(); if (render) render->Shutdown(); }
};

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        if (argc != 5) throw std::runtime_error("usage: native-host <dotnet-root> <managed-dir> <project-root> <mode>");
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        const std::wstring mode = argv[4];
        Require(GetModuleHandleW(L"coreclr.dll") == nullptr, "native EXE starts without CLR");
        OvReflection::MainThread main;
        RuntimeHost host(main);
        auto paths = HostPaths::Resolve(RuntimeHost::ExecutableDirectory(), argv[1], argv[2]);
        if (mode == L"missing-config") paths.runtimeConfig += L".missing";
        if (mode == L"missing-assembly") paths.assembly += L".missing";
        if (mode == L"missing-nethost") paths.nethost += L".missing";
        if (mode == L"wrong-architecture") paths.nethost = std::filesystem::path(argv[2]) / "fake-x86.dll";
        if (mode == L"wrong-assembly-architecture") paths.assembly = std::filesystem::path(argv[2]) / "fake-x86.dll";
        if (mode == L"invalid-config") paths.runtimeConfig += L".invalid";
        host.Start(paths);
        Require(GetModuleHandleW(L"coreclr.dll") != nullptr, "native nethost/hostfxr started CLR");
        if (mode == L"production") {
            host.OpenSession(argv[3]);
            bool absent = false;
            try { host.Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost", L"Smoke"); }
            catch (const std::exception&) { absent = true; }
            Require(absent, "production DLL has no acceptance-only Smoke export");
            for (const auto* name : {L"HoldFacade", L"ProbeFacade"}) {
                absent = false;
                try { host.Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost", name); }
                catch (const std::exception&) { absent = true; }
                Require(absent, "production DLL has no acceptance-only facade probe");
            }
            host.Shutdown();
            Require(true, "production DLL native startup and R6 binding");
            return 0;
        }
        const auto& api = host.Api();
        auto get = reinterpret_cast<OvCsGetHostApiFn>(host.Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost", L"GetHostApi"));
        const uint8_t signature[32] = {OVCS_HOST_ABI_SIGNATURE_BYTES};
        for (int test = 0; test < 3; ++test) {
            uint8_t hash[32]; std::memcpy(hash, signature, 32); if (test == 2) hash[0] ^= 1;
            OvCsHostApi rejected{};
            std::memset(&rejected, 0xcc, sizeof(rejected));
            Require(get(test == 0 ? 2 : 1, sizeof(rejected) - (test == 1 ? 8 : 0), hash, &rejected) == OVCS_STATUS_ABI_MISMATCH,
                "Host ABI mismatch rejected");
            const OvCsHostApi empty{};
            Require(std::memcmp(&rejected, &empty, sizeof(empty)) == 0, "Host ABI mismatch publishes no pointers");
        }
        auto smoke = reinterpret_cast<SmokeFn>(host.Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost", L"Smoke"));
        const std::string root = std::filesystem::path(argv[3]).string(), logical = "NativeSmoke";
        OvCsSessionCreateInfo info{sizeof(info), 0, 0, View(root), {}, View(logical)};
        OvCsSessionHandle temporary{};
        host.Check(api.create_session(&info, &temporary), "create temporary session");
        uint8_t coreHash[32]{}; const std::string coreSignature = OV_SCRIPT_OVCORE_SIGNATURE;
        for (size_t i = 0; i < 32; ++i) coreHash[i] = static_cast<uint8_t>(std::stoul(coreSignature.substr(i * 2, 2), nullptr, 16));
        const std::string module = "OvCore";
        for (int test = 0; test < 3; ++test) {
            uint8_t hash[32]; std::memcpy(hash, coreHash, 32); if (test == 2) hash[0] ^= 1;
            Require(api.bind_native_api(temporary, View(module), test == 0 ? 2 : 1,
                sizeof(OvScriptOvCoreTable) - (test == 1 ? 8 : 0), hash,
                reinterpret_cast<uintptr_t>(&OvScript_GetOvCoreApi)) == OVCS_STATUS_NATIVE_ABI_MISMATCH, "R6 metadata mismatch rejected");
        }
        Require(api.bind_native_api(temporary, View(module), 1, sizeof(OvScriptOvCoreTable), coreHash,
            reinterpret_cast<uintptr_t>(&RejectR6)) == OVCS_STATUS_NATIVE_ABI_MISMATCH && rejectedCalls == 1,
            "managed wrapper performs independent native R6 handshake");
        host.Check(api.close_session(temporary), "close temporary session", temporary);
        host.OpenSession(argv[3]);
        const auto session = host.Session();
        uint32_t count = 99;
        host.Check(api.get_type_count(session, &count), "get_type_count", session);
        Require(count == 0, "S1 session has no S2 user types");
        Engine engine(mode == L"render-thread");
        auto& actor = engine.scenes.GetCurrentScene()->CreateActor("S1 real native actor");
        auto& box = actor.AddComponent<Box>(); box.SetMass(0);
        const auto boxId = OvCore::Scripting::PublishComponent(host.NativeContext(), box);
        const auto transformId = OvCore::Scripting::PublishComponent(host.NativeContext(), actor.transform);
        host.Check(smoke(session, boxId, transformId, 0), "Smoke", session);
        const auto boxSize = box.GetSize(); const auto position = actor.transform.GetLocalPosition();
        Require(boxSize.x == 3 && boxSize.y == 4 && boxSize.z == 5 && position.x == 6 && position.y == 7 && position.z == 8,
            "managed R6 writes changed real CPhysicalBox and CTransform");
        OvCsStatus worker = 0;
        std::thread wrong([&] { worker = smoke(session, boxId, transformId, 0); }); wrong.join();
        Require(worker == OVCS_STATUS_WRONG_THREAD, "managed entry rejects worker thread");
        bool nativeRejected = false;
        std::thread wrongNative([&] {
            try { host.CloseSession(); }
            catch (const std::exception&) { nativeRejected = true; }
        }); wrongNative.join();
        Require(nativeRejected && host.Session().id == session.id, "native host rejects worker before closing session");
        if (engine.render) Require(engine.render->Invoke([&](auto&) { return smoke(session, boxId, transformId, 0); }) == OVCS_STATUS_WRONG_THREAD,
            "managed entry rejects RenderThread");
        Require(smoke(session, boxId, transformId, 1) == OVCS_STATUS_MANAGED_EXCEPTION, "managed exception becomes Host status");
        bool logged = false;
        try { host.Check(OVCS_STATUS_MANAGED_EXCEPTION, "deliberate Smoke", session); }
        catch (const std::exception& e) { logged = std::string(e.what()).find("S1 deliberate managed exception") != std::string::npos; }
        Require(logged, "native error logging includes managed exception stack");
        Require(smoke(session, boxId, transformId, 1) == OVCS_STATUS_MANAGED_EXCEPTION, "exception is repeatable without escaping ABI");
        OvCsOwnedUtf8 error{};
        host.Check(api.take_last_error(session, &error), "take_last_error", session);
        const std::string message(reinterpret_cast<char*>(error.data), static_cast<size_t>(error.length));
        Require(message.find("S1 deliberate managed exception") != std::string::npos && message.find("Bootstrap") != std::string::npos,
            "error contains managed message and stack");
        auto copy = error;
        host.Check(api.release_owned_utf8(&error), "release_owned_utf8");
        Require(!error.data && !error.length, "release zeroes owned output");
        Require(api.release_owned_utf8(&copy) == OVCS_STATUS_BUFFER_OWNERSHIP, "double release rejected");
        host.Check(api.take_last_error(session, &error), "consume error again", session);
        Require(!error.data && !error.length, "take_last_error consumes error");
        Require(actor.RemoveComponent(box), "native component removal");
        Require(smoke(session, boxId, transformId, 0) == OVCS_STATUS_MANAGED_EXCEPTION, "R6 rejects removed real component");
        host.CloseSession();
        Require(smoke(session, boxId, transformId, 0) == OVCS_STATUS_SESSION_NOT_FOUND, "closed session rejects calls");
        Require(api.close_session(session) == 0, "same closed handle close is idempotent");
        auto stale = session; ++stale.generation;
        Require(api.close_session(stale) == OVCS_STATUS_SESSION_NOT_FOUND, "stale session generation rejected");
        uint64_t previous = session.id;
        for (int i = 0; i < 25; ++i) {
            host.OpenSession(argv[3]); Require(host.Session().id > previous, "session IDs never reused");
            previous = host.Session().id; host.CloseSession();
        }
        const auto nativeContext = host.NativeContext().Id();
        host.Shutdown(); // Managed bindings first, R6 context second, engine scene below last.
        Require(OvReflection::Script::Guard(nativeContext, [](auto&) {}) == OV_SCRIPT_INVALID_CONTEXT,
            "shutdown invalidates R6 context before native objects");
        Require(GetModuleHandleW(L"coreclr.dll") != nullptr, "hostfxr close/detach keeps CLR resident");
        host.Start(paths); host.OpenSession(argv[3]); host.Shutdown();
        Require(true, "same resident CLR reattaches after host shutdown");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "S1 native host failure: %s\n", error.what()); return 1;
    }
}
