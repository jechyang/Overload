#include <OvEditor/Panels/Inspector.h>
#include <OvCore/ECS/Components/CPhysicalBox.h>
#include <OvCore/SceneSystem/SceneManager.h>
#include <OvPhysics/Core/PhysicsEngine.h>
#include <cstdio>
#include <stdexcept>
using Inspector = OvEditor::Panels::Inspector;
using Actor = OvCore::ECS::Actor;
using Box = OvCore::ECS::Components::CPhysicalBox;
using Vector = OvMaths::FVector3;
using Array = std::array<float, 3>;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void Walk(OvUI::Internal::WidgetContainer& root, F&& f)
{
    for (auto [widget, mode] : root.GetWidgets())
    {
        f(*widget);
        if (auto* container = dynamic_cast<OvUI::Internal::WidgetContainer*>(widget)) Walk(*container, f);
    }
}
int main() try
{
    OvPhysics::Core::PhysicsEngine physics(OvPhysics::Settings::PhysicsSettings{});
    OvCore::SceneSystem::SceneManager scenes;
    auto& a = scenes.GetCurrentScene()->CreateActor("A");
    auto& b = scenes.GetCurrentScene()->CreateActor("B");
    std::vector<std::function<void()>> queue; int selected = 0, unselected = 0;
    Inspector::Callbacks callbacks{
        [&](std::function<void()> action) { queue.push_back(std::move(action)); },
        [&](Actor&) { ++selected; }, [&](Actor&) { ++unselected; }};
    auto flush = [&] { auto actions = std::move(queue); queue.clear(); for (auto& action : actions) action(); };
    auto panel = std::make_unique<Inspector>("R5 Inspector", true, OvUI::Settings::PanelWindowSettings{}, callbacks);
    panel->FocusActor(a);
    auto& box = a.AddComponent<Box>();
    Require(queue.size() == 1, "component add did not schedule refresh"); flush();
    std::vector<OvUI::Plugins::DataDispatcher<Array>*> vectors;
    OvUI::Widgets::Layout::GroupCollapsable* boxHeader = nullptr;
    Walk(*panel, [&](auto& widget) {
        if (auto* d = widget.template GetPlugin<OvUI::Plugins::DataDispatcher<Array>>()) vectors.push_back(d);
        if (auto* h = dynamic_cast<OvUI::Widgets::Layout::GroupCollapsable*>(&widget); h && h->name == "Physical Box") boxHeader = h;
    });
    Require(vectors.size() >= 4 && boxHeader, "real Inspector did not populate migrated drawers");
    auto sizeCallback = *vectors.back(); sizeCallback.NotifyChange(); sizeCallback.Provide({2,3,4});
    Require(box.GetSize().x == 2 && box.GetSize().y == 3 && box.GetSize().z == 4, "panel property write");
    OvUI::Plugins::DataDispatcher<std::string>* name = nullptr;
    Walk(*panel, [&](auto& widget) { if (!name) name = widget.template GetPlugin<OvUI::Plugins::DataDispatcher<std::string>>(); });
    Require(name, "actor name control"); auto oldName = *name;
    auto closeAgain = boxHeader->CloseEvent;
    boxHeader->CloseEvent.Invoke();
    Require(!a.GetComponent<Box>() && queue.size() == 1, "panel remove");
    bool allStopped = true; Walk(*panel, [&](auto& widget) { if (&widget != panel->GetWidgets().front().first) allStopped &= !widget.enabled; });
    Require(allStopped, "controls remain drawable before deferred refresh");
    sizeCallback.NotifyChange(); sizeCallback.Provide({99,99,99}); closeAgain.Invoke();
    panel->FocusActor(b);
    OvUI::Plugins::DataDispatcher<Array>* position = nullptr;
    Walk(*panel, [&](auto& widget) { if (!position) position = widget.template GetPlugin<OvUI::Plugins::DataDispatcher<Array>>(); });
    Require(position, "new target position control"); auto currentPosition = *position;
    flush(); currentPosition.NotifyChange(); currentPosition.Provide({11,12,13});
    Require(&panel->GetTargetActor().value() == &b && b.transform.GetLocalPosition().x == 11, "old selection refresh invalidated current target view");
    oldName.NotifyChange(); oldName.Provide("stale"); Require(a.GetName() == "A" && b.GetName() == "B", "old actor info callback changed target");
    b.AddComponent<Box>(); Require(queue.size() == 1, "second target refresh");
    panel.reset(); oldName.NotifyChange(); oldName.Provide("dead"); Require(oldName.Gather().empty(), "dead panel name gather"); flush(); // Pending callback must not dereference destroyed Inspector.
    Require(selected == 2 && unselected == 2, "selection notifications");
    panel = std::make_unique<Inspector>("R5 Inspector", true, OvUI::Settings::PanelWindowSettings{}, callbacks);
    panel->FocusActor(b); b.RemoveComponent<Box>(); Require(!queue.empty(), "pending scene refresh");
    scenes.LoadEmptyScene(); flush(); Require(!panel->GetTargetActor(), "scene replacement retained Inspector target");
    std::puts("PASS: real Inspector population/edit/remove, deferred refresh, target switch, panel destruction and scene replacement"); return 0;
}
catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
