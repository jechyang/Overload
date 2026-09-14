#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/Scripting/CSharp/RuntimeHost.h>
#include <OvCore/ECS/Components/Behaviour.h>
#include <OvDebug/Logger.h>

namespace OvCore::Scripting
{
#if defined(LUA_SCRIPTING)
    struct ScriptEngine::Legacy : LuaScriptEngine { using LuaScriptEngine::LuaScriptEngine; };
#else
    struct ScriptEngine::Legacy : NullScriptEngine { using NullScriptEngine::NullScriptEngine; };
#endif
    namespace
    {
        // A resolved type attachment, not a constructed managed object. S3 creates
        // Prepared instances together with the controlled Owner bridge.
        class CSharpTypeScript final : public Script
        {
            bool IsValid() const override { return false; }
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
    ScriptEngine::~ScriptEngine() = default;
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
        m_types.clear(); m_csharp = false; m_error.clear();
        for (auto* target : m_behaviours) if (target->Identity().backend == "csharp") target->RemoveScript();
    }
    bool ScriptEngine::LoadCSharpProject(CSharp::RuntimeHost& host, const std::filesystem::path& root,
        const std::filesystem::path& assembly, const std::string& logical)
    {
        OvCsSessionHandle session{};
        const auto& api = host.Api();
        try {
            if (!api.create_session) throw std::runtime_error("CSharp backend is not started");
            if (assembly.empty()) throw std::runtime_error("Project assembly path is empty");
            const auto rootText = PathText(root), assemblyText = PathText(assembly);
            OvCsSessionCreateInfo info{sizeof(info), 0, 0, View(rootText), View(assemblyText), View(logical)};
            host.Check(api.create_session(&info, &session), "discover project");
            std::unordered_map<std::string, ScriptIdentity> candidate;
            uint32_t count = 0;
            host.Check(api.get_type_count(session, &count), "get_type_count", session);
            for (uint32_t i = 0; i < count; ++i) {
                OvCsTypeInfo type{};
                host.Check(api.get_type_info(session, i, &type), "get_type_info", session);
                try {
                    auto identity = ScriptIdentity::CSharp(Text(type.assembly_logical_name), Text(type.canonical_type_name), Text(type.stable_id));
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
            // Retain persistent metadata only, never expired type tokens. S3 owns
            // runtime sessions/instances; S6 adds generation swapping and hot reload.
            host.Check(api.close_session(session), "close discovery session", session); session = {};
            std::unordered_map<ECS::Actor*, std::unordered_set<std::string>> actorKeys;
            for (auto* target : m_behaviours) {
                const auto key = target->CurrentIdentity().Key();
                const auto it = candidate.find(key);
                const auto resolved = it == candidate.end() ? key : it->second.Key();
                if (!actorKeys[&target->owner].insert(resolved).second)
                    throw std::runtime_error("Project aliases would merge existing Actor script records");
            }
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
#define OV_FORWARD(Name) void ScriptEngine::Name(ECS::Components::Behaviour& target) { if (CanDispatch(target)) m_legacy->Name(target); }
    OV_FORWARD(OnAwake) OV_FORWARD(OnStart) OV_FORWARD(OnEnable) OV_FORWARD(OnDisable) OV_FORWARD(OnDestroy)
#undef OV_FORWARD
#define OV_FORWARD(Name) void ScriptEngine::Name(ECS::Components::Behaviour& target, float dt) { if (CanDispatch(target)) m_legacy->Name(target, dt); }
    OV_FORWARD(OnUpdate) OV_FORWARD(OnFixedUpdate) OV_FORWARD(OnLateUpdate)
#undef OV_FORWARD
#define OV_FORWARD(Name) void ScriptEngine::Name(ECS::Components::Behaviour& target, ECS::Components::CPhysicalObject& other) { if (CanDispatch(target)) m_legacy->Name(target, other); }
    OV_FORWARD(OnCollisionEnter) OV_FORWARD(OnCollisionStay) OV_FORWARD(OnCollisionExit)
    OV_FORWARD(OnTriggerEnter) OV_FORWARD(OnTriggerStay) OV_FORWARD(OnTriggerExit)
#undef OV_FORWARD
}
