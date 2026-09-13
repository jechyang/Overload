#include "OvCore/Reflection.generated.h"
#include <OvCore/ECS/Actor.h>
#include <OvPhysics/Core/PhysicsEngine.h>
#include <OvRendering/Context/Driver.h>
#include <OvRendering/Threading/RenderThread.h>
#include <OvWindowing/Context/Device.h>
#include <OvWindowing/Window.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <OvCore/Helpers/ReflectionDrawer.h>
#include <OvCore/SceneSystem/SceneManager.h>
#include <OvCore/SceneSystem/PrefabOperations.h>
#include <cmath>
#include <filesystem>
#include <stdexcept>

using namespace OvReflection;
using Box = OvCore::ECS::Components::CPhysicalBox;
using Vector = OvMaths::FVector3;
using RenderThread = OvRendering::Threading::RenderThread;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool Equal(const Vector& a, const Vector& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

std::string argvOutput;
void Verify(RenderThread* render)
{
    Registry registry;
    Require(RegisterOvMathsTypes(registry) == Error::None && RegisterOvCoreTypes(registry) == Error::None, "module registration");
    Require(registry.Types().size() == 20 && registry.Freeze() == Error::None, "module ownership/freeze");
    auto* descriptor = registry.Find("class OvCore::ECS::Components::CPhysicalBox");
    Require(descriptor == &TypeOf<Box>(), "legacy component identity");
    auto* size = descriptor->FindProperty("size");
    Require(size && size->Info().thread == Thread::Main && size->Info().type.key == Key<Vector>(), "Size descriptor");

    OvPhysics::Core::PhysicsEngine physics(OvPhysics::Settings::PhysicsSettings{});
    bool playing = true;
    OvCore::ECS::Actor actor(1, "Reflection R2", "", playing);
    auto& box = actor.AddComponent<Box>();
    box.SetMass(0);
    actor.SetSleeping(false);
    actor.OnAwake();
    actor.OnEnable();
    actor.OnStart();
    auto ray = [&] { return physics.Raycast({1.5f, 0, 5}, {0, 0, -1}, 10).has_value(); };
    Require(!ray(), "initial half-unit box unexpectedly hit");
    Vector input{2, 3, 4}, output;
    Require(size->Write(registry.Main(), ObjectRef::From(box), ValueRef::From(input)) == Error::None, "Size write");
    Require(Equal(box.GetSize(), input), "existing GetSize differs");
    Require(ray(), "Size setter did not rebuild real Bullet shape/body");
    Require(size->Read(registry.Main(), ObjectRef::From(std::as_const(box)), ValueRef::From(output)) == Error::None && Equal(output, input), "Size read");
    Require(ObjectRef::From(box).As<OvCore::ECS::Components::CPhysicalObject>() == static_cast<OvCore::ECS::Components::CPhysicalObject*>(&box), "real base conversion");
    Require(ObjectRef::From(box).As<OvCore::ECS::Components::AComponent>() == static_cast<OvCore::ECS::Components::AComponent*>(&box), "unannotated grandparent conversion");
    float wrong = 1;
    Require(size->Write(registry.Main(), ObjectRef::From(box), ValueRef::From(wrong)) == Error::WrongValueType, "wrong Size type");
    Require(size->Write(registry.Main(), ObjectRef::From(input), ValueRef::From(input)) == Error::WrongObjectType, "wrong component");
    Require(size->Write(registry.Main(), ObjectRef::From(std::as_const(box)), ValueRef::From(input)) == Error::ReadOnly, "const component write");
    if (render)
    {
        const bool rejected = render->Invoke([&](auto&)
        {
            Vector attempted{20, 20, 20}, unchanged{77, 77, 77};
            return render->IsOwnerThread()
                && registry.Find("class OvCore::ECS::Components::CPhysicalBox") == descriptor
                && size->Read(registry.Main(), ObjectRef::From(box), ValueRef::From(unchanged)) == Error::WrongThread
                && Equal(unchanged, {77, 77, 77})
                && size->Write(registry.Main(), ObjectRef::From(box), ValueRef::From(attempted)) == Error::WrongThread
                && Access<Box>::Write_GetSize(registry.Main(), box, attempted) == Error::WrongThread;
        });
        Require(rejected && Equal(box.GetSize(), input), "RenderThread accessed component");
    }
    Vector small{0.5f, 0.5f, 0.5f};
    Require(Access<Box>::Write_GetSize(registry.Main(), box, small) == Error::None && !ray(), "typed setter failed to restore Bullet shape");
    float x = 8;
    Require(TypeOf<Vector>().FindProperty("x")->Write(registry.Main(), ObjectRef::From(output), ValueRef::From(x)) == Error::None && output.x == 8, "real vector field");

    using namespace OvCore::Helpers::ReflectionProperties;
    using Transform = OvCore::ECS::Components::CTransform;
    const auto& app = OvCore::ECS::GetComponentRegistry().Reflection();
    OvUI::Internal::WidgetContainer boxUI;
    box.OnInspector(boxUI);
    auto* sizeControl = boxUI.GetWidgets().back().first->GetPlugin<OvUI::Plugins::DataDispatcher<std::array<float, 3>>>();
    Require(sizeControl != nullptr, "real Size drawer");
    auto sizeCallback = *sizeControl;
    sizeCallback.NotifyChange(); sizeCallback.Provide({2, 3, 4}); Require(ray(), "Size UI did not rebuild Bullet shape");
    tinyxml2::XMLDocument boxDoc; auto* boxNode = boxDoc.NewElement("data"); boxDoc.InsertEndChild(boxNode);
    box.OnSerialize(boxDoc, boxNode); box.SetSize({0.5f, 0.5f, 0.5f}); Require(!ray(), "Box restore baseline");
    box.OnDeserialize(boxDoc, boxNode); Require(ray(), "Size XML did not rebuild Bullet shape");
    auto sizeBinding = Bind(box, *size, app.Main(), box.InspectorTicket());
    if (render) Require(render->Invoke([&](auto&) { std::any v; return sizeBinding.read(v).access == Error::WrongThread && sizeBinding.write(Vector{1,1,1}).access == Error::WrongThread && !Deserialize(app.Main(), ObjectRef::From(box), TypeOf<Box>(), *boxNode).Ok(); }), "RenderThread adapter access");
    actor.RemoveComponent(box); std::any dead;
    Require(sizeBinding.read(dead).status == Status::Expired, "detached component binding survived");
    sizeCallback.NotifyChange(); sizeCallback.Provide({100,100,100}); // Must not touch freed component.

    bool editing = false;
    OvCore::ECS::Actor parent(2, "parent", "", editing), child(3, "child", "", editing);
    child.SetParent(parent);
    parent.transform.SetLocalPosition({10, 20, 30}); parent.transform.SetLocalScale({2,3,4});
    auto& transform = child.transform;
    transform.SetLocalPosition({1, 2, 3}); transform.SetLocalRotation(OvMaths::FQuaternion(Vector{10,20,30})); transform.SetLocalScale({2,2,2});
    auto matrixNear = [](const OvMaths::FMatrix4& a, const OvMaths::FMatrix4& b) { for (size_t i = 0; i < 16; ++i) if (std::abs(a.data[i] - b.data[i]) > 0.0005f) return false; return true; };
    tinyxml2::XMLDocument trs; auto* trsNode = trs.NewElement("data"); trs.InsertEndChild(trsNode); transform.OnSerialize(trs, trsNode);
    Require(trsNode->FirstChildElement("rotation")->FirstChildElement("w"), "rotation archive stopped being quaternion");
    auto original = transform.GetWorldMatrix();
    transform.SetLocalPosition({99,99,99}); transform.OnDeserialize(trs, trsNode);
    Require(matrixNear(original, transform.GetWorldMatrix()), "TRS world matrix round trip");
    OvUI::Internal::WidgetContainer transformUI; transform.OnInspector(transformUI);
    Require(transformUI.GetWidgets().size() == 6, "TRS drawer order/count");
    auto* rotationControl = transformUI.GetWidgets()[3].first->GetPlugin<OvUI::Plugins::DataDispatcher<std::array<float,3>>>();
    Require(rotationControl, "Euler rotation drawer");
    auto display = rotationControl->Gather(); Require(std::abs(display[0]-10) < 0.001f && std::abs(display[1]-20) < 0.001f && std::abs(display[2]-30) < 0.001f, "Euler rotation display");
    rotationControl->NotifyChange(); rotationControl->Provide({35, -15, 80});
    auto expected = OvMaths::FQuaternion(Vector{35,-15,80});
    Require(std::abs(OvMaths::FQuaternion::DotProduct(expected, transform.GetLocalRotation())) > 0.99999f, "Euler rotation edit");
    OvMaths::FTransform reference({1,2,3}, expected, {2,2,2}); reference.SetParent(parent.transform.GetFTransform());
    Require(matrixNear(reference.GetWorldMatrix(), transform.GetWorldMatrix()), "rotation edit hierarchy matrix");
    tinyxml2::XMLDocument partial; partial.Parse("<data><position><x>7</x></position><rotation><z>0.5</z></rotation></data>");
    transform.OnDeserialize(partial, partial.RootElement());
    Require(Equal(transform.GetLocalPosition(), {7,0,0}) && Equal(transform.GetLocalScale(), {0,0,0}) && transform.GetLocalRotation().w == 1, "legacy missing/partial TRS defaults");
    transform.OnDeserialize(trs, trsNode); auto stable = transform.GetWorldMatrix();
    partial.Parse("<data><position><x>9</x></position><rotation><w>bad</w></rotation><scale><x>1</x></scale></data>");
    bool rejected = false; try { transform.OnDeserialize(partial, partial.RootElement()); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected && matrixNear(stable, transform.GetWorldMatrix()), "malformed TRS partially mutated transform");
    parent.transform.SetLocalPosition({30,20,10}); reference.GenerateMatricesLocal(transform.GetLocalPosition(), transform.GetLocalRotation(), transform.GetLocalScale());
    Require(matrixNear(reference.GetWorldMatrix(), transform.GetWorldMatrix()), "parent propagation after load");

    OvCore::SceneSystem::SceneManager scenes;
    auto* scene = scenes.GetCurrentScene(); auto& saved = scene->CreateActor("R5 saved");
    saved.transform.SetLocalPosition({3,4,5}); auto& savedBox = saved.AddComponent<Box>(); savedBox.SetSize({6,7,8});
    auto oldBinding = Bind(saved.transform, *TypeOf<Transform>().FindProperty("position"), app.Main(), saved.transform.InspectorTicket());
    tinyxml2::XMLDocument backup; auto* backupRoot = backup.NewElement("root"); backup.InsertEndChild(backupRoot); scene->OnSerialize(backup, backupRoot);
    scene->Play(); saved.transform.SetLocalPosition({100,100,100}); savedBox.SetSize({2,2,2});
    Require(scenes.LoadSceneFromMemory(backup), "Play/Stop backup restore");
    Require(oldBinding.read(dead).status == Status::Expired, "Play/Stop retained old target");
    auto* restored = scenes.GetCurrentScene()->FindActorByName("R5 saved");
    Require(restored && Equal(restored->transform.GetLocalPosition(), {3,4,5}) && Equal(restored->GetComponent<Box>()->GetSize(), {6,7,8}), "Play/Stop restored wrong component data");
    auto prefab = std::filesystem::path(argvOutput) / "r5.ovprefab";
    Require(OvCore::SceneSystem::PrefabOperations::SaveToFile(*restored, prefab), "R5 prefab save");
    auto* instance = scenes.GetCurrentScene()->InstantiatePrefab(prefab.string());
    Require(instance && Equal(instance->GetComponent<Box>()->GetSize(), {6,7,8}) && Equal(instance->transform.GetLocalPosition(), {3,4,5}), "R5 prefab restore");
    auto replacedBinding = Bind(restored->transform, *TypeOf<Transform>().FindProperty("position"), app.Main(), restored->transform.InspectorTicket());
    scenes.LoadEmptyScene(); Require(replacedBinding.write(Vector{1,1,1}).status == Status::Expired, "scene replacement retained target");
    std::puts("PASS: R5 real Size UI/XML Bullet effects, quaternion/Euler TRS, hierarchy, malformed XML, Prefab, Play/Stop restore and scene callbacks");
    std::puts(render ? "PASS: real GL RenderThread rejects component access; Main Size updates real Bullet ray hits"
                     : "PASS: default Main GL owner; Size getter/setter updates real Bullet ray hits");
}

int main(int argc, char** argv) try
{
    Require(argc > 2, "usage: output mode"); argvOutput = argv[1]; std::filesystem::create_directories(argvOutput);
    const bool threaded = std::string_view(argv[2]) == "render-thread";
    OvWindowing::Settings::DeviceSettings ds;
    ds.contextMajorVersion = 4; ds.contextMinorVersion = 5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;
    ws.visible = false; ws.focused = false; ws.width = 64; ws.height = 64; ws.samples = 1;
    OvWindowing::Window window(device, ws);
    if (threaded)
    {
        glfwMakeContextCurrent(nullptr);
        RenderThread render([&]
        {
            window.MakeCurrentContext();
            return std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{});
        }, [] { glfwMakeContextCurrent(nullptr); });
        Verify(&render);
        render.Shutdown();
    }
    else
    {
        window.MakeCurrentContext();
        OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{});
        Verify(nullptr);
    }
    return 0;
}
catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
