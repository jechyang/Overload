#include "OvCore/Reflection.generated.h"
#include <OvCore/ECS/Actor.h>
#include <OvPhysics/Core/PhysicsEngine.h>
#include <OvRendering/Context/Driver.h>
#include <OvRendering/Threading/RenderThread.h>
#include <OvWindowing/Context/Device.h>
#include <OvWindowing/Window.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <stdexcept>

using namespace OvReflection;
using Box = OvCore::ECS::Components::CPhysicalBox;
using Vector = OvMaths::FVector3;
using RenderThread = OvRendering::Threading::RenderThread;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool Equal(const Vector& a, const Vector& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

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
    std::puts(render ? "PASS: real GL RenderThread rejects component access; Main Size updates real Bullet ray hits"
                     : "PASS: default Main GL owner; Size getter/setter updates real Bullet ray hits");
}

int main(int argc, char** argv) try
{
    const bool threaded = argc > 1 && std::string_view(argv[1]) == "render-thread";
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
