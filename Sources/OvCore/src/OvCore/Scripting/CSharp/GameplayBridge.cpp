#include <OvCore/Scripting/CSharp/GameplayBridge.h>
#include <OvCore/Scripting/ScriptBridge.h>
#include <OvCore/Scripting/DispatchScope.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/SceneSystem/SceneManager.h>
#include "OvCore/Reflection.generated.h"

namespace OvCore::Scripting::CSharp
{
    uint32_t OVCS_CALL Gameplay(uint64_t context, uint64_t owner, uint32_t operation,
        uint64_t argument, OvCsUtf8View text, uint64_t* output) noexcept
    {
        using namespace OvReflection::Script;
        return Guard(context, [&](State& state) {
            Require(output != nullptr); *output = 0;
            // The mandatory, non-removable Transform is the Actor lifetime anchor.
            // Resolve checks context, captured Main, weak epoch and exact native type.
            auto& actor = state.Get<ECS::Components::CTransform>(owner)->owner;
            DispatchScope callbackScope;
            if (operation >= 5 && operation <= 11 && operation != 9) Require(actor.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
            auto& scripts = OVSERVICE(ScriptEngine);
            const auto value = Decode({text.data, text.length});
            const auto ticket = actor.transform.InspectorTicket();
            auto publish = [&](auto& component) {
                return PublishComponent(scripts.GameplayContext(context), component);
            };
            switch (operation) {
            case 1: *output = actor.GetGUID(); break;
            case 2: *output = owner; break;
            case 3: if (auto* box = actor.GetComponent<ECS::Components::CPhysicalBox>()) *output = publish(*box); break;
            case 4: *output = actor.IsSelfActive(); break;
            case 5: Require(argument <= 1); actor.SetActive(argument != 0); break;
            case 6: actor.MarkAsDestroy(); break;
            case 7: case 8: {
                const auto split = value.find('\n'); Require(split != std::string::npos);
                const auto identity = ScriptIdentity::CSharp(value.substr(0, split), value.substr(split + 1));
                if (operation == 7) DispatchScope::Defer([ticket, &actor, identity] {
                    if (!ticket.expired() && actor.CanRunScripts()) actor.AddBehaviour(identity);
                });
                else actor.RemoveBehaviour(identity);
                break;
            }
            case 9: case 10: case 11: {
                auto& manager = OVSERVICE(SceneSystem::SceneManager);
                auto* scene = manager.GetCurrentScene();
                Require(scene && scene->FindActorByGUID(actor.GetGUID()) == &actor, OV_SCRIPT_INVALID_HANDLE);
                if (operation == 11) { manager.LoadAndPlayDelayed(value); break; }
                auto* found = operation == 9 ? scene->FindActorByName(value) : &scene->CreateActor(value);
                if (found && found->IsAlive()) *output = publish(found->transform);
                break;
            }
            case 12: {
                auto* scene=actor.GetScene(); Require(scene!=nullptr,OV_SCRIPT_INVALID_HANDLE);
                auto* found=scene->FindActorByGUID(argument);
                if(found && found->IsAlive()) *output=publish(found->transform);
                break;
            }
            case 13: {
                auto* scene=actor.GetScene(); Require(scene!=nullptr,OV_SCRIPT_INVALID_HANDLE);
                Require(argument<=9); *output=scene->ResolveScriptAsset(static_cast<uint32_t>(argument),value); break;
            }
            default: Require(false);
            }
        });
    }
}
