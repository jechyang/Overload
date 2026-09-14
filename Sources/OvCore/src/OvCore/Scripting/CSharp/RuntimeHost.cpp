#include <OvCore/Scripting/CSharp/RuntimeHost.h>
#include <OvCore/Scripting/CSharp/GameplayBridge.h>
#include <OvDebug/Logger.h>
#include "OvCore/Script.generated.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <fstream>
#include <vector>
#include <cstring>

namespace
{
    using namespace OvCore::Scripting::CSharp;
    const OvReflection::MainThread* activeMain = nullptr;
    thread_local std::wstring fxrError;
    constexpr wchar_t bootstrapType[] = L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost";
    // Public nethost/hostfxr ABI declarations. No SDK header/link dependency for Lua builds.
    struct GetHostfxrParameters { size_t size; const wchar_t* assembly_path; const wchar_t* dotnet_root; };
    struct InitializeParameters { size_t size; const wchar_t* host_path; const wchar_t* dotnet_root; };
    using GetHostfxr = int(__cdecl*)(wchar_t*, size_t*, const GetHostfxrParameters*);
    using Initialize = int(__cdecl*)(const wchar_t*, const InitializeParameters*, void**);
    using GetDelegate = int(__cdecl*)(void*, int, void**);
    using Close = int(__cdecl*)(void*);
    using ErrorWriter = void(__cdecl*)(const wchar_t*);
    using SetErrorWriter = ErrorWriter(__cdecl*)(ErrorWriter);
    using LoadAssembly = int(__stdcall*)(const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*, void*, void**);

    std::string Utf8(const std::wstring& text)
    {
        if (text.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (!size) throw std::runtime_error("Invalid UTF-16 path/hostfxr diagnostic");
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
        return result;
    }
    void RequireMain(const OvReflection::MainThread& main)
    {
        if (!main.IsCurrent()) throw std::runtime_error("CSharp host requires captured engine Main thread");
    }
    uint32_t OVCS_CALL IsMain() { return activeMain && activeMain->IsCurrent() ? 1u : 0u; }
    void OVCS_CALL Log(uint32_t level, OvCsUtf8View text) noexcept
    {
        try {
            if (!IsMain() || (!text.data && text.length) || text.length > INT_MAX) return;
            const std::string message = "[CSharp] " + std::string(reinterpret_cast<const char*>(text.data), static_cast<size_t>(text.length));
            if (level >= 2) { OVLOG_ERROR(message); } else { OVLOG_INFO(message); }
        } catch (...) { /* Logging must never unwind into CLR. */ }
    }
    void __cdecl FxrError(const wchar_t* text) noexcept
    { try { if (text) fxrError += std::wstring(text) + L"\n"; } catch (...) {} }
    void CheckHr(int status, const char* operation)
    {
        if (status < 0) throw std::runtime_error(std::string(operation) + " failed status=" + std::to_string(status) + ": " + Utf8(fxrError));
    }
    void File(const std::filesystem::path& path, bool pe)
    {
        if (!path.is_absolute() || !std::filesystem::is_regular_file(path))
            throw std::runtime_error("Missing absolute CSharp dependency: " + Utf8(path.wstring()));
        if (!pe) return;
        std::ifstream input(path, std::ios::binary);
        IMAGE_DOS_HEADER dos{}; input.read(reinterpret_cast<char*>(&dos), sizeof(dos));
        if (!input || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0)
            throw std::runtime_error("Invalid PE: " + Utf8(path.wstring()));
        input.seekg(dos.e_lfanew);
        DWORD signature{}; IMAGE_FILE_HEADER header{};
        input.read(reinterpret_cast<char*>(&signature), sizeof(signature));
        input.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!input || signature != IMAGE_NT_SIGNATURE || header.Machine != IMAGE_FILE_MACHINE_AMD64)
            throw std::runtime_error("CSharp requires x64 PE (0x8664): " + Utf8(path.wstring()));
    }
    HMODULE LoadResident(const std::filesystem::path& path)
    {
        File(path, true);
        auto module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) throw std::runtime_error("LoadLibraryEx failed Win32=" + std::to_string(GetLastError()) + ": " + Utf8(path.wstring()));
        // Intentional process lifetime: CLR and any published entry must remain executable.
        return module;
    }
    template<class T> T Symbol(HMODULE module, const char* name)
    {
        auto address = GetProcAddress(module, name);
        if (!address) throw std::runtime_error(std::string("Missing host export: ") + name);
        return reinterpret_cast<T>(address);
    }
    OvCsUtf8View View(const std::string& value)
    { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
    std::filesystem::path ExecutablePath()
    {
        std::vector<wchar_t> buffer(32768);
        const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!size || size == buffer.size()) throw std::runtime_error("Cannot resolve executable path");
        return std::filesystem::path(std::wstring(buffer.data(), size));
    }
}

namespace OvCore::Scripting::CSharp
{
    HostPaths HostPaths::Resolve(const std::filesystem::path& base, const std::filesystem::path& runtime, const std::filesystem::path& managed)
    {
        if (!base.is_absolute()) throw std::invalid_argument("CSharp installation base must be absolute");
        auto resolve = [&](const auto& value) { return (value.is_absolute() ? value : base / value).lexically_normal(); };
        HostPaths result;
        result.dotnetRoot = resolve(runtime);
        result.nethost = result.dotnetRoot / "packs/Microsoft.NETCore.App.Host.win-x64/10.0.12/runtimes/win-x64/native/nethost.dll";
        result.assembly = resolve(managed) / "Overload.ManagedHost.dll";
        result.runtimeConfig = resolve(managed) / "Overload.ManagedHost.runtimeconfig.json";
        return result;
    }
    std::filesystem::path RuntimeHost::ExecutableDirectory()
    {
        return ExecutablePath().parent_path();
    }
    RuntimeHost::RuntimeHost(const OvReflection::MainThread& main) : m_main(main)
    { RequireMain(m_main); }
    RuntimeHost::~RuntimeHost()
    {
        // A host belongs to Main; destroying on another thread would leave live callbacks.
        if (!m_main.IsCurrent()) std::terminate();
        try { Shutdown(); } catch (...) { std::terminate(); }
    }
    void RuntimeHost::Start(const HostPaths& paths)
    {
        RequireMain(m_main);
        if (m_attached || activeMain) throw std::runtime_error("Only one native CSharp host may be attached");
        File(paths.runtimeConfig, false); File(paths.assembly, true);
        const auto expectedFxr = paths.dotnetRoot / "host/fxr/10.0.12/hostfxr.dll";
        const auto expectedClr = paths.dotnetRoot / "shared/Microsoft.NETCore.App/10.0.12/coreclr.dll";
        File(expectedFxr, true); File(expectedClr, true);
        if (auto existing = GetModuleHandleW(L"coreclr.dll")) {
            wchar_t location[32768]{};
            if (!GetModuleFileNameW(existing, location, 32768) || !std::filesystem::equivalent(location, expectedClr))
                throw std::runtime_error("Process already contains a different CoreCLR; restart with the locked runtime");
        }
        auto net = LoadResident(paths.nethost);
        const GetHostfxrParameters parameters{sizeof(GetHostfxrParameters), nullptr, paths.dotnetRoot.c_str()};
        std::array<wchar_t, 32768> fxrPath{}; size_t size = fxrPath.size();
        CheckHr(Symbol<GetHostfxr>(net, "get_hostfxr_path")(fxrPath.data(), &size, &parameters), "nethost get_hostfxr_path");
        if (!std::filesystem::equivalent(fxrPath.data(), expectedFxr))
            throw std::runtime_error("nethost selected an unexpected hostfxr: " + Utf8(fxrPath.data()));
        auto fxr = LoadResident(expectedFxr);
        auto setError = Symbol<SetErrorWriter>(fxr, "hostfxr_set_error_writer");
        auto previous = setError(&FxrError);
        struct WriterScope { SetErrorWriter set; ErrorWriter old; ~WriterScope() { set(old); } } writer{setError, previous};
        fxrError.clear();
        auto close = Symbol<Close>(fxr, "hostfxr_close");
        void* handle = nullptr;
        const auto executable = ExecutablePath();
        const InitializeParameters initialize{sizeof(InitializeParameters), executable.c_str(), paths.dotnetRoot.c_str()};
        int status = Symbol<Initialize>(fxr, "hostfxr_initialize_for_runtime_config")(paths.runtimeConfig.c_str(), &initialize, &handle);
        struct HandleScope { Close close; void* handle; ~HandleScope() { if (handle) close(handle); } } scope{close, handle};
        CheckHr(status, "hostfxr_initialize_for_runtime_config");
        CheckHr(Symbol<GetDelegate>(fxr, "hostfxr_get_runtime_delegate")(handle, 5, &m_loadAssembly), "hostfxr_get_runtime_delegate");
        wchar_t loadedClr[32768]{};
        if (!GetModuleFileNameW(GetModuleHandleW(L"coreclr.dll"), loadedClr, 32768) ||
            !std::filesystem::equivalent(loadedClr, expectedClr)) {
            m_loadAssembly = nullptr;
            throw std::runtime_error("hostfxr loaded a different CoreCLR than the explicit locked runtime");
        }
        m_assembly = paths.assembly;
        try {
            const uint8_t signature[32] = {OVCS_HOST_ABI_SIGNATURE_BYTES};
            auto get = reinterpret_cast<OvCsGetHostApiFn>(Entry(bootstrapType, L"GetHostApi"));
            const auto result = get(OVCS_HOST_ABI_VERSION, sizeof(m_api), signature, &m_api);
            if (result || m_api.abi_version != OVCS_HOST_ABI_VERSION || m_api.table_size != sizeof(m_api) ||
                std::memcmp(signature, m_api.signature, sizeof(signature)))
                throw std::runtime_error("Host ABI version/size/signature handshake failed");
            if (!m_api.release_owned_utf8 || !m_api.release_field_value || !m_api.take_last_error ||
                !m_api.create_session || !m_api.close_session || !m_api.bind_native_api ||
                !m_api.get_type_count || !m_api.get_type_info || !m_api.get_type_alias || !m_api.release_type_info ||
                !m_api.get_field_info || !m_api.get_field_alias || !m_api.release_field_info ||
                !m_api.create_instance || !m_api.destroy_instance || !m_api.dispatch_lifecycle || !m_api.read_field || !m_api.write_field)
                throw std::runtime_error("Host ABI omitted a required entry");
            m_detach = reinterpret_cast<OvCsDetachFn>(Entry(bootstrapType, L"Detach"));
            auto configure = reinterpret_cast<OvCsConfigureFn>(Entry(bootstrapType, L"Configure"));
            m_context = std::make_unique<OvReflection::Script::Context>(m_main);
            activeMain = &m_main;
            OvCsBootstrapServices services{1, sizeof(services), m_context->Id(), &IsMain, &Log};
            Check(configure(&services), "Configure");
            m_attached = true;
            auto gameplay = reinterpret_cast<ConfigureGameplayFn>(Entry(bootstrapType, L"ConfigureGameplay"));
            Check(gameplay(1, sizeof(GameplayFn), &Gameplay), "ConfigureGameplay");
        } catch (...) {
            if (m_attached && m_detach) m_detach();
            m_attached = false;
            activeMain = nullptr; m_context.reset(); m_api = {}; m_detach = nullptr; m_loadAssembly = nullptr;
            throw;
        }
    }
    void* RuntimeHost::Entry(const wchar_t* type, const wchar_t* method) const
    {
        RequireMain(m_main);
        if (!m_loadAssembly) throw std::runtime_error("CLR delegate is unavailable");
        void* result = nullptr;
        CheckHr(reinterpret_cast<LoadAssembly>(m_loadAssembly)(m_assembly.c_str(), type, method,
            reinterpret_cast<const wchar_t*>(static_cast<intptr_t>(-1)), nullptr, &result), "load_assembly_and_get_function_pointer");
        if (!result) throw std::runtime_error("Managed entry is null");
        return result;
    }
    void RuntimeHost::Check(OvCsStatus status, const char* operation, OvCsSessionHandle session)
    {
        RequireMain(m_main);
        if (!status) return;
        std::string error;
        if (m_api.take_last_error) {
            OvCsOwnedUtf8 owned{};
            if (m_api.take_last_error(session, &owned) == 0) {
                if (owned.data) error.assign(reinterpret_cast<const char*>(owned.data), static_cast<size_t>(owned.length));
                m_api.release_owned_utf8(&owned);
            }
        }
        const std::string message = std::string(operation) + " session=" + std::to_string(session.id) +
            " status=" + std::to_string(status) + " " + error;
        OVLOG_ERROR("[CSharp] " + message);
        throw std::runtime_error(message);
    }
    void RuntimeHost::OpenSession(const std::filesystem::path& projectRoot)
    {
        RequireMain(m_main);
        if (!m_attached || m_session.id) throw std::runtime_error("CSharp host is not ready for a new session");
        const auto root = Utf8(projectRoot.wstring()); const std::string logical = "S1.Bootstrap";
        OvCsSessionCreateInfo info{sizeof(info), 0, 0, View(root), {}, View(logical)};
        Check(m_api.create_session(&info, &m_session), "create_session");
        try {
            // The generated native entry validates against the managed wrapper's independent contract.
            const std::string module = "OvCore";
            uint8_t signature[32]{};
            const std::string hex = OV_SCRIPT_OVCORE_SIGNATURE;
            for (size_t i = 0; i < 32; ++i) signature[i] = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
            Check(m_api.bind_native_api(m_session, View(module), 1, sizeof(OvScriptOvCoreTable), signature,
                reinterpret_cast<uintptr_t>(&OvScript_GetOvCoreApi)), "bind_native_api", m_session);
        } catch (...) { CloseSession(); throw; }
    }
    void RuntimeHost::CloseSession()
    {
        RequireMain(m_main);
        if (m_session.id) { Check(m_api.close_session(m_session), "close_session", m_session); m_session = {}; }
    }
    void RuntimeHost::Shutdown()
    {
        RequireMain(m_main);
        if (!m_attached) return;
        CloseSession();
        Check(m_detach(), "Detach"); // Drops all resident managed references to native functions/context.
        m_attached = false; activeMain = nullptr;
        m_context.reset(); // R6 registry invalidated while native scene/components are still alive.
        m_api = {}; m_detach = nullptr; m_loadAssembly = nullptr;
        // hostfxr_close already released the initialization handle. CoreCLR stays resident.
    }
}
