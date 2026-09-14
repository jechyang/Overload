#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/Scripting/CSharp/RuntimeHost.h>
#include <OvCore/ECS/Components/Behaviour.h>
#include <OvDebug/Logger.h>
#include <OvCore/ECS/Actor.h>
#include <OvCore/Scripting/ScriptBridge.h>
#include <OvCore/Scripting/DispatchScope.h>
#include "OvCore/Reflection.generated.h"
#include "OvCore/Script.generated.h"

namespace OvCore::Scripting
{
#if defined(LUA_SCRIPTING)
    struct ScriptEngine::Legacy : LuaScriptEngine { using LuaScriptEngine::LuaScriptEngine; };
#else
    struct ScriptEngine::Legacy : NullScriptEngine { using NullScriptEngine::NullScriptEngine; };
#endif
    namespace
    {
        // Type attachment in edit mode, session-owned instance from the first Awake.
        class CSharpTypeScript final : public Script
        {
        public:
            CSharp::RuntimeHost* host = nullptr;
            OvCsSessionHandle session{};
            OvCsInstanceHandle instance{};
            bool faulted = false;
            ~CSharpTypeScript() override {
                DispatchScope callbackScope;
                if (instance.id && host && host->Api().destroy_instance) {
                    try { host->Check(host->Api().destroy_instance(session, instance), "destroy instance", session); }
                    catch (const std::exception& error) { OVLOG_ERROR(error.what()); }
                }
            }
            bool IsValid() const override { return instance.id != 0 && !faulted; }
            bool IsFaulted() const override { return faulted; }
            std::map<std::string, ScriptPropertyValue> GetDefaultProperties() const override { return {}; }
            std::optional<ScriptPropertyValue> GetProperty(const std::string&) const override { return {}; }
            void SetProperty(const std::string&, const ScriptPropertyValue&) override {}
        };
        OvCsUtf8View View(const std::string& value)
        { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
        std::string Text(const OvCsOwnedUtf8& value)
        { return value.data ? std::string(reinterpret_cast<const char*>(value.data), static_cast<size_t>(value.length)) : std::string{}; }
        std::string PathText(const std::filesystem::path& path)
        { const auto text = path.u8string(); return {reinterpret_cast<const char*>(text.data()), text.size()}; }
    }
    ScriptEngine::ScriptEngine(const std::filesystem::path& assets, const std::filesystem::path& engine) : m_legacy(std::make_unique<Legacy>(assets, engine)) {}
    ScriptEngine::~ScriptEngine() { ClearCSharpProject(); }
    bool ScriptEngine::CreateProjectFiles(const std::filesystem::path& folder, bool force) { return m_legacy->CreateProjectFiles(folder, force); }
    std::unordered_set<std::string> ScriptEngine::GetValidExtensions() { return m_legacy->GetValidExtensions(); }
    std::string ScriptEngine::GetDefaultScriptContent(const std::string& name) { return m_legacy->GetDefaultScriptContent(name); }
    std::string ScriptEngine::GetDefaultExtension() { return m_legacy->GetDefaultExtension(); }
    bool ScriptEngine::IsOk() const { return m_legacy->IsOk() && m_error.empty(); }
    ScriptIdentity ScriptEngine::ResolveIdentity(const ScriptIdentity& identity) const
    {
        if (const auto it = m_types.find(identity.Key()); it != m_types.end()) return it->second;
        return identity;
    }
    ScriptStatus ScriptEngine::Status(const ScriptIdentity& identity) const
    {
        if (identity.backend == "csharp") return !m_csharp ? ScriptStatus::MissingBackend :
            m_types.contains(identity.Key()) ? ScriptStatus::TypeReady : ScriptStatus::MissingType;
#if defined(LUA_SCRIPTING)
        if (identity.backend == "lua") return ScriptStatus::InstanceReady;
#endif
        return ScriptStatus::MissingBackend;
    }
    void ScriptEngine::AddBehaviour(ECS::Components::Behaviour& target)
    {
        if (!m_behaviours.insert(&target).second) return;
        if (target.Identity().backend == "lua") m_legacy->AddBehaviour(target);
        else if (target.Status() == ScriptStatus::TypeReady) target.SetScript(std::make_unique<CSharpTypeScript>());
    }
    void ScriptEngine::RemoveBehaviour(ECS::Components::Behaviour& target)
    {
        if (!m_behaviours.erase(&target)) return;
        if (target.Identity().backend == "lua") m_legacy->RemoveBehaviour(target);
        else target.RemoveScript();
    }
    void ScriptEngine::Reload()
    {
        m_legacy->Reload();
        for (auto* target : m_behaviours) if (target->Identity().backend == "csharp") {
            target->RemoveScript();
            if (target->Status() == ScriptStatus::TypeReady) target->SetScript(std::make_unique<CSharpTypeScript>());
        }
    }
    void ScriptEngine::ClearCSharpProject()
    {
        if (DispatchScope::Active()) { DispatchScope::Defer([this] { ClearCSharpProject(); }); return; }
        DispatchScope scope;
        for (auto* target : m_behaviours) if (target->Identity().backend == "csharp") target->RemoveScript();
        if (m_session.id && m_host && m_host->Api().close_session) {
            try { m_host->Check(m_host->Api().close_session(m_session), "close project", m_session); }
            catch (const std::exception& error) { OVLOG_ERROR(error.what()); }
        }
        m_session = {}; m_host = nullptr; m_tokens.clear();
        m_types.clear(); m_csharp = false; m_error.clear();
    }
    OvReflection::Script::Context& ScriptEngine::GameplayContext(uint64_t context)
    {
        OvReflection::Script::Require(m_host && m_host->NativeContext().Id() == context, OV_SCRIPT_INVALID_CONTEXT);
        return m_host->NativeContext();
    }
    bool ScriptEngine::LoadCSharpProject(CSharp::RuntimeHost& host, const std::filesystem::path& root,
        const std::filesystem::path& assembly, const std::string& logical)
    {
        OvCsSessionHandle session{};
        const auto& api = host.Api();
        try {
            if (DispatchScope::Active()) throw std::runtime_error("Project loading requires a lifecycle safe point");
            if (!api.create_session) throw std::runtime_error("CSharp backend is not started");
            if (assembly.empty()) throw std::runtime_error("Project assembly path is empty");
            const auto rootText = PathText(root), assemblyText = PathText(assembly);
            OvCsSessionCreateInfo info{sizeof(info), 0, 0, View(rootText), View(assemblyText), View(logical)};
            host.Check(api.create_session(&info, &session), "discover project");
            std::unordered_map<std::string, ScriptIdentity> candidate;
            std::unordered_map<std::string, uint64_t> tokens;
            uint32_t count = 0;
            host.Check(api.get_type_count(session, &count), "get_type_count", session);
            for (uint32_t i = 0; i < count; ++i) {
                OvCsTypeInfo type{};
                host.Check(api.get_type_info(session, i, &type), "get_type_info", session);
                try {
                    auto identity = ScriptIdentity::CSharp(Text(type.assembly_logical_name), Text(type.canonical_type_name), Text(type.stable_id));
                    tokens.emplace(identity.Key(), type.type_token);
                    if (!candidate.emplace(identity.Key(), identity).second) throw std::runtime_error("Duplicate native identity key");
                    for (uint32_t a = 0; a < type.alias_count; ++a) {
                        OvCsOwnedUtf8 alias{};
                        host.Check(api.get_type_alias(session, type.type_token, a, &alias), "get_type_alias", session);
                        std::string key;
                        try { key = "\x1f" + ScriptIdentity::Part("csharp") + Text(alias); }
                        catch (...) { api.release_owned_utf8(&alias); throw; }
                        host.Check(api.release_owned_utf8(&alias), "release alias");
                        if (!candidate.emplace(key, identity).second) throw std::runtime_error("Duplicate native identity key");
                    }
                } catch (...) { api.release_type_info(&type); throw; }
                host.Check(api.release_type_info(&type), "release_type_info");
            }
            std::unordered_map<ECS::Actor*, std::unordered_set<std::string>> actorKeys;
            for (auto* target : m_behaviours) {
                const auto key = target->CurrentIdentity().Key();
                const auto it = candidate.find(key);
                const auto resolved = it == candidate.end() ? key : it->second.Key();
                if (!actorKeys[&target->owner].insert(resolved).second)
                    throw std::runtime_error("Project aliases would merge existing Actor script records");
            }
            const std::string module = "OvCore";
            uint8_t signature[32]{}; const std::string hex = OV_SCRIPT_OVCORE_SIGNATURE;
            for (size_t i = 0; i < 32; ++i) signature[i] = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
            host.Check(api.bind_native_api(session, View(module), 1, sizeof(OvScriptOvCoreTable), signature,
                reinterpret_cast<uintptr_t>(&OvScript_GetOvCoreApi)), "bind project R6", session);
            ClearCSharpProject();
            m_host = &host; m_session = session; session = {}; m_tokens = std::move(tokens);
            m_types = std::move(candidate); m_csharp = true; m_error.clear();
            for (auto* target : m_behaviours) if (target->Identity().backend == "csharp") {
                target->RefreshIdentity();
                target->RemoveScript();
                if (target->Status() == ScriptStatus::TypeReady) target->SetScript(std::make_unique<CSharpTypeScript>());
            }
            return true;
        } catch (const std::exception& error) {
            if (session.id) api.close_session(session);
            m_error = error.what(); OVLOG_ERROR(m_error);
            return false; // The previous successfully validated catalogue stays usable.
        }
    }
    bool ScriptEngine::CanDispatch(ECS::Components::Behaviour& target) const
    {
        auto script = target.GetScript();
        return target.Identity().backend == "lua" && script && script->IsValid();
    }

    void ScriptEngine::DispatchCSharp(ECS::Components::Behaviour& target, uint32_t event, float dt,
        ECS::Components::CPhysicalObject* other)
    {
        if (!m_host || target.Identity().backend != "csharp") return;
        auto script = target.GetScript();
        auto* runtime = script ? dynamic_cast<CSharpTypeScript*>(&script.value()) : nullptr;
        if (!runtime || (runtime->faulted && event != OVCS_LIFECYCLE_DESTROY)) return;
        try {
            if (!runtime->instance.id) {
                if (event != OVCS_LIFECYCLE_AWAKE) return;
                const auto token = m_tokens.find(target.CurrentIdentity().Key());
                if (token == m_tokens.end()) return;
                runtime->host = m_host; runtime->session = m_session;
                const auto owner = PublishComponent(m_host->NativeContext(), target.owner.transform);
                OvCsInstanceCreateInfo info{sizeof(info), 0, token->second, m_host->NativeContext().Id(), owner, target.owner.GetGUID()};
                m_host->Check(m_host->Api().create_instance(m_session, &info, &runtime->instance), "construct instance", m_session);
            }
            const auto otherHandle = other ? PublishComponent(m_host->NativeContext(), other->owner.transform) : 0;
            OvCsLifecycleCall call{sizeof(call), event, runtime->instance, dt, 0, otherHandle};
            m_host->Check(m_host->Api().dispatch_lifecycle(m_session, &call), "lifecycle", m_session);
        } catch (const std::exception& error) { runtime->faulted = true; OVLOG_ERROR(error.what()); }
    }
#define OV_FORWARD(Name, Event) void ScriptEngine::Name(ECS::Components::Behaviour& target) { DispatchScope scope; if (CanDispatch(target)) m_legacy->Name(target); else DispatchCSharp(target, Event); }
    OV_FORWARD(OnAwake, 1) OV_FORWARD(OnStart, 2) OV_FORWARD(OnEnable, 3) OV_FORWARD(OnDisable, 4) OV_FORWARD(OnDestroy, 14)
#undef OV_FORWARD
#define OV_FORWARD(Name, Event) void ScriptEngine::Name(ECS::Components::Behaviour& target, float dt) { DispatchScope scope; if (CanDispatch(target)) m_legacy->Name(target, dt); else DispatchCSharp(target, Event, dt); }
    OV_FORWARD(OnUpdate, 5) OV_FORWARD(OnFixedUpdate, 6) OV_FORWARD(OnLateUpdate, 7)
#undef OV_FORWARD
#define OV_FORWARD(Name, Event) void ScriptEngine::Name(ECS::Components::Behaviour& target, ECS::Components::CPhysicalObject& other) { DispatchScope scope; if (CanDispatch(target)) m_legacy->Name(target, other); else DispatchCSharp(target, Event, 0, &other); }
    OV_FORWARD(OnCollisionEnter, 8) OV_FORWARD(OnCollisionStay, 9) OV_FORWARD(OnCollisionExit, 10)
    OV_FORWARD(OnTriggerEnter, 11) OV_FORWARD(OnTriggerStay, 12) OV_FORWARD(OnTriggerExit, 13)
#undef OV_FORWARD
}
