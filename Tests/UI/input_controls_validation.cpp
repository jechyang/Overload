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
    OvCore::Global::ServiceLocator::Provide(engine.scenes);
    std::unique_ptr<baregl::ResourceExecutor> executor;
    if (engine.render)
        executor = std::make_unique<baregl::ResourceExecutor>([&](std::function<void()> fn) { engine.render->Invoke([&](auto&) { fn(); }); });
    Require(scripts.LoadCSharpProject(host, project, assembly, "InputFixture"), "load InputFixture");
    auto& scene = *engine.scenes.GetCurrentScene();
    auto& canvas = scene.CreateActor("Canvas");
    canvas.AddComponent<CCanvas>();

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
