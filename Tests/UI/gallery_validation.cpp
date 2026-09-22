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
#include <tinyxml2.h>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace OvCore::ECS::Components;
using OvCore::UI::Rect;

static constexpr uint16_t kWidth = 960, kHeight = 540;
static std::vector<uint8_t> g_pixels;

static bool Near(float a, float b, float eps = 0.02f) { return std::abs(a - b) <= eps; }

static void ExpectChannel(int actual, int expected, int tol, const char* what)
{
    if (std::abs(actual - expected) > tol)
    {
        std::fprintf(stderr, "%s: channel %d expected %d±%d\n", what, actual, expected, tol);
        throw std::runtime_error(what);
    }
}

static void ExpectColor(int x, int y, int r, int g, int b, const char* what, int tol = 12)
{
    const size_t index = (size_t(y) * kWidth + x) * 4;
    if (index + 2 >= g_pixels.size()) throw std::runtime_error("pixel out of range");
    ExpectChannel(g_pixels[index], r, tol, what);
    ExpectChannel(g_pixels[index + 1], g, tol, what);
    ExpectChannel(g_pixels[index + 2], b, tol, what);
}

static std::array<int, 2> CenterOf(const Rect& rect)
{
    return { int(rect.min.x + rect.size.x * 0.5f), int(rect.min.y + rect.size.y * 0.5f) };
}

static Rect CanvasRectOf(CRectTransform* leaf)
{
    std::vector<CRectTransform*> chain;
    for (CRectTransform* r = leaf; r != nullptr;)
    {
        chain.push_back(r);
        auto* parentActor = r->owner.GetParent();
        r = parentActor ? parentActor->GetComponent<CRectTransform>() : nullptr;
    }
    OvMaths::FVector2 min{0, 0}, size{0, 0};
    for (int i = int(chain.size()) - 1; i >= 0; --i)
    {
        auto* r = chain[i];
        const auto local = r->GetRect();
        const auto next = local.size;
        if (i == int(chain.size()) - 1) { min = {0, 0}; size = next; continue; }
        const auto low = r->GetAnchorMin(), high = r->GetAnchorMax(), p = r->GetPivot();
        const auto pos = r->GetAnchoredPosition();
        min = {
            min.x + size.x * (low.x + (high.x - low.x) * p.x) + pos.x - next.x * p.x,
            min.y + size.y * (low.y + (high.y - low.y) * p.y) + pos.y - next.y * p.y
        };
        size = next;
    }
    return Rect{ min, size };
}

static Rect RectOf(OvCore::SceneSystem::Scene& scene, const char* name)
{
    auto* actor = scene.FindActorByName(name);
    Require(actor != nullptr, name);
    auto* rect = actor->GetComponent<CRectTransform>();
    Require(rect != nullptr, name);
    const auto value = CanvasRectOf(rect);
    Require(!value.Empty(), name);
    return value;
}

static void ExpectActorColor(OvCore::SceneSystem::Scene& scene, const char* name,
    int r, int g, int b, const char* what, int tol = 12)
{
    const auto c = CenterOf(RectOf(scene, name));
    ExpectColor(c[0], c[1], r, g, b, what, tol);
}

static void RenderAndRead(Engine& engine, OvCore::SceneSystem::Scene& scene)
{
    baregl::Framebuffer target;
    auto setup = [&](auto&) { OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target, kWidth, kHeight, true, true, true); };
    if (engine.render) engine.render->Invoke(setup); else setup(*engine.driver);
    auto output = target.RecordTarget();
    OvRendering::Entities::Camera camera;
    camera.SetClearColor({ 0.05f, 0.06f, 0.08f });
    OvRendering::Data::Material fallback;
    OvCore::Rendering::SceneRenderRequest main{ { scene, camera, kWidth, kHeight, 1, 1 } };
    OvCore::Rendering::SceneFrameRequest request{ main, output, fallback };
    request.generateShadows = request.captureReflections = false;
    OvCore::Rendering::RenderFrontend frontend;
    auto extracted = frontend.ExtractFrame(request);
    Require(extracted != nullptr, "gallery produces a scene frame");
    auto prepared = frontend.PrepareFrame(std::move(extracted), {});
    auto commands = frontend.RecordFrame(std::move(prepared));
    OvRendering::Data::RenderFrame frame;
    frame.Append(std::move(commands));
    frame.Seal();
    auto read = [&](auto&)
    {
        output.Bind();
        g_pixels.assign(size_t(kWidth) * kHeight * 4, 0);
        glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, g_pixels.data());
        Require(glGetError() == GL_NO_ERROR, "gallery GL readback");
        baregl::Framebuffer::BindDefault();
    };
    if (engine.render)
    {
        Require(glfwGetCurrentContext() == nullptr, "gallery Main never acquires Render context");
        auto done = engine.render->Submit(std::move(frame));
        engine.render->WaitIdle();
        done.RethrowIfFailed();
        engine.render->Invoke(read);
    }
    else
    {
        frame.Submit(*engine.driver);
        frame.Wait();
        frame.GetCompletion().RethrowIfFailed();
        read(*engine.driver);
    }
}

static void CheckAnchors(OvCore::SceneSystem::Scene& scene)
{
    auto ui = OvCore::UI::ExtractFrame(scene, { kWidth, kHeight });
    Require(ui.meshes.size() + ui.images.size() >= 20, "gallery frame has expected graphic count");

    // Solid layout tiles after GridLayoutGroup placement.
    const std::pair<const char*, std::array<int, 3>> tiles[] = {
        { "Cell1", { 64, 140, 242 } },   // BlueTile
        { "Cell2", { 51, 166, 153 } },   // TealTile
        { "Cell3", { 230, 128, 64 } },   // OrangeTile
        { "Cell4", { 128, 102, 204 } },  // PurpleTile
        { "Cell5", { 89, 102, 140 } },   // SlateTile (0.9 alpha over SoftImage)
        { "Cell6", { 217, 102, 115 } },  // CoralTile
    };
    for (const auto& [name, rgb] : tiles)
    {
        // Cell5 is the only translucent slate tile; allow alpha-blend slack.
        const int tol = std::string(name) == "Cell5" ? 40 : 12;
        ExpectActorColor(scene, name, rgb[0], rgb[1], rgb[2], name, tol);
    }

    // Pointer probe default blue.
    ExpectActorColor(scene, "PulseTarget", 71, 115, 217, "PulseTarget normal blue");

    // Header/panel chrome is dark slate; sample a corner away from the title text.
    {
        const auto header = RectOf(scene, "Header");
        ExpectColor(int(header.min.x + 12), int(header.min.y + 12), 41, 46, 61, "Header panel chrome", 18);
    }

    // Nested layout block keeps its soft backing visible near an edge sample.
    {
        const auto nested = RectOf(scene, "NestedColumn");
        ExpectColor(int(nested.min.x + 3), int(nested.min.y + 3), 70, 80, 110, "NestedColumn soft fill", 35);
    }

    // RectMask2D panel interior is non-empty (text/slate), exterior of the clip
    // sample stays on ClipColumn chrome.
    const auto clip = RectOf(scene, "ClipPanel");
    const auto clipCenter = CenterOf(clip);
    const size_t clipIndex = (size_t(clipCenter[1]) * kWidth + clipCenter[0]) * 4;
    Require(g_pixels[clipIndex] + g_pixels[clipIndex + 1] + g_pixels[clipIndex + 2] > 40,
        "RectMask2D panel has visible content");

    // Mask children sit inside the stencil window; opaque tile centers must match.
    ExpectActorColor(scene, "MaskA", 217, 102, 115, "MaskA coral through stencil");
    ExpectActorColor(scene, "MaskB", 230, 128, 64, "MaskB orange through stencil");
    ExpectActorColor(scene, "MaskC", 128, 102, 204, "MaskC purple through stencil");

    // CanvasGroup dim multiplies child caption over panel; center should differ
    // from undimmed panel chrome and remain mid-toned.
    const auto group = CenterOf(RectOf(scene, "GroupPanel"));
    const size_t gi = (size_t(group[1]) * kWidth + group[0]) * 4;
    Require(g_pixels[gi] < 200 && g_pixels[gi + 1] < 200, "CanvasGroup dimmed panel is not full-bright");

    // AspectRatioFitter square uses the checker graphic (orange/dark cells).
    const auto aspect = CenterOf(RectOf(scene, "AspectSquare"));
    const size_t ai = (size_t(aspect[1]) * kWidth + aspect[0]) * 4;
    Require(g_pixels[ai] + g_pixels[ai + 1] + g_pixels[ai + 2] > 30, "Aspect checker paints pixels");

    // Best-fit / text row exists in the footer band (non-chrome).
    const auto fit = CenterOf(RectOf(scene, "BestFitBox"));
    const size_t fi = (size_t(fit[1]) * kWidth + fit[0]) * 4;
    Require(g_pixels[fi] + g_pixels[fi + 1] + g_pixels[fi + 2] > 50, "BestFit text paints");
}

int wmain(int argc, wchar_t** argv) try
{
    Require(argc == 5, "gallery arguments: runtime, host, GameScripts.dll, default/render-thread");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    OvReflection::MainThread authority;
    RuntimeHost host(authority);
    host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(), argv[1], argv[2]));
    std::filesystem::path assembly = argv[3], project = assembly.parent_path();
    // Empty Font AssetRef resolves to engine ':Fonts/Roboto-Regular.ttf'.
    OvCore::ResourceManagement::FontManager::ProvideAssetPaths(
        project, std::filesystem::current_path() / "Resources" / "Engine");
    OvCore::Scripting::ScriptEngine scripts(project, project);
    OvCore::Global::ServiceLocator::Provide(scripts);
    Engine engine(std::wstring(argv[4]) == L"render-thread");
    OvCore::Global::ServiceLocator::Provide(engine.scenes);
    std::unique_ptr<baregl::ResourceExecutor> executor;
    if (engine.render)
        executor = std::make_unique<baregl::ResourceExecutor>([&](std::function<void()> fn) { engine.render->Invoke([&](auto&) { fn(); }); });

    Require(scripts.LoadCSharpProject(host, project, assembly, "GameScripts"), "load UIGallery GameScripts");

    tinyxml2::XMLDocument doc;
    Require(doc.LoadFile("Examples/UIGallery/Assets/Scenes/UIGallery.ovscene") == tinyxml2::XML_SUCCESS,
        "load UIGallery.ovscene");
    auto& scene = *engine.scenes.GetCurrentScene();
    scene.OnDeserialize(doc, doc.RootElement()->FirstChildElement("scene"));

    // First edit-mode ExtractFrame prepares UI types (no Inspector, no Play).
    auto first = OvCore::UI::ExtractFrame(scene, { kWidth, kHeight });
    Require(first.meshes.size() + first.images.size() >= 20, "first frame initializes gallery UI");
    for (auto* actor : scene.GetActors())
        for (auto& [key, behaviour] : actor->GetBehaviours())
            Require(behaviour.GetScript() && behaviour.GetScript()->IsValid(), "gallery UI behaviours prepared");
    // Nested layout groups settle across rebuild passes after parent resizes.
    for (int pass = 0; pass < 3; ++pass) OvCore::UI::ExtractFrame(scene, { kWidth, kHeight });

    RenderAndRead(engine, scene);
    CheckAnchors(scene);

    const auto out = project / (std::wstring(argv[4]) + L".rgba");
    std::ofstream capture(out, std::ios::binary);
    capture.write(reinterpret_cast<const char*>(g_pixels.data()), std::streamsize(g_pixels.size()));
    capture.close();

    scripts.ClearCSharpProject();
    engine.scenes.UnloadCurrentScene();
    host.Shutdown();
    std::puts("PASS UIGallery production pixels and anchors");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "UIGallery validation failed: %s\n", error.what());
    return 1;
}
