#pragma once
#include <OvCore/Scripting/CSharp/BootstrapABI.h>
#include <OvReflection/ScriptBridge.h>
#include <filesystem>
#include <memory>
#include <string>

namespace OvCore::Scripting::CSharp
{
    struct HostPaths
    {
        static std::filesystem::path FromUtf8(const std::string& value)
        { return std::filesystem::path(std::u8string(value.begin(), value.end())); }
        std::filesystem::path dotnetRoot, nethost, runtimeConfig, assembly;
        // Relative settings resolve against an explicit installation directory, never cwd/PATH.
        static HostPaths Resolve(const std::filesystem::path& base,
            const std::filesystem::path& runtime, const std::filesystem::path& managed);
    };

    class RuntimeHost final
    {
    public:
        explicit RuntimeHost(const OvReflection::MainThread& main);
        ~RuntimeHost();
        RuntimeHost(const RuntimeHost&) = delete;
        RuntimeHost& operator=(const RuntimeHost&) = delete;
        void Start(const HostPaths& paths);
        void OpenSession(const std::filesystem::path& projectRoot);
        void CloseSession();
        void Shutdown();
        void Check(OvCsStatus status, const char* operation, OvCsSessionHandle session = {});
        void* Entry(const wchar_t* type, const wchar_t* method) const;
        const OvCsHostApi& Api() const { return m_api; }
        OvCsSessionHandle Session() const { return m_session; }
        OvReflection::Script::Context& NativeContext() { return *m_context; }
        static std::filesystem::path ExecutableDirectory();
    private:
        OvReflection::MainThread m_main;
        std::unique_ptr<OvReflection::Script::Context> m_context;
        OvCsHostApi m_api{};
        OvCsSessionHandle m_session{};
        OvCsDetachFn m_detach = nullptr;
        void* m_loadAssembly = nullptr;
        std::filesystem::path m_assembly;
        bool m_attached = false;
    };
}
