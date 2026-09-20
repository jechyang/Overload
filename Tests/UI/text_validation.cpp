#define wmain FontUnusedHostMain
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
#include <fstream>
#include <set>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <map>

using namespace OvCore::ECS::Components;
using Actor = OvCore::ECS::Actor;
using Scene = OvCore::SceneSystem::Scene;
using Behaviour = OvCore::ECS::Components::Behaviour;
using OvCore::Scripting::ScriptIdentity;
static Behaviour& AddText(Actor& actor, const char* type) { return actor.AddBehaviour(ScriptIdentity::CSharp("FontFixture", type)); }
static int Field(Behaviour& behaviour, const char* name)
{
    for (const auto& field : behaviour.GetScript()->ManagedFields()) if (field.name == name)
        return behaviour.GetScript()->ReadManagedField(field.token)->scalar.int32_value;
    throw std::runtime_error("Missing font fixture field");
}
#include "font_bridge_checks.h"
static Actor& Rectangle(Scene& scene, Actor& parent, const char* name, float x, float y, float width, float height)
{
    auto& actor = scene.CreateActor(name); actor.SetParent(parent);
    auto& rect = actor.AddComponent<CRectTransform>();
    rect.SetAnchorMin({0, 0}); rect.SetAnchorMax({0, 0}); rect.SetPivot({0, 0});
    rect.SetAnchoredPosition({x, y}); rect.SetSizeDelta({width, height}); return actor;
}
static std::vector<uint8_t> Pixels(const baregl::Framebuffer::Snapshot& target, uint16_t width, uint16_t height)
{
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    target.Bind(); glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()); baregl::Framebuffer::BindDefault(); return pixels;
}
static OvRendering::Data::RenderFrame Record(OvCore::Rendering::RenderFrontend& frontend, Scene& scene,
    OvRendering::Entities::Camera& camera, const baregl::Framebuffer::Snapshot& target, uint16_t width, uint16_t height)
{
    OvRendering::Data::Material fallback;
    OvCore::Rendering::SceneRenderRequest main{{scene, camera, width, height, 1, 1}};
    OvCore::Rendering::SceneFrameRequest request{main, target, fallback}; request.generateShadows = request.captureReflections = false;
    auto extracted = frontend.ExtractFrame(request);
    auto prepared = frontend.PrepareFrame(std::move(extracted), {});
    auto commands = frontend.RecordFrame(std::move(prepared));
    OvRendering::Data::RenderFrame frame; frame.Append(std::move(commands)); frame.Seal(); return frame;
}
static std::vector<uint8_t> Render(Engine& engine, Scene& scene, OvRendering::Entities::Camera& camera, uint16_t width = 320, uint16_t height = 200)
{
    baregl::Framebuffer target; OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target, width, height, true, true, true);
    auto snapshot = target.RecordTarget(); OvCore::Rendering::RenderFrontend frontend;
    auto frame = Record(frontend, scene, camera, snapshot, width, height);
    if (engine.render)
    {
        auto done = engine.render->Submit(std::move(frame)); engine.render->WaitIdle(); done.RethrowIfFailed();
        return engine.render->Invoke([&](auto&) { return Pixels(snapshot, width, height); });
    }
    frame.Submit(*engine.driver); frame.Wait(); frame.GetCompletion().RethrowIfFailed(); return Pixels(snapshot, width, height);
}
static void Pixel(const std::vector<uint8_t>& pixels, int x, int y, int min, int max, uint16_t width = 320)
{
    auto value = pixels[(size_t(y) * width + x) * 4];
    if (value < min || value > max) std::fprintf(stderr, "pixel (%d,%d)=%u expected [%d,%d]\n", x, y, value, min, max);
    Require(value >= min && value <= max, "production text interior/exterior pixel");
}
static void EdgeWidth(const std::vector<uint8_t>& pixels, int y, int expected, uint16_t width = 320)
{
    int covered = 0; for (int x = 0; x < width; ++x) if (pixels[(size_t(y) * width + x) * 4] >= 128) ++covered;
    Require(std::abs(covered - expected) <= 1, "SDF half-coverage edge matches analytic outline within one pixel");
}
static void CheckPagePixels(const OvCore::UI::Frame& frame, uint64_t actor, const std::vector<uint8_t>& pixels)
{
    std::map<std::pair<uint64_t, uint32_t>, bool> checked;
    for (const auto& mesh : frame.meshes) if (mesh.actorGuid == actor) for (const auto& slot : mesh.materialSlots)
    {
        if (!slot.fontPage) continue;
        const auto& page = *slot.fontPage;
        auto& sampled = checked[{page.generation, page.index}];
        if (sampled) continue;
        for (uint32_t i = slot.firstIndex; i < slot.firstIndex + slot.indexCount && !sampled; i += 3)
        {
            const auto& a = mesh.vertices[mesh.indices[i]], & b = mesh.vertices[mesh.indices[i + 1]], & c = mesh.vertices[mesh.indices[i + 2]];
            const float denominator = (b.position.y - c.position.y) * (a.position.x - c.position.x) + (c.position.x - b.position.x) * (a.position.y - c.position.y);
            if (std::abs(denominator) < .00001f) continue;
            int minx = std::max(0, static_cast<int>(std::floor(std::min({a.position.x, b.position.x, c.position.x}))));
            int maxx = std::min(319, static_cast<int>(std::ceil(std::max({a.position.x, b.position.x, c.position.x}))));
            int miny = std::max(0, static_cast<int>(std::floor(std::min({a.position.y, b.position.y, c.position.y}))));
            int maxy = std::min(199, static_cast<int>(std::ceil(std::max({a.position.y, b.position.y, c.position.y}))));
            for (int y = miny; y <= maxy && !sampled; ++y) for (int x = minx; x <= maxx && !sampled; ++x)
            {
                float wa = ((b.position.y - c.position.y) * (x + .5f - c.position.x) + (c.position.x - b.position.x) * (y + .5f - c.position.y)) / denominator;
                float wb = ((c.position.y - a.position.y) * (x + .5f - c.position.x) + (a.position.x - c.position.x) * (y + .5f - c.position.y)) / denominator;
                float wc = 1 - wa - wb;
                if (wa < 0 || wb < 0 || wc < 0) continue;
                float u = (a.uv.x * wa + b.uv.x * wb + c.uv.x * wc) * page.size - .5f;
                float v = (a.uv.y * wa + b.uv.y * wb + c.uv.y * wc) * page.size - .5f;
                int tx = static_cast<int>(std::floor(u)), ty = static_cast<int>(std::floor(v));
                float fx = u - tx, fy = v - ty;
                auto texel = [&](int xx, int yy) { return float(page.pixels[size_t(std::clamp(yy, 0, int(page.size) - 1)) * page.size + std::clamp(xx, 0, int(page.size) - 1)]); };
                float value = (texel(tx, ty) * (1 - fx) + texel(tx + 1, ty) * fx) * (1 - fy) + (texel(tx, ty + 1) * (1 - fx) + texel(tx + 1, ty + 1) * fx) * fy;
                if (value > 140) { Require(pixels[(size_t(y) * 320 + x) * 4] > 100, "each atlas page/fallback generation contributes actual SDF pixels"); sampled = true; }
            }
        }
    }
    Require(!checked.empty() && std::all_of(checked.begin(), checked.end(), [](const auto& item) { return item.second; }), "GPU samples cover every Text atlas page");
}
int wmain(int argc, wchar_t** argv) try
{
    Require(argc == 6, "text validation: runtime, host, fixture assembly, mode, evidence");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    OvReflection::MainThread main; RuntimeHost host(main);
    host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(), argv[1], argv[2]));
    const std::filesystem::path assembly = argv[3], project = assembly.parent_path(), evidence = argv[5];
    OvCore::ResourceManagement::FontManager::ProvideAssetPaths(project, project);
    OvCore::Scripting::ScriptEngine scripts(project, project); OvCore::Global::ServiceLocator::Provide(scripts);
    const bool threaded = std::wstring(argv[4]) == L"render-thread";
    Engine engine(threaded); OvCore::Global::ServiceLocator::Provide(engine.scenes);
    std::unique_ptr<baregl::ResourceExecutor> executor;
    if (threaded) executor = std::make_unique<baregl::ResourceExecutor>([&](std::function<void()> fn) { engine.render->Invoke([&](auto&) { fn(); }); });
    Require(scripts.LoadCSharpProject(host, project, assembly, "FontFixture"), "load Text subclass through real CLR");
    auto& scene = *engine.scenes.GetCurrentScene();
    FontBridgeChecks(host, scene);
    auto& cameraActor = scene.CreateActor("Camera"); auto& camera = cameraActor.AddComponent<CCamera>(); camera.GetCamera().SetClearColor({0, 0, 0});
    auto& canvasActor = scene.CreateActor("Canvas"); auto& canvas = canvasActor.AddComponent<CCanvas>();
    auto& label = Rectangle(scene, canvasActor, "Label", 20, 20, 80, 80); auto& script = AddText(label, "FontFixture.Label");
    auto& defaultLabel = Rectangle(scene, canvasActor, "DefaultFont", 200, 0, 100, 30);
    auto& defaultScript = AddText(defaultLabel, "FontFixture.DefaultFont");
    auto& gameplay = AddText(scene.CreateActor("Gameplay"), "FontFixture.GameplayOnly");
    Require(!script.GetScript()->IsValid(), "Text remains metadata-only before first edit frame");
    auto first = OvCore::UI::ExtractFrame(scene, {320, 200});
    Require(first.meshes.size() == 2 && first.meshes[0].indices.size() == 6, "first edit frame produces C# Text geometry");
    Require(Field(script, "awakes") == 0 && !gameplay.GetScript()->IsValid(), "Text preparation does not start gameplay");
    auto* renderer = label.GetComponent<CCanvasRenderer>();
    Require(renderer->GetMaterialSlots().size() == 1 && renderer->GetMaterialSlots()[0].fontPage, "Text pins native atlas through official material slot");
    auto page = renderer->GetMaterialSlots()[0].fontPage;
    uint64_t generation = renderer->GetMeshGeneration();
    auto baseline = Render(engine, scene, camera.GetCamera()); Pixel(baseline, 30, 40, 245, 255); Pixel(baseline, 46, 40, 0, 5);
    bool defaultFontRead = false;
    for (const auto& field : defaultScript.GetScript()->ManagedFields())
    {
        auto value = defaultScript.GetScript()->ReadManagedField(field.token);
        Require(value.has_value(), "default Text inherited fields are readable through Inspector ABI");
        if (field.name == "m_Font") defaultFontRead = value->tag == OVCS_FIELD_ASSET_REF && value->auxiliary == 8 && value->text.empty();
    }
    Require(defaultFontRead, "default Text font field stores legal empty Font AssetRef");
    CheckPagePixels(OvCore::UI::ExtractFrame(scene, {320, 200}), defaultLabel.GetGUID(), baseline);
    EdgeWidth(baseline, 40, 20);
    Require(renderer->GetMeshGeneration() == generation && renderer->GetMaterialSlots()[0].fontPage == page, "unchanged text does not regenerate/upload glyphs or mesh");
    OvCore::Scripting::ManagedFieldValue sizeValue; sizeValue.tag = OVCS_FIELD_INT32; sizeValue.scalar.int32_value = 48;
    Require(script.EditManagedField("m_FontSize", sizeValue), "Text Inspector field edit uses shared serialization ABI");
    tinyxml2::XMLDocument document; auto* xml = document.NewElement("root"); document.InsertEndChild(xml); scene.OnSerialize(document, xml);
    Require(document.SaveFile((evidence / "Text.ovscene").string().c_str()) == tinyxml2::XML_SUCCESS, "serialize Text component fields");
    {
        Scene restored(project, project); restored.OnDeserialize(document, xml->FirstChildElement("scene"));
        auto* restoredLabel = restored.FindActorByName("Label");
        auto* restoredScript = restoredLabel->GetBehaviour(ScriptIdentity::CSharp("FontFixture", "FontFixture.Label"));
        Require(restoredScript && scripts.PrepareCSharp(*restoredScript) && Field(*restoredScript, "m_FontSize") == 48,
            "private inherited Text fields survive XML roundtrip");
        auto restoredFrame = OvCore::UI::ExtractFrame(restored, {320, 200});
        Require(!restoredFrame.meshes.empty() && restoredLabel->GetComponent<CCanvasRenderer>()->GetMaterialSlots()[0].fontPage,
            "deserialized Text regenerates native page bindings");
    }
    sizeValue.scalar.int32_value = 40; Require(script.EditManagedField("m_FontSize", sizeValue), "restore authored font size");
    std::ofstream image(evidence / (threaded ? "text-thread.rgba" : "text-default.rgba"), std::ios::binary);
    image.write(reinterpret_cast<const char*>(baseline.data()), static_cast<std::streamsize>(baseline.size()));
    canvas.SetScaleFactor(2);
    auto dpi = Render(engine, scene, camera.GetCamera(), 640, 400); Pixel(dpi, 60, 80, 245, 255, 640);
    EdgeWidth(dpi, 80, 40, 640);
    Require(renderer->GetMaterialSlots()[0].fontPage == page, "DPI scale reuses fixed-em SDF atlas"); canvas.SetScaleFactor(1);
    scene.Play(); Require(Field(script, "awakes") == 1, "Text Play lifecycle starts once");
    scene.Update(1); auto small = Render(engine, scene, camera.GetCamera()); EdgeWidth(small, 28, 8);
    Require(renderer->GetMaterialSlots()[0].fontPage == page, "font size 16 reuses SDF page");
    scene.Update(2); auto large = Render(engine, scene, camera.GetCamera()); Pixel(large, 36, 50, 245, 255);
    EdgeWidth(large, 50, 32);
    Require(renderer->GetMaterialSlots()[0].fontPage == page, "font size 64 reuses SDF page");
    scene.Update(3); Render(engine, scene, camera.GetCamera());
    Require(renderer->GetMaterialSlots()[0].fontPage->generation != page->generation, "font resource switch binds a different atlas generation");
    scene.Update(4); Render(engine, scene, camera.GetCamera());
    label.SetActive(false);
    auto disabled = OvCore::UI::ExtractFrame(scene, {320, 200});
    Require(std::none_of(disabled.meshes.begin(), disabled.meshes.end(), [&](const auto& mesh) { return mesh.actorGuid == label.GetGUID(); }), "disabled Text excluded");
    label.SetActive(true); Render(engine, scene, camera.GetCamera());
    scene.Update(5); auto rich = Render(engine, scene, camera.GetCamera());
    Pixel(rich, 30, 40, 240, 255);
    const size_t red = (40 * 320 + 46) * 4;
    Require(rich[red] > 240 && rich[red + 1] < 10 && rich[red + 2] < 10, "rich text style/page/color survive production SDF shader");
    Pixel(rich, 55, 40, 0, 5);
    scene.Update(6); auto fitted = Render(engine, scene, camera.GetCamera());
    Require(Field(script, "sizeUsed") == 38, "best fit uses batch kerning to select exact largest fitting size");
    EdgeWidth(fitted, 40, 76);
    scene.Update(7); auto tightened = Render(engine, scene, camera.GetCamera());
    Pixel(tightened, 30, 65, 240, 255); Pixel(tightened, 30, 35, 240, 255);
    scene.Update(4); Render(engine, scene, camera.GetCamera());
    // Both scene passes use the same C# mesh and pinned SDF page.
    canvas.SetRenderMode(CCanvas::RenderMode::ScreenSpaceCamera); canvas.SetWorldCamera(&camera); canvas.SetPlaneDistance(5);
    auto cameraPixels = Render(engine, scene, camera.GetCamera());
    { std::ofstream dump(evidence / "camera.rgba", std::ios::binary); dump.write(reinterpret_cast<const char*>(cameraPixels.data()), static_cast<std::streamsize>(cameraPixels.size())); }
    const bool cameraCorrect = cameraPixels[(40 * 320 + 30) * 4] >= 240;
    if (!cameraCorrect) std::fprintf(stderr, "FAIL (deferred): Camera Canvas off-center text pixel; continuing other diagnostics\n");
    canvas.SetRenderMode(CCanvas::RenderMode::WorldSpace);
    canvasActor.GetComponent<CRectTransform>()->SetSizeDelta({320, 200});
    canvasActor.transform.SetLocalPosition({0, 0, 5}); canvasActor.transform.SetLocalScale({.01f, .01f, .01f});
    auto worldPixels = Render(engine, scene, camera.GetCamera());
    size_t lit = 0; for (size_t i = 0; i < worldPixels.size(); i += 4) if (worldPixels[i] > 100) ++lit;
    Require(lit > 10 && lit < 10000, "World Space SDF production pixels");
    canvasActor.transform.SetLocalPosition({0, 0, 0}); canvasActor.transform.SetLocalScale({1, 1, 1}); canvas.SetRenderMode(CCanvas::RenderMode::ScreenSpaceOverlay);
    auto& multi = Rectangle(scene, canvasActor, "MultiPage", 0, 100, 300, 100); AddText(multi, "FontFixture.MultiPage");
    auto multiPixels = Render(engine, scene, camera.GetCamera());
    auto* multiRenderer = multi.GetComponent<CCanvasRenderer>(); std::set<uint32_t> pages;
    for (const auto& slot : multiRenderer->GetMaterialSlots()) if (slot.fontPage) pages.insert(slot.fontPage->index);
    Require(pages.size() > 1, "real multi-page Text uses official ordered material slots");
    CheckPagePixels(OvCore::UI::ExtractFrame(scene, {320, 200}), multi.GetGUID(), multiPixels);
    auto& fallback = Rectangle(scene, canvasActor, "Fallback", 120, 20, 180, 70); AddText(fallback, "FontFixture.Fallback");
    auto fallbackPixels = Render(engine, scene, camera.GetCamera()); std::set<uint64_t> fonts;
    for (const auto& slot : fallback.GetComponent<CCanvasRenderer>()->GetMaterialSlots()) if (slot.fontPage) fonts.insert(slot.fontPage->generation);
    Require(fonts.size() == 2, "real CJK/astral fallback spans two font generations");
    CheckPagePixels(OvCore::UI::ExtractFrame(scene, {320, 200}), fallback.GetGUID(), fallbackPixels);
    auto& clip = Rectangle(scene, canvasActor, "Clip", 20, 20, 10, 80); AddText(clip, "FontFixture.Clip");
    label.SetParent(clip); label.GetComponent<CRectTransform>()->SetAnchoredPosition({0, 0});
    auto clipped = Render(engine, scene, camera.GetCamera()); Pixel(clipped, 25, 40, 240, 255); Pixel(clipped, 35, 40, 0, 5);
    label.SetParent(canvasActor); label.GetComponent<CRectTransform>()->SetAnchoredPosition({20, 20});
    clip.SetActive(false);
    auto& stencil = Rectangle(scene, canvasActor, "Stencil", 20, 20, 10, 80); AddText(stencil, "FontFixture.MaskImage"); AddText(stencil, "FontFixture.Stencil");
    label.SetParent(stencil); label.GetComponent<CRectTransform>()->SetAnchoredPosition({0, 0});
    auto masked = Render(engine, scene, camera.GetCamera()); Pixel(masked, 25, 40, 240, 255); Pixel(masked, 35, 40, 0, 5);
    label.SetParent(canvasActor); label.GetComponent<CRectTransform>()->SetAnchoredPosition({20, 20}); stencil.SetActive(false);
    auto oldPage = renderer->GetMaterialSlots()[0].fontPage;
    const auto originalFont = project / "outline-fixture.ttf", savedFont = project / "outline-saved.ttf";
    const auto oldStamp = std::filesystem::last_write_time(originalFont);
    std::filesystem::copy_file(originalFont, savedFont, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(project / "Roboto-Regular.ttf", originalFont, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::last_write_time(originalFont, oldStamp + std::chrono::seconds(2));
    Render(engine, scene, camera.GetCamera());
    Require(renderer->GetMaterialSlots()[0].fontPage->generation != oldPage->generation && oldPage->pixels.size() > 0,
        "font file hot replacement invalidates managed Text while preserving old page lease");
    std::filesystem::copy_file(savedFont, originalFont, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::last_write_time(originalFont, oldStamp + std::chrono::seconds(4));
    Render(engine, scene, camera.GetCamera());
    Require(scripts.LoadCSharpProject(host, project, assembly, "FontFixture", true), "Text project hot reload");
    auto reloaded = Render(engine, scene, camera.GetCamera()); Pixel(reloaded, 30, 40, 240, 255);
    Require(Field(script, "awakes") == 1, "hot reload preserves state without another Awake");
    // Record a real immutable frame, then destroy scene, project session and all
    // native font managers before the render consumer sees it.
    baregl::Framebuffer target; OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target, 320, 200, true, true, true);
    auto snapshot = target.RecordTarget(); auto frontend = std::make_unique<OvCore::Rendering::RenderFrontend>();
    auto inFlight = Record(*frontend, scene, camera.GetCamera(), snapshot, 320, 200);
    const auto labelGuid = label.GetGUID();
    Require(label.RemoveBehaviour(ScriptIdentity::CSharp("FontFixture", "FontFixture.Label")), "remove live Text component");
    auto afterRemoval = OvCore::UI::ExtractFrame(scene, {320, 200});
    Require(std::none_of(afterRemoval.meshes.begin(), afterRemoval.meshes.end(), [&](const auto& mesh) { return mesh.actorGuid == labelGuid; }),
        "Text removal clears renderer and weak registry before next extraction");
    engine.scenes.UnloadCurrentScene(); scripts.ClearCSharpProject(); frontend.reset();
    const auto collect = reinterpret_cast<OvCsCollectibleStatsFn>(host.Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost", L"CollectibleStats"));
    uint64_t alive = 0, collected = 0; host.Check(collect(1, &alive, &collected), "font session collection");
    Require(alive == 0 && collected >= 2, "Text/font registries release retired project sessions");
    std::vector<uint8_t> retained;
    if (threaded) {
        auto done = engine.render->Submit(std::move(inFlight)); engine.render->WaitIdle(); done.RethrowIfFailed();
        retained = engine.render->Invoke([&](auto&) { return Pixels(snapshot, 320, 200); });
        Require(glfwGetCurrentContext() == nullptr, "Main never acquired render context during font generation");
    } else { inFlight.Submit(*engine.driver); inFlight.Wait(); inFlight.GetCompletion().RethrowIfFailed(); retained = Pixels(snapshot, 320, 200); }
    Pixel(retained, 30, 40, 240, 255);
    host.Shutdown();
    Require(cameraCorrect, "Camera Canvas preserves off-center text screen orientation");
    std::puts("PASS production C# Text / SDF / canvas modes / mask / lifecycle"); return 0;
}
catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
