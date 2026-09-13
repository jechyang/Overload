#include "OvCore/Reflection.generated.h"
#include "OvCore/Script.generated.h"
#include <OvCore/Scripting/ScriptBridge.h>
#include <OvCore/SceneSystem/SceneManager.h>
#include <OvPhysics/Core/PhysicsEngine.h>
#include <OvRendering/Context/Driver.h>
#include <OvRendering/Threading/RenderThread.h>
#include <OvWindowing/Context/Device.h>
#include <OvWindowing/Window.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <memory>
#include <cmath>
using Box = OvCore::ECS::Components::CPhysicalBox;
using Transform = OvCore::ECS::Components::CTransform;
using RenderThread = OvRendering::Threading::RenderThread;
using namespace OvReflection;
namespace {
struct Engine {
    MainThread main;
    Script::Context context{main};
    OvWindowing::Context::Device device;
    OvWindowing::Window window;
    std::unique_ptr<OvRendering::Context::Driver> driver;
    std::unique_ptr<RenderThread> render;
    OvPhysics::Core::PhysicsEngine physics{OvPhysics::Settings::PhysicsSettings{}};
    OvCore::SceneSystem::SceneManager scenes;
    OvCore::ECS::Actor* parent = nullptr;
    OvCore::ECS::Actor* child = nullptr;
    Box* box = nullptr;
    uint64_t boxHandle = 0, transformHandle = 0;
    static OvWindowing::Settings::DeviceSettings DeviceSettings() { OvWindowing::Settings::DeviceSettings s; s.contextMajorVersion=4; s.contextMinorVersion=5; return s; }
    static OvWindowing::Settings::WindowSettings WindowSettings() { OvWindowing::Settings::WindowSettings s; s.visible=false;s.focused=false;s.width=64;s.height=64;s.samples=1;return s; }
    explicit Engine(bool threaded) : device(DeviceSettings()), window(device,WindowSettings()) {
        if (threaded) {
            glfwMakeContextCurrent(nullptr);
            render=std::make_unique<RenderThread>([this]{window.MakeCurrentContext();return std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{});},[]{glfwMakeContextCurrent(nullptr);});
        } else {window.MakeCurrentContext();driver=std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{});}
        Populate();
    }
    ~Engine() { scenes.LoadEmptyScene(); if(render)render->Shutdown(); }
    void Populate() {
        auto* scene=scenes.GetCurrentScene();
        parent=&scene->CreateActor("R6 parent");child=&scene->CreateActor("R6 child");child->SetParent(*parent);
        box=&child->AddComponent<Box>();box->SetMass(0);
        boxHandle=OvCore::Scripting::PublishComponent(context,*box);
        transformHandle=OvCore::Scripting::PublishComponent(context,child->transform);
        scene->Play();
    }
};
std::unique_ptr<Engine> engine;
}
extern "C" OV_SCRIPT_EXPORT uint64_t OV_SCRIPT_CALL R6_EngineStart(uint32_t threaded) {
    try {engine=std::make_unique<Engine>(threaded!=0);return engine->context.Id();}
    catch(const std::exception& e){std::fprintf(stderr,"Engine start failed: %s\n",e.what());return 0;}
}
extern "C" OV_SCRIPT_EXPORT uint64_t OV_SCRIPT_CALL R6_EngineHandle(uint32_t which) {return which==0?engine->boxHandle:engine->transformHandle;}
extern "C" OV_SCRIPT_EXPORT int32_t OV_SCRIPT_CALL R6_EngineRay() {return engine->physics.Raycast({1.5f,0,5},{0,0,-1},10).has_value()?1:0;}
extern "C" OV_SCRIPT_EXPORT int32_t OV_SCRIPT_CALL R6_EngineMatrix() {
    auto& transform=engine->child->transform;
    auto position=transform.GetLocalPosition();auto rotation=transform.GetLocalRotation();auto scale=transform.GetLocalScale();
    engine->parent->transform.SetLocalPosition({10,20,30});
    OvMaths::FTransform reference(position,rotation,scale);reference.SetParent(engine->parent->transform.GetFTransform());
    for(size_t i=0;i<16;++i)if(std::abs(reference.GetWorldMatrix().data[i]-transform.GetWorldMatrix().data[i])>0.0005f)return 0;
    return 1;
}
extern "C" OV_SCRIPT_EXPORT int32_t OV_SCRIPT_CALL R6_EngineRenderReject() {
    if(!engine->render)return 1;
    // Explicit entry reference retains the production generated table in static linking.
    uint8_t signature[32]={R6_CORE_SIGNATURE_BYTES};OvScriptOvCoreTable table{};
    if(OvScript_GetOvCoreApi(1,signature,sizeof(table),&table)!=0)return 0;
    return engine->render->Invoke([&](auto&){OvScriptVector3 value{9,9,9};return table.OvCore_ECS_Components_CPhysicalBox_set_size(engine->context.Id(),engine->boxHandle,&value)==OV_SCRIPT_WRONG_THREAD;})?1:0;
}
extern "C" OV_SCRIPT_EXPORT int32_t OV_SCRIPT_CALL R6_EngineRemoveBox() {
    try {return engine->child->RemoveComponent(*engine->box)?1:0;} catch(...){return 0;}
}
extern "C" OV_SCRIPT_EXPORT int32_t OV_SCRIPT_CALL R6_EngineReplaceScene() {
    try {engine->scenes.LoadEmptyScene();engine->Populate();return 1;} catch(...){return 0;}
}
extern "C" OV_SCRIPT_EXPORT void OV_SCRIPT_CALL R6_EngineStop() {engine.reset();}
