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

using namespace OvCore::ECS::Components;
using OvCore::Scripting::ScriptIdentity;

static constexpr uint16_t kWidth = 320, kHeight = 200;
static std::vector<uint8_t> g_pixels;

static Behaviour& Add(OvCore::ECS::Actor& actor, const char* name)
{ return actor.AddBehaviour(ScriptIdentity::CSharp("ControlFixture", name)); }

static void Place(OvCore::ECS::Actor& actor, float x, float y, float w, float h)
{
    auto* rect = actor.GetComponent<CRectTransform>();
    rect->SetAnchorMin({0, 0}); rect->SetAnchorMax({0, 0});
    rect->SetPivot({0.5f, 0.5f});
    rect->SetAnchoredPosition({x, y});
    rect->SetSizeDelta({w, h});
}

static void SetBool(Behaviour& b, const char* field, bool value)
{
    OvCore::Scripting::ManagedFieldValue v;
    v.tag = OVCS_FIELD_BOOL;
    v.scalar.boolean = value;
    Require(b.EditManagedField(field, v), field);
}

static void ExpectColor(int x, int y, int r, int g, int b, const char* what, int tol = 18)
{
    const size_t index = (size_t(y) * kWidth + x) * 4;
    auto ch = [&](int actual, int expected, const char* axis) {
        if (std::abs(actual - expected) > tol) {
            std::fprintf(stderr, "%s %s (%d,%d)=(%d,%d,%d) expected %d,%d,%d\n",
                what, axis, x, y, g_pixels[index], g_pixels[index + 1], g_pixels[index + 2], r, g, b);
            throw std::runtime_error(what);
        }
    };
    ch(g_pixels[index], r, "r"); ch(g_pixels[index + 1], g, "g"); ch(g_pixels[index + 2], b, "b");
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
    Require(argc == 5, "controls arguments: runtime, host, ControlFixture.dll, default/render-thread");
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
    Require(scripts.LoadCSharpProject(host, project, assembly, "ControlFixture"), "load ControlFixture");
    auto& scene = *engine.scenes.GetCurrentScene();
    auto& canvas = scene.CreateActor("Canvas");
    canvas.AddComponent<CCanvas>();

    auto& button = scene.CreateActor("Button");
    button.SetParent(canvas);
    button.AddComponent<CRectTransform>();
    Add(button, "ControlFixture.ProbeImage");
    auto& buttonGraphic = Add(button, "ControlFixture.TintButton");
    Place(button, 60, 100, 80, 40);

    auto& toggle = scene.CreateActor("Toggle");
    toggle.SetParent(canvas);
    toggle.AddComponent<CRectTransform>();
    Add(toggle, "ControlFixture.ProbeImage");
    auto& toggleScript = Add(toggle, "ControlFixture.CounterToggle");
    Place(toggle, 60, 40, 40, 40);

    auto& slider = scene.CreateActor("Slider");
    slider.SetParent(canvas);
    slider.AddComponent<CRectTransform>();
    auto& sliderScript = Add(slider, "ControlFixture.ValueSlider");
    Place(slider, 200, 100, 100, 20);

    auto& scroller = scene.CreateActor("Scroll");
    scroller.SetParent(canvas);
    scroller.AddComponent<CRectTransform>();
    auto& scrollScript = Add(scroller, "ControlFixture.ScrollProbe");
    Place(scroller, 200, 40, 100, 40);

    auto frame = OvCore::UI::ExtractFrame(scene, {kWidth, kHeight});
    Require(buttonGraphic.GetScript()->IsValid(), "Button prepared");
    Require(toggleScript.GetScript()->IsValid() && sliderScript.GetScript()->IsValid() && scrollScript.GetScript()->IsValid(),
        "Toggle/Slider/Scroll prepared");

    scene.Play();scene.Update(1.f/60);scene.LateUpdate(1.f/60);
    // ColorTint disabled state applies through the shared field ABI.
    SetBool(buttonGraphic, "m_Interactable", false);
    OvCore::UI::ExtractFrame(scene, {kWidth, kHeight});
    Render(engine, scene);
    // Reject unchanged white as well as missing black geometry.
    const size_t bi = (size_t(100) * kWidth + 60) * 4;
    Require(std::abs(int(g_pixels[bi]) - int(g_pixels[bi + 1])) < 24 &&
            std::abs(int(g_pixels[bi + 1]) - int(g_pixels[bi + 2])) < 24 &&
            g_pixels[bi] > 80 && g_pixels[bi] < 220, "Button ColorTint disabled differs from normal white");
    SetBool(buttonGraphic, "m_Interactable", true);
    OvCore::UI::ExtractFrame(scene, {kWidth, kHeight});
    Render(engine, scene);

    const auto out = project / (std::wstring(argv[4]) + L".rgba");
    std::ofstream capture(out, std::ios::binary);
    capture.write(reinterpret_cast<const char*>(g_pixels.data()), std::streamsize(g_pixels.size()));
    capture.close();
    scripts.ClearCSharpProject();
    engine.scenes.UnloadCurrentScene();
    host.Shutdown();
    std::puts("PASS U8-U10 controls prepare and ColorTint disabled tint");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "U8-U10 controls validation failed: %s\n", error.what());
    return 1;
}
