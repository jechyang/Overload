#include <OvCore/Scripting/CSharp/GameplayBridge.h>
#include <OvCore/Scripting/CSharp/UIBridge.h>
#include <OvCore/Scripting/CSharp/DrivenBridge.h>
#include <OvCore/Scripting/CSharp/InputBridge.h>
#include <OvCore/Scripting/CSharp/FontBridge.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/UI/Frame.h>
#include <bit>
#include <tinyxml2.h>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <charconv>
#include <limits>
#include <span>
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
            if (operation >= 100 && operation <= 103)
            {
                // These dedicated operations carry a pinned mutable binary buffer,
                // not UTF-8. All ordinary gameplay text remains read-only.
                Require(text.data != nullptr && text.length >= 64 && text.length <= 1024 * 1024);
                QueryFont(state, argument, owner, operation,
                    std::span<uint8_t>(const_cast<uint8_t*>(text.data), static_cast<size_t>(text.length)), *output);
                return;
            }
            if (operation >= 80 && operation <= 88)
            {
                QueryInput(state, owner, operation, argument, Decode({text.data, text.length}), *output);
                return;
            }
            if (operation >= 60 && operation <= 62)
            {
                const auto request = Decode({text.data, text.length});
                const auto separator = request.find('\n'); Require(separator != std::string::npos);
                uint64_t session = 0;
                const auto parsed = std::from_chars(request.data(), request.data() + separator, session);
                Require(parsed.ec == std::errc{} && parsed.ptr == request.data() + separator && session != 0);
                QueryDriven(state, session, owner, operation, argument, request.substr(separator + 1), *output);
                return;
            }
            if (operation == 45)
            {
                Require(text.length <= std::numeric_limits<size_t>::max());
                UploadUIMesh(state, owner, std::span<const uint8_t>(text.data, static_cast<size_t>(text.length)), *output);
                return;
            }
            if (operation >= 40 && operation <= 59)
            {
                QueryUI(state, owner, operation, argument, Decode({text.data, text.length}), *output);
                return;
            }
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
            case 29: {
                Require(actor.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
                auto* scene=actor.GetScene();Require(scene!=nullptr);
                ECS::Actor* parent=argument?&state.Get<ECS::Components::CTransform>(argument)->owner:nullptr;
                Require(!parent || parent->GetScene()==scene);
                std::vector<ECS::Actor*> sources, copies;
                std::function<void(ECS::Actor&)> collect=[&](ECS::Actor& value){sources.push_back(&value);for(auto* child:value.GetChildren())collect(*child);};collect(actor);
                std::unordered_map<uint64_t,uint64_t> mapping;
                std::unordered_map<ECS::Actor*,ECS::Actor*> actors;
                try {
                    for(auto* source:sources){
                        tinyxml2::XMLDocument doc;auto* root=doc.NewElement("actors");doc.InsertEndChild(root);source->OnSerialize(doc,root);
                        auto* record=root->FirstChildElement("actor");
                        auto* behaviourRecord=record->FirstChildElement("behaviours")->FirstChildElement("behaviour");
                        for(const auto& key:source->GetBehavioursOrder()){
                            auto* behaviour=source->GetBehaviour(key);if(!behaviour||!behaviourRecord)continue;
                            behaviour->PrepareManagedFields();behaviour->SerializeLiveManagedFields(doc,behaviourRecord->FirstChildElement("data"));
                            behaviourRecord=behaviourRecord->NextSiblingElement("behaviour");
                        }
                        auto& copy=scene->CreateActor(source->GetName());copies.push_back(&copy);
                        auto id=copy.GetID();auto guid=copy.GetGUID();copy.OnDeserialize(doc,record);copy.SetID(id);copy.SetGUID(guid);
                        mapping[source->GetGUID()]=guid;actors[source]=&copy;
                    }
                    for(size_t i=0;i<copies.size();++i){
                        auto* source=sources[i];auto* copy=copies[i];
                        if(i==0){if(parent)copy->SetParent(*parent);else copy->DetachFromParent();}
                        else copy->SetParent(*actors.at(source->GetParent()));
                        for(auto& [key,behaviour]:copy->GetBehaviours())behaviour.RemapManagedActorReferences(mapping);
                    }
                    for(auto* copy:copies)for(auto& [key,behaviour]:copy->GetBehaviours())scripts.PrepareCSharp(behaviour);
                    *output=publish(copies.front()->transform);
                } catch(...) {for(auto* copy:copies)copy->MarkAsDestroy();throw;}
                break;
            }
            case 27: {
                Require(actor.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
                const auto split = value.find('\n'); Require(split != std::string::npos);
                const auto identity = ScriptIdentity::CSharp(value.substr(0, split), value.substr(split + 1));
                auto& component = actor.AddBehaviour(identity);
                Require(scripts.PrepareCSharp(component), OV_SCRIPT_INVALID_HANDLE);
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
            case 14: if (auto* parent = actor.GetParent()) *output = publish(parent->transform); break;
            case 15: *output = actor.GetChildren().size(); break;
            case 16: Require(argument < actor.GetChildren().size()); *output = publish(actor.GetChildren()[argument]->transform); break;
            case 17: {
                Require(actor.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
                if (argument) {
                    auto& parent = state.Get<ECS::Components::CTransform>(argument)->owner;
                    Require(parent.GetScene() == actor.GetScene() && parent.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
                    for (auto* cursor = &parent; cursor; cursor = cursor->GetParent()) Require(cursor != &actor);
                    actor.SetParent(parent);
                } else actor.DetachFromParent();
                break;
            }
            case 18: *output = actor.IsActive(); break;
            case 19: *output = actor.GetScene() == state.Get<ECS::Components::CTransform>(argument)->owner.GetScene(); break;
            case 20: {
                Require(actor.GetScene() != nullptr, OV_SCRIPT_INVALID_HANDLE);
                UI::UpdateRectContexts(*actor.GetScene(), {std::bit_cast<float>(uint32_t(argument)), std::bit_cast<float>(uint32_t(argument >> 32))});
                break;
            }
            case 21: *output = actor.CanRunScripts(); break;
            case 22: case 23: {
                auto* scene = actor.GetScene(); Require(scene != nullptr, OV_SCRIPT_INVALID_HANDLE);
                auto* parent = actor.GetParent();
                std::vector<ECS::Actor*> siblings;
                if (parent) siblings = parent->GetChildren();
                else for (auto* candidate : scene->GetActors()) if (!candidate->GetParent()) siblings.push_back(candidate);
                const auto found = std::find(siblings.begin(), siblings.end(), &actor);
                Require(found != siblings.end(), OV_SCRIPT_INVALID_HANDLE);
                const size_t previous = static_cast<size_t>(found - siblings.begin());
                if (operation == 22) { *output = previous; break; }
                Require(actor.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
                const size_t desired = static_cast<size_t>(std::min<uint64_t>(argument, siblings.size() - 1));
                if (desired == previous) break;
                if (desired < previous) std::rotate(siblings.begin() + desired, siblings.begin() + previous, siblings.begin() + previous + 1);
                else std::rotate(siblings.begin() + previous, siblings.begin() + previous + 1, siblings.begin() + desired + 1);
                if (parent) parent->GetChildren() = siblings;
                // Scene XML enumerates the flat actor list. Keep its sibling order in sync
                // without moving unrelated descendants or changing parent/transform state.
                size_t next = 0;
                for (auto*& candidate : scene->GetActors()) if (candidate->GetParent() == parent) candidate = siblings[next++];
                break;
            }
            case 30: case 31: {
                if (operation == 31) Require(actor.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
                if (argument == 1) {
                    auto* rect = operation == 31 ? &actor.AddComponent<ECS::Components::CRectTransform>() : actor.GetComponent<ECS::Components::CRectTransform>();
                    if (rect) *output = publish(*rect);
                } else if (argument == 2) {
                    auto* canvas = operation == 31 ? &actor.AddComponent<ECS::Components::CCanvas>() : actor.GetComponent<ECS::Components::CCanvas>();
                    if (canvas) *output = publish(*canvas);
                } else if (argument == 3) {
                    auto* renderer = operation == 31 ? &actor.AddComponent<ECS::Components::CCanvasRenderer>() : actor.GetComponent<ECS::Components::CCanvasRenderer>();
                    if (renderer) *output = publish(*renderer);
                } else if (argument == 4) {
                    auto* group = operation == 31 ? &actor.AddComponent<ECS::Components::CCanvasGroup>() : actor.GetComponent<ECS::Components::CCanvasGroup>();
                    if (group) *output = publish(*group);
                } else if (argument == 5) {
                    auto* mask = operation == 31 ? &actor.AddComponent<ECS::Components::CRectMask2D>() : actor.GetComponent<ECS::Components::CRectMask2D>();
                    if (mask) *output = publish(*mask);
                } else Require(false);
                break;
            }
            default: Require(false);
            }
        });
    }
}
