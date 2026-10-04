#define wmain HostValidationMain
#include "Tests/Scripting/Integration/native_host_validation.cpp"
#undef wmain
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/ResourceManagement/FontManager.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/UI/Frame.h>
#include <OvCore/Rendering/RenderFrontend.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <OvWindowing/Inputs/InputManager.h>
#include <baregl/ResourceExecutor.h>
#include <baregl/detail/glad/glad.h>
#include <fstream>
#include <vector>
#include <string>

using namespace OvCore::ECS::Components;
using OvCore::Scripting::ScriptIdentity;

static constexpr uint16_t kWidth = 320, kHeight = 200;
static std::vector<uint8_t> g_pixels;

static Behaviour& Add(OvCore::ECS::Actor& actor, const char* name)
{ return actor.AddBehaviour(ScriptIdentity::CSharp("InputFixture", name)); }

static void Place(OvCore::ECS::Actor& actor, float x, float y, float w, float h)
{
    auto* rect = actor.GetComponent<CRectTransform>();
    rect->SetAnchorMin({0, 0}); rect->SetAnchorMax({0, 0});
    rect->SetPivot({0.5f, 0.5f});
    rect->SetAnchoredPosition({x, y});
    rect->SetSizeDelta({w, h});
}

static std::string Str(Behaviour& b, const char* name)
{
    for (const auto& field : b.GetScript()->ManagedFields())
        if (field.name == name) {
            auto value = b.GetScript()->ReadManagedField(field.token);
            return value ? value->text : "";
        }
    throw std::runtime_error(name);
}

static int Int(Behaviour& b, const char* name)
{
    for (const auto& field : b.GetScript()->ManagedFields())
        if (field.name == name) {
            auto value = b.GetScript()->ReadManagedField(field.token);
            return value ? value->scalar.int32_value : -1;
        }
    throw std::runtime_error(name);
}

static void SetInt(Behaviour& b, const char* field, int value)
{
    for (const auto& meta : b.GetScript()->ManagedFields()) {
        if (meta.name != field) continue;
        auto live = b.GetScript()->ReadManagedField(meta.token);
        Require(live.has_value(), field);
        OvCore::Scripting::ManagedFieldValue v = *live;
        std::fprintf(stderr, "edit %s tag=%d\n", field, int(v.tag));
        if (v.tag == OVCS_FIELD_BOOL) v.scalar.boolean = value != 0;
        else if (v.tag == OVCS_FIELD_ENUM_UINT64 || v.tag == OVCS_FIELD_UINT64) v.scalar.uint64_value = (uint64_t)value;
        else v.scalar.int64_value = value;
        Require(b.EditManagedField(field, v), field);
        return;
    }
    throw std::runtime_error(field);
}

static void SetStr(Behaviour& b, const char* field, const std::string& value)
{
    OvCore::Scripting::ManagedFieldValue v;
    v.tag = OVCS_FIELD_STRING;
    v.text = value;
    Require(b.EditManagedField(field, v), field);
}

static void Render(Engine& engine, OvCore::SceneSystem::Scene& scene)
{
    baregl::Framebuffer target;
    auto setup = [&](auto&) { OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target, kWidth, kHeight, true, true, true); };
    if (engine.render) engine.render->Invoke(setup); else setup(*engine.driver);
    auto output = target.RecordTarget();
    OvRendering::Entities::Camera camera;
    camera.SetClearColor({0, 0, 0});
    OvRendering::Data::Material fallback;
    OvCore::Rendering::SceneRenderRequest main{{scene, camera, kWidth, kHeight, 1, 1}};
    OvCore::Rendering::SceneFrameRequest request{main, output, fallback};
    request.generateShadows = request.captureReflections = false;
    OvCore::Rendering::RenderFrontend frontend;
    auto prepared = frontend.PrepareFrame(frontend.ExtractFrame(request), {});
    auto commands = frontend.RecordFrame(std::move(prepared));
    OvRendering::Data::RenderFrame frame;
    frame.Append(std::move(commands));
    frame.Seal();
    auto read = [&](auto&) {
        output.Bind();
        g_pixels.assign(size_t(kWidth) * kHeight * 4, 0);
        glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, g_pixels.data());
        baregl::Framebuffer::BindDefault();
    };
    if (engine.render) {
        auto done = engine.render->Submit(std::move(frame));
        engine.render->WaitIdle();
        done.RethrowIfFailed();
        engine.render->Invoke(read);
    } else {
        frame.Submit(*engine.driver);
        frame.Wait();
        frame.GetCompletion().RethrowIfFailed();
        read(*engine.driver);
    }
}

int wmain(int argc, wchar_t** argv) try
{
    Require(argc == 5, "input-controls arguments: runtime, host, InputFixture.dll, default/render-thread");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    OvReflection::MainThread authority;
    RuntimeHost host(authority);
    host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(), argv[1], argv[2]));
    std::filesystem::path assembly = argv[3], project = assembly.parent_path();
    OvCore::ResourceManagement::FontManager::ProvideAssetPaths(
        project, std::filesystem::current_path() / "Resources" / "Engine");
    OvCore::Scripting::ScriptEngine scripts(project, project);
    OvCore::Global::ServiceLocator::Provide(scripts);
    Engine engine(std::wstring(argv[4]) == L"render-thread");
    OvWindowing::Inputs::InputManager input(engine.window);
    OvCore::Global::ServiceLocator::Provide(input);
    input.SetUIViewport({{0,0},{kWidth,kHeight},{1,1}});
    OvCore::Global::ServiceLocator::Provide(engine.scenes);
    std::unique_ptr<baregl::ResourceExecutor> executor;
    if (engine.render)
        executor = std::make_unique<baregl::ResourceExecutor>([&](std::function<void()> fn) { engine.render->Invoke([&](auto&) { fn(); }); });
    Require(scripts.LoadCSharpProject(host, project, assembly, "InputFixture"), "load InputFixture");
    auto& scene = *engine.scenes.GetCurrentScene();
    auto& canvas = scene.CreateActor("Canvas");
    canvas.AddComponent<CCanvas>();
    Add(canvas,"Overload.UI.GraphicRaycaster");
    auto& events=scene.CreateActor("EventSystem");
    Add(events,"Overload.EventSystems.BaseInput");
    Add(events,"Overload.EventSystems.EventSystem");
    Add(events,"Overload.EventSystems.StandaloneInputModule");

    auto& field = scene.CreateActor("Field");
    field.SetParent(canvas);
    field.AddComponent<CRectTransform>();
    auto& fieldScript = Add(field, "InputFixture.EditBox");
    Place(field, 160, 140, 180, 30);

    auto& drop = scene.CreateActor("Drop");
    drop.SetParent(canvas);
    drop.AddComponent<CRectTransform>();
    auto& dropScript = Add(drop, "InputFixture.ChoiceBox");
    Place(drop, 160, 80, 160, 30);

    auto& factory = scene.CreateActor("Factory");
    factory.SetParent(canvas);
    factory.AddComponent<CRectTransform>();
    auto& factoryScript = Add(factory, "InputFixture.FactoryProbe");
    Place(factory, 160, 30, 10, 10);

    auto frame = OvCore::UI::ExtractFrame(scene, {kWidth, kHeight});
    for (auto* actor : scene.GetActors())
        for (auto& [key, behaviour] : actor->GetBehaviours())
            if (!behaviour.GetScript() || !behaviour.GetScript()->IsValid())
                Require(scripts.PrepareCSharp(behaviour), "prepare fixture behaviour");
    Require(fieldScript.GetScript() && fieldScript.GetScript()->IsValid(), "InputField prepared");
    Require(dropScript.GetScript() && dropScript.GetScript()->IsValid(), "Dropdown prepared");
    Require(factoryScript.GetScript() && factoryScript.GetScript()->IsValid(), "Factory prepared");

    // Seed text through the shared field ABI after prepare (OnEnable runs before
    // field restore, so constructor-time typing is overwritten by XML defaults).
    SetStr(fieldScript, "m_Text", "HelloHello");
    Require(Str(fieldScript, "m_Text") == "HelloHello", "InputField m_Text field ABI");
    SetInt(fieldScript, "m_ContentType", 7); // ContentType.Password
    OvCore::UI::ExtractFrame(scene, {kWidth, kHeight});
    Require(Str(fieldScript, "m_Text") == "HelloHello", "password keeps real text");
    // Asterisk display is applied to the label Text; raw m_Text must stay unmasked.

    // Soft keyboard adaptation after seed restore; OnUpdate runs the probe once.
    SetInt(fieldScript, "keyboardProbeRequest", 1);
    // Dropdown options and value
    Require(Int(dropScript, "m_Value") == 0, "dropdown starts at 0");
    SetInt(dropScript, "m_Value", 2);
    OvCore::UI::ExtractFrame(scene, {kWidth, kHeight});
    Require(Int(dropScript, "m_Value") == 2, "dropdown value write");

    // DefaultControls factory: OnUpdate after Play creates the standard roots.
    scene.Play();
    scene.Update(1);
    Require(Int(fieldScript, "keyboardProbeRan") == 1, "soft keyboard probe ran");
    Require(Int(fieldScript, "keyboardSupported") == 1, "TouchScreenKeyboard.isSupported desktop backend");
    Require(Int(fieldScript, "keyboardStatus") == 1, "TouchScreenKeyboard Done status");
    Require(Str(fieldScript, "keyboardText") == "HelloHello!", "TouchScreenKeyboard inject/limit text");
    Require(Str(fieldScript, "m_Text") == "HelloHello!", "InputField syncs from soft keyboard");
    std::fprintf(stderr, "factory created=%d\n", Int(factoryScript, "created"));
    Require(Int(factoryScript, "created") == 5, "DefaultControls created five roots");
    Require(scene.FindActorByName("Button") && scene.FindActorByName("InputField") &&
            scene.FindActorByName("Dropdown") && scene.FindActorByName("Scroll View"),
        "DefaultControls actors exist");

    scene.LateUpdate(1.f/60);
    OvCore::UI::ExtractFrame(scene, {kWidth, kHeight});
    scene.Update(1.f/60);scene.LateUpdate(1.f/60);
    std::fprintf(stderr,"completion checks=%d failure=%s\n",Int(factoryScript,"checks"),Str(factoryScript,"failure").c_str());
    Require(Str(factoryScript,"failure").empty(),"factory and control completion assertions");
    Require(Int(factoryScript,"checks")>=23,"completion assertions executed");
    auto* button=scene.FindActorByName("Button");
    for(auto* child:factory.GetChildren())child->SetActive(child==button);
    button->SetParent(canvas);Place(*button,160,100,160,30);
    using namespace OvWindowing::Inputs;
    auto tick=[&]{scene.Update(1.f/60);scene.LateUpdate(1.f/60);input.ClearEvents();OvCore::UI::ExtractFrame(scene,{kWidth,kHeight});};
    engine.window.UIInputEvent.Invoke({UIInputKind::Focus,1});tick();
    engine.window.UIInputEvent.Invoke({UIInputKind::PointerDown,0,{160,100}});
    engine.window.UIInputEvent.Invoke({UIInputKind::PointerUp,0,{160,100}});tick();
    Require(Int(factoryScript,"clicks")==1,"factory button receives native pointer click");
    auto* inputActor=scene.FindActorByName("InputField");
    inputActor->SetParent(canvas);Place(*inputActor,160,150,180,30);inputActor->SetActive(true);
    auto& interactive=inputActor->GetBehaviours().at(ScriptIdentity::CSharp("InputFixture","Overload.UI.InputField").Key());
    SetInt(interactive,"m_ContentType",0);SetInt(interactive,"m_InputType",0);SetInt(interactive,"m_LineType",0);SetStr(interactive,"m_Text","");tick();
    auto key=[&](int code){engine.window.UIInputEvent.Invoke({UIInputKind::KeyDown,code});engine.window.UIInputEvent.Invoke({UIInputKind::KeyUp,code});tick();};
    auto clickInput=[&]{engine.window.UIInputEvent.Invoke({UIInputKind::PointerDown,0,{160,50}});engine.window.UIInputEvent.Invoke({UIInputKind::PointerUp,0,{160,50}});tick();};
    clickInput();
    engine.window.UIInputEvent.Invoke({UIInputKind::Character,0x1f600});tick();
    Require(Str(interactive,"m_Text")=="\xf0\x9f\x98\x80","native character input preserves supplementary Unicode");
    key(259);Require(Str(interactive,"m_Text").empty(),"native Backspace deletes complete Unicode scalar");
    engine.window.UIInputEvent.Invoke({UIInputKind::Character,'a'});tick();key(257);
    engine.window.UIInputEvent.Invoke({UIInputKind::Character,'b'});tick();
    Require(Str(interactive,"m_Text")=="a","Enter ends editing without immediate submit reactivation");
    clickInput();engine.window.UIInputEvent.Invoke({UIInputKind::Focus,0});tick();
    engine.window.UIInputEvent.Invoke({UIInputKind::Focus,1});tick();
    engine.window.UIInputEvent.Invoke({UIInputKind::Character,'c'});tick();
    Require(Str(interactive,"m_Text")=="a","window focus loss releases input field editing");
    inputActor->SetActive(false);button->SetActive(false);
    auto* sliderActor=scene.FindActorByName("Slider");sliderActor->SetParent(canvas);Place(*sliderActor,160,100,200,20);sliderActor->SetActive(true);
    auto& sliderScript=sliderActor->GetBehaviours().at(ScriptIdentity::CSharp("InputFixture","Overload.UI.Slider").Key());
    SetInt(sliderScript,"m_Direction",0);tick();
    engine.window.UIInputEvent.Invoke({UIInputKind::PointerDown,0,{110,100}});
    engine.window.UIInputEvent.Invoke({UIInputKind::PointerUp,0,{110,100}});tick();
    bool sliderMoved=false;
    for(const auto& field:sliderScript.GetScript()->ManagedFields())if(field.name=="m_Value")
        sliderMoved=std::abs(sliderScript.GetScript()->ReadManagedField(field.token)->scalar.float32_value-0.25f)<0.03f;
    Require(sliderMoved,"native slider click uses local rectangle coordinates");sliderActor->SetActive(false);
    auto* scrollActor=scene.FindActorByName("Scroll View");scrollActor->SetParent(canvas);Place(*scrollActor,160,100,200,150);scrollActor->SetActive(true);
    auto& scrollScript=scrollActor->GetBehaviours().at(ScriptIdentity::CSharp("InputFixture","Overload.UI.ScrollRect").Key());
    SetInt(scrollScript,"m_MovementType",2);tick();
    auto* contentRect=scrollActor->GetChildren()[0]->GetChildren()[0]->GetComponent<CRectTransform>();
    float beforeDrag=contentRect->GetAnchoredPosition().y;
    engine.window.UIInputEvent.Invoke({UIInputKind::PointerDown,0,{160,100}});tick();
    engine.window.UIInputEvent.Invoke({UIInputKind::PointerMove,0,{160,50}});tick();
    engine.window.UIInputEvent.Invoke({UIInputKind::PointerUp,0,{160,50}});tick();
    Require(contentRect->GetAnchoredPosition().y>beforeDrag+1,"native ScrollRect drag moves content after capture");
    Render(engine, scene);
    const auto out = project / (std::wstring(argv[4]) + L".rgba");
    std::ofstream capture(out, std::ios::binary);
    capture.write(reinterpret_cast<const char*>(g_pixels.data()), std::streamsize(g_pixels.size()));
    capture.close();
    scripts.ClearCSharpProject();
    engine.scenes.UnloadCurrentScene();
    host.Shutdown();
    std::puts("PASS U11-U12 InputField, Dropdown, DefaultControls");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "U11-U12 validation failed: %s\n", error.what());
    return 1;
}
