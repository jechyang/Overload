#pragma once
#include <memory>
#include <unordered_map>
#include <OvCore/Scripting/Common/Script.h>
#include <OvCore/Scripting/Common/ScriptIdentity.h>
#include <OvCore/Scripting/CSharp/HostABI.h>
#include <OvReflection/ScriptBridge.h>
#if defined(LUA_SCRIPTING)
#include <OvCore/Scripting/Lua/LuaScript.h>
#include <OvCore/Scripting/Lua/LuaScriptEngine.h>
#else
#include <OvCore/Scripting/Null/NullScript.h>
#include <OvCore/Scripting/Null/NullScriptEngine.h>
#endif

namespace OvCore::Scripting::CSharp { class RuntimeHost; }
namespace OvCore::Scripting
{
    enum class ScriptStatus { MissingBackend, MissingType, TypeReady, InstanceReady, Faulted };
    class ScriptEngine
    {
    public:
        ScriptEngine(const std::filesystem::path& assets, const std::filesystem::path& engine);
        ~ScriptEngine();
        bool CreateProjectFiles(const std::filesystem::path& folder, bool force = false);
        std::unordered_set<std::string> GetValidExtensions();
        std::string GetDefaultScriptContent(const std::string& name);
        std::string GetDefaultExtension();
        void AddBehaviour(ECS::Components::Behaviour& target);
        void RemoveBehaviour(ECS::Components::Behaviour& target);
        void Reload();
        bool IsOk() const;
        // Loading is metadata-only; instances are constructed at the first Awake.
        bool LoadCSharpProject(CSharp::RuntimeHost& host, const std::filesystem::path& root,
            const std::filesystem::path& assembly, const std::string& logical);
        void ClearCSharpProject();
        OvReflection::Script::Context& GameplayContext(uint64_t context);
        ScriptIdentity ResolveIdentity(const ScriptIdentity& identity) const;
        ScriptStatus Status(const ScriptIdentity& identity) const;
        const std::string& LastError() const { return m_error; }
        void OnAwake(ECS::Components::Behaviour& target);
        void OnStart(ECS::Components::Behaviour& target);
        void OnEnable(ECS::Components::Behaviour& target);
        void OnDisable(ECS::Components::Behaviour& target);
        void OnDestroy(ECS::Components::Behaviour& target);
        void OnUpdate(ECS::Components::Behaviour& target, float dt);
        void OnFixedUpdate(ECS::Components::Behaviour& target, float dt);
        void OnLateUpdate(ECS::Components::Behaviour& target, float dt);
        void OnCollisionEnter(ECS::Components::Behaviour& target, ECS::Components::CPhysicalObject& other);
        void OnCollisionStay(ECS::Components::Behaviour& target, ECS::Components::CPhysicalObject& other);
        void OnCollisionExit(ECS::Components::Behaviour& target, ECS::Components::CPhysicalObject& other);
        void OnTriggerEnter(ECS::Components::Behaviour& target, ECS::Components::CPhysicalObject& other);
        void OnTriggerStay(ECS::Components::Behaviour& target, ECS::Components::CPhysicalObject& other);
        void OnTriggerExit(ECS::Components::Behaviour& target, ECS::Components::CPhysicalObject& other);
    private:
        struct Legacy;
        std::unique_ptr<Legacy> m_legacy;
        std::unordered_set<ECS::Components::Behaviour*> m_behaviours;
        std::unordered_map<std::string, ScriptIdentity> m_types;
        std::string m_error;
        bool m_csharp = false;
        bool CanDispatch(ECS::Components::Behaviour& target) const;
        void DispatchCSharp(ECS::Components::Behaviour& target, uint32_t event, float dt = 0,
            ECS::Components::CPhysicalObject* other = nullptr);
        CSharp::RuntimeHost* m_host = nullptr;
        OvCsSessionHandle m_session{};
        std::unordered_map<std::string, uint64_t> m_tokens;
    };
}
