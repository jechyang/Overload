#include "OvCore/Reflection.generated.h"
#include <OvCore/ECS/ComponentFactory.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/SceneSystem/Scene.h>
#include <OvCore/SceneSystem/PrefabOperations.h>
#include <OvAudio/Core/AudioEngine.h>
#include <OvPhysics/Core/PhysicsEngine.h>
#include <OvRendering/Context/Driver.h>
#include <OvRendering/Threading/RenderThread.h>
#include <OvWindowing/Context/Device.h>
#include <OvWindowing/Window.h>
#include <GLFW/glfw3.h>
#include <baregl/ResourceExecutor.h>
#include <tinyxml2.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <thread>

using namespace OvCore::ECS;
using namespace OvCore::ECS::Components;
using namespace OvCore::SceneSystem;
namespace R = OvReflection;
void Require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
bool Equal(const OvMaths::FVector3& a, const OvMaths::FVector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
std::string Xml(const tinyxml2::XMLNode& node)
{
    tinyxml2::XMLPrinter printer(nullptr, true); node.Accept(&printer); return printer.CStr();
}
std::string Save(Actor& actor)
{
    tinyxml2::XMLDocument doc; auto* root = doc.NewElement("actors"); doc.InsertEndChild(root);
    actor.OnSerialize(doc, root); return Xml(*root->FirstChildElement("actor"));
}
void Load(Actor& actor, const std::string& xml)
{
    tinyxml2::XMLDocument doc; Require(doc.Parse(xml.c_str()) == tinyxml2::XML_SUCCESS, "parse actor");
    actor.OnDeserialize(doc, doc.RootElement());
}
std::vector<std::string> trace;
struct Probe : CCamera
{
    explicit Probe(Actor& actor) : CCamera(actor) { trace.push_back("construct"); }
    ~Probe() { trace.push_back("destruct"); }
    void OnAwake() override { trace.push_back("awake"); }
    void OnEnable() override { trace.push_back("enable"); }
    void OnStart() override { trace.push_back("start"); }
    void OnDisable() override { trace.push_back("disable"); }
    void OnDestroy() override { trace.push_back("destroy"); }
};
std::vector<std::string> Lifecycle(bool factory, bool playing, bool sleeping, bool active, bool remove)
{
    trace.clear();
    {
        Actor actor(9, "probe", "", playing);
        actor.SetActive(active); actor.SetSleeping(sleeping);
        actor.ComponentAddedEvent += [](AComponent& c) { if (dynamic_cast<Probe*>(&c)) trace.push_back("added"); };
        actor.ComponentRemovedEvent += [](AComponent& c) { if (dynamic_cast<Probe*>(&c)) trace.push_back("removed"); };
        auto descriptor = MakeComponentDesc<Probe, false>(R::TypeOf<CCamera>());
        auto* component = factory ? descriptor.DeserializeComponent(actor) : &actor.AddComponent<Probe>();
        Require(component && actor.GetComponent<Probe>() == component, "factory ownership");
        const auto count = trace.size();
        Require((factory ? descriptor.DeserializeComponent(actor) : &actor.AddComponent<Probe>()) == component && trace.size() == count,
                "duplicate factory changed events");
        if (remove) Require(actor.RemoveComponent(*component), "remove probe");
        else if (!sleeping && active) { actor.OnAwake(); actor.OnEnable(); actor.OnStart(); }
    }
    return trace;
}
void Verify(const std::filesystem::path& output, OvRendering::Threading::RenderThread* render)
{
    std::puts("CHECK registry"); std::fflush(stdout);
    const auto& registry = GetComponentRegistry();
    Require(registry.Reflection().IsFrozen() && registry.Components().size() == 19, "native registry/freeze");
    bool playing = false;
    int added = 0, removed = 0, menuCount = 0;
    Actor actor(1, "native", "tag", playing);
    std::ofstream menu(output / "menu.txt");
    actor.ComponentAddedEvent += [&](AComponent&) { ++added; };
    actor.ComponentRemovedEvent += [&](AComponent&) { ++removed; };
    for (const auto& info : registry.Components())
    {
        Require(registry.Find(info.type->Info().id) == &info, "canonical identity");
        for (auto alias : info.type->Info().aliases) Require(registry.Find(alias) == &info, "legacy alias");
        std::printf("CHECK native %.*s\n", static_cast<int>(info.GetName().size()), info.GetName().data()); std::fflush(stdout);
        if (!info.IsAddable(actor)) continue;
        menu << info.GetName() << '\n'; ++menuCount;
        auto* instance = info.AddComponent(actor);
        Require(instance && &instance->owner == &actor && !info.IsAddable(actor), "native add/uniqueness");
        Require(registry.Find(instance->GetTypeName()) == &info, "compatibility output type");
        Require(!info.AddComponent(actor), "duplicate menu add");
        Require(info.DeserializeComponent(actor) == instance, "archive existing instance");
        Require(actor.RemoveComponent(*instance) && info.IsAddable(actor), "native removal/menu refresh");
    }
    Require(menuCount == 16 && added == 16 && removed == 16, "menu/events count");
    std::puts("CHECK transform"); std::fflush(stdout);
    auto* transform = registry.Find("class OvCore::ECS::Components::CTransform");
    Require(transform && !transform->IsAddable(actor) && !transform->AddComponent(actor), "Transform not addable");
    Require(transform->DeserializeComponent(actor) == &actor.transform && actor.GetComponents().size() == 1, "Transform lookup");
    Require(!actor.RemoveComponent(actor.transform) && !actor.RemoveComponent<AComponent>(), "Transform erased/base removal blocked");
    for (auto name : {"class OvCore::ECS::Components::CLight", "class OvCore::ECS::Components::CPhysicalObject"})
    {
        auto* abstract = registry.Find(name);
        Require(abstract && !abstract->IsAddable(actor) && !abstract->DeserializeComponent(actor), "abstract factory exposed");
    }
    std::puts("CHECK physical exclusion"); std::fflush(stdout);
    auto* box = registry.Find("class OvCore::ECS::Components::CPhysicalBox");
    auto* sphere = registry.Find("class OvCore::ECS::Components::CPhysicalSphere");
    auto* capsule = registry.Find("class OvCore::ECS::Components::CPhysicalCapsule");
    Require(box->AddComponent(actor) && !sphere->IsAddable(actor) && !capsule->AddComponent(actor), "physical menu exclusion");
    Require(sphere->DeserializeComponent(actor), "legacy mixed physical archive restriction changed");
    Require(R::ObjectRef::From(*actor.GetComponent<CPhysicalBox>()).As<CPhysicalObject>(), "physical inheritance");
    Require(actor.RemoveComponent<CPhysicalObject>() && !box->IsAddable(actor), "base removal preserves other physical");
    Require(actor.RemoveComponent<CPhysicalObject>() && box->IsAddable(actor), "base removal restores menu");
    std::puts("CHECK worker"); std::fflush(stdout);
    const auto before = actor.GetComponents().size();
    auto wrongThread = [&] { return !box->IsAddable(actor) && !box->AddComponent(actor) && !box->DeserializeComponent(actor)
        && registry.Find("class OvCore::ECS::Components::CPhysicalBox") == box; };
    bool rejected = false;
    std::thread worker([&] { rejected = wrongThread(); }); worker.join();
    Require(rejected && actor.GetComponents().size() == before, "worker mutated actor");
    if (render) Require(render->Invoke([&](auto&) { return wrongThread(); }), "RenderThread component factory");

    std::puts("CHECK all native acquire"); std::fflush(stdout);
    // All 17 concrete native types, including inherited light/physics families, use their real serializers.
    for (const auto& info : registry.Components()) info.DeserializeComponent(actor);
    Require(actor.GetComponents().size() == 17, "all native archive factories");
    actor.transform.SetLocalPosition({2, 3, 4});
    actor.GetComponent<CPhysicalBox>()->SetSize({2, 4, 6});
    actor.GetComponent<CCamera>()->SetFov(63);
    std::puts("CHECK save all native"); std::fflush(stdout);
    const auto legacy = Save(actor);
    std::ofstream(output / "all-native-legacy.xml") << legacy;
    Actor restored(2, "restored", "", playing); Load(restored, legacy);
    Require(restored.GetComponents().size() == 17, "legacy native type count");
    Require(Equal(restored.GetComponent<CPhysicalBox>()->GetSize(), OvMaths::FVector3(2, 4, 6)), "Box payload changed");
    Require(restored.GetComponent<CCamera>()->GetFov() == 63 && Equal(restored.transform.GetLocalPosition(), OvMaths::FVector3(2, 3, 4)), "camera/transform payload");
    Require(Save(restored) == legacy, "all native archive roundtrip");
    std::string canonical = legacy;
    for (size_t at = 0; (at = canonical.find("class OvCore::", at)) != std::string::npos;) canonical.erase(at, 6);
    Actor byID(3, "id", "", playing); Load(byID, canonical);
    Require(Save(byID) == legacy, "canonical ID should still write legacy names");

    // Unknown/malformed entries survive document destruction, repeated load and repeated saves.
    tinyxml2::XMLDocument unknownDoc; unknownDoc.Parse(legacy.c_str());
    auto* components = unknownDoc.RootElement()->FirstChildElement("components");
    const auto duplicateTransform = Xml(*components->FirstChildElement("component"));
    tinyxml2::XMLDocument duplicate; duplicate.Parse(duplicateTransform.c_str());
    components->InsertEndChild(duplicate.RootElement()->DeepClone(&unknownDoc));
    const std::vector<std::string> unknowns = {
        "<component version='9'><type>future.Widget</type><data><nested value='a&amp;b'><![CDATA[raw <payload>]]></nested><!--keep--></data><extension x='7'/></component>",
        "<component><data><without_type>42</without_type></data></component>",
        "<component><type>class OvCore::ECS::Components::CCamera</type><extension>missing data</extension></component>",
        "<component><type>class OvCore::ECS::Components::CLight</type><data><opaque>abstract</opaque></data></component>"};
    std::vector<std::string> expected;
    for (const auto& xml : unknowns)
    {
        tinyxml2::XMLDocument node; node.Parse(xml.c_str()); expected.push_back(Xml(*node.RootElement()));
        components->InsertEndChild(node.RootElement()->DeepClone(&unknownDoc));
    }
    const auto mixed = Xml(*unknownDoc.RootElement());
    Actor opaque(4, "opaque", "", playing); Load(opaque, mixed); Load(opaque, mixed); unknownDoc.Clear();
    Require(opaque.GetComponents().size() == 17, "duplicate Transform or unknown created a component");
    auto saved = Save(opaque); Actor again(5, "again", "", playing); Load(again, saved);
    Require(Save(again) == saved, "opaque roundtrip differs");
    for (const auto& xml : expected) Require(saved.find(xml) != std::string::npos, "unknown XML lost");
    std::ofstream(output / "unknown-preserved.xml") << saved;

    const auto opaquePath = output / "unknown.ovprefab";
    Require(PrefabOperations::SaveToFile(opaque, opaquePath), "unknown prefab save");
    Scene opaqueScene;
    auto* opaqueInstance = opaqueScene.InstantiatePrefab(opaquePath.string());
    Require(opaqueInstance, "unknown prefab load");
    const auto prefabUnknown = Save(*opaqueInstance);
    for (const auto& xml : expected) Require(prefabUnknown.find(xml) != std::string::npos, "unknown prefab payload lost");

    for (bool play : {false, true}) for (bool sleep : {false, true}) for (bool active : {false, true}) for (bool remove : {false, true})
    {
        auto direct = Lifecycle(false, play, sleep, active, remove);
        auto generated = Lifecycle(true, play, sleep, active, remove);
        Require(direct == generated, "lifecycle/event order differs from Actor::AddComponent");
        if (play && !sleep && active && remove)
            Require(generated == std::vector<std::string>{"construct", "added", "awake", "enable", "start", "removed", "destruct"}, "live-add event ordering");
    }

    std::puts("CHECK legacy scene"); std::fflush(stdout);
    tinyxml2::XMLDocument oldScene;
    Require(oldScene.LoadFile("Tests/Reflection/Fixtures/R4/legacy.ovscene") == tinyxml2::XML_SUCCESS, "old scene fixture");
    Scene scene; scene.OnDeserialize(oldScene, oldScene.RootElement()->FirstChildElement("scene"));
    Require(scene.GetActors().size() == 2 && scene.GetFastAccessComponents().cameras.size() == 1 && scene.GetFastAccessComponents().lights.size() == 1, "old scene/derived component events");
    Require(scene.FindActorByName("Lab Camera")->GetComponent<CCamera>()->GetFov() == 45, "old camera field");
    scene.Play(); scene.Update(0); scene.FixedUpdate(0); scene.LateUpdate(0);
    Scene prefabs;
    auto* first = prefabs.InstantiatePrefab("Tests/Reflection/Fixtures/R4/legacy.ovprefab");
    auto* second = prefabs.InstantiatePrefab("Tests/Reflection/Fixtures/R4/legacy.ovprefab");
    Require(first && second && first != second && first->GetGUID() != second->GetGUID() && first->GetChildren().size() == 1, "old prefab remapping/hierarchy");
    Require(first->GetChildren()[0]->GetComponent<CDirectionalLight>() && first->GetChildren()[0]->GetParent() == first, "prefab inherited component");
    Require(Equal(first->transform.GetWorldPosition(), OvMaths::FVector3(0, 22, 40)), "prefab transform data");
    const auto prefabPath = output / "roundtrip.ovprefab";
    Require(PrefabOperations::SaveToFile(*first, prefabPath), "prefab save");
    auto* third = prefabs.InstantiatePrefab(prefabPath.string());
    Require(third && third->GetChildren().size() == 1 && third->GetComponent<CCamera>(), "prefab roundtrip");
    prefabs.Play();
    auto* live = prefabs.InstantiatePrefab("Tests/Reflection/Fixtures/R4/legacy.ovprefab");
    Require(live && live->GetChildren().size() == 1, "playing prefab load");
    std::puts("PASS: 19 types, 16 menu entries, native add/remove, Main enforcement, all native XML, unknown XML, lifecycle matrix, old scene/Prefab");
}
int main(int argc, char** argv) try
{
    Require(argc >= 3, "usage: component_validation output mode");
    std::filesystem::path output = argv[1]; std::filesystem::create_directories(output);
    const bool threaded = std::string_view(argv[2]) == "render-thread";
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion = 4; ds.contextMinorVersion = 5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible = false; ws.focused = false; ws.width = 64; ws.height = 64; ws.samples = 1;
    OvWindowing::Window window(device, ws);
    OvPhysics::Core::PhysicsEngine physics(OvPhysics::Settings::PhysicsSettings{});
    OvAudio::Core::AudioEngine audio; OvCore::Global::ServiceLocator::Provide(audio);
    if (threaded)
    {
        glfwMakeContextCurrent(nullptr);
        OvRendering::Threading::RenderThread render([&] { window.MakeCurrentContext(); return std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{}); }, [] { glfwMakeContextCurrent(nullptr); });
        {
            baregl::ResourceExecutor resources([&](std::function<void()> work) { render.Invoke([&](auto&) { work(); }); });
            Verify(output, &render);
        }
        render.Shutdown();
    }
    else
    {
        window.MakeCurrentContext(); OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{});
        Verify(output, nullptr);
    }
    return 0;
}
catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
