#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace OvEditor::Core
{
    class Context;
    // Editor-only build coordinator. All public methods are Main-thread operations.
    class CSharpProject
    {
    public:
        explicit CSharpProject(Context& context);
        ~CSharpProject();
        bool CreateScript(const std::filesystem::path& destination);
        void RequestBuild(const std::string& configuration = "Debug");
        void SetAutomaticBuild(bool enabled);
        void SetHotReload(bool enabled);
        bool Tick(bool stopped); // true only when the catalogue changed at this safe point
        const std::string& Status() const { return m_status; }
        const std::string& Diagnostics() const { return m_diagnostics; }
        const std::filesystem::path& AppliedGeneration() const { return m_applied; }
        bool Busy() const { return m_process != nullptr || m_requested; }
        bool Pending() const { return !m_pending.empty(); }
        const std::filesystem::path& PendingGeneration() const { return m_pending; }
        bool watch = true;
        bool hotReload = false; // Explicit opt-in to supported runtime field migration.
    private:
        void CheckMain() const;
        void EnsureProject();
        void ResolveTools();
        void Launch();
        void Fail(const std::string& error);
        std::string Snapshot() const;
        Context& m_context;
        const std::thread::id m_main = std::this_thread::get_id();
        std::filesystem::path m_engine, m_node, m_python, m_cache, m_result, m_log, m_pending, m_applied;
        void* m_process = nullptr;
        void* m_job = nullptr;
        bool m_requested = false, m_dirty = false;
        std::string m_configuration = "Debug", m_buildConfiguration, m_status = "C#: ready to create or build", m_diagnostics, m_snapshot;
        std::chrono::steady_clock::time_point m_scan{}, m_changed{};
    };
}
