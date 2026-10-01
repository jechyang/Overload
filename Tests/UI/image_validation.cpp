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
#include <array>
#include <cmath>
#include <fstream>
#include <vector>

using namespace OvCore::ECS::Components;
using OvCore::UI::Frame;
using OvCore::UI::Rect;
using OvCore::Scripting::ScriptIdentity;

static constexpr uint16_t kWidth = 320, kHeight = 200;
static std::vector<uint8_t> g_pixels;

static bool Near(float a, float b, float eps = 0.05f) { return std::abs(a - b) <= eps; }

static Behaviour& Add(OvCore::ECS::Actor& actor, const char* name)
{ return actor.AddBehaviour(ScriptIdentity::CSharp("ImageFixture", name)); }

static void Place(OvCore::ECS::Actor& actor, float x, float y, float w, float h)
{
    auto* rect = actor.GetComponent<CRectTransform>();
    rect->SetAnchorMin({0, 0});
    rect->SetAnchorMax({0, 0});
    rect->SetPivot({0.5f, 0.5f});
    rect->SetAnchoredPosition({x, y});
    rect->SetSizeDelta({w, h});
}

static void ExpectColor(int x, int y, int r, int g, int b, const char* what, int tol = 10)
{
    const size_t index = (size_t(y) * kWidth + x) * 4;
    if (index + 2 >= g_pixels.size()) throw std::runtime_error("pixel out of range");
    auto ch = [&](int actual, int expected) {
        if (std::abs(actual - expected) > tol) {
            std::fprintf(stderr, "%s: (%d,%d)=(%d,%d,%d) expected %d,%d,%d\n",
                what, x, y, g_pixels[index], g_pixels[index + 1], g_pixels[index + 2], r, g, b);
            throw std::runtime_error(what);
        }
    };
    ch(g_pixels[index], r); ch(g_pixels[index + 1], g); ch(g_pixels[index + 2], b);
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
    auto extracted = frontend.ExtractFrame(request);
    auto prepared = frontend.PrepareFrame(std::move(extracted), {});
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

static const OvCore::UI::Mesh* MeshOf(const Frame& frame, uint64_t guid)
{
    for (const auto& mesh : frame.meshes) if (mesh.actorGuid == guid) return &mesh;
    return nullptr;
}

static void CheckGeometry(OvCore::SceneSystem::Scene& scene)
{
    auto frame = OvCore::UI::ExtractFrame(scene, {kWidth, kHeight});
    auto* simple = scene.FindActorByName("SimpleFill");
    auto* sliced = scene.FindActorByName("SlicedFill");
    auto* slicedNc = scene.FindActorByName("SlicedNoCenter");
    auto* tiled = scene.FindActorByName("TiledFill");
    auto* fillH = scene.FindActorByName("FillH");
    auto* fillH1 = scene.FindActorByName("FillHOrigin");
    auto* fillV = scene.FindActorByName("FillV");
    auto* empty = scene.FindActorByName("FillEmpty");
    auto* r90 = scene.FindActorByName("FillRadial90");
    auto* r360 = scene.FindActorByName("FillRadial360");
    Require(simple && sliced && slicedNc && tiled && fillH && fillH1 && fillV && empty && r90 && r360, "image fixture actors");

    auto* mSimple = MeshOf(frame, simple->GetGUID());
    Require(mSimple && mSimple->vertices.size() == 4 && mSimple->indices.size() == 6, "simple quad");
    Require(Near(mSimple->vertices[0].uv.x, 0) && Near(mSimple->vertices[2].uv.x, 1), "simple full outer UV");

    auto* mSliced = MeshOf(frame, sliced->GetGUID());
    Require(mSliced && mSliced->vertices.size() == 36, "sliced 9 quads with center");
    auto* mSlicedNc = MeshOf(frame, slicedNc->GetGUID());
    Require(mSlicedNc && mSlicedNc->vertices.size() == 32, "sliced 8 quads without center");

    // Corner UV of sliced image stays at outer/inner corners (no bleed into center).
    Require(Near(mSliced->vertices[0].uv.x, 0) && Near(mSliced->vertices[0].uv.y, 0), "sliced outer uv");

    auto* mTiled = MeshOf(frame, tiled->GetGUID());
    Require(mTiled && mTiled->vertices.size() >= 16 && (mTiled->vertices.size() % 4) == 0, "tiled multi-quad");
    Require(Near(mTiled->vertices[0].uv.x, 0) && Near(mTiled->vertices[2].uv.x, 1), "tiled tile UV span");

    auto* mH = MeshOf(frame, fillH->GetGUID());
    Require(mH && mH->vertices.size() == 4, "fill horizontal quad");
    float minX = mH->vertices[0].position.x, maxX = mH->vertices[2].position.x;
    Require(maxX - minX > 1 && maxX - minX < 45, "fill horizontal 0.5 shrinks width");

    auto* mH1 = MeshOf(frame, fillH1->GetGUID());
    Require(mH1 && mH1->vertices.size() == 4, "fill origin 1 quad");
    // Origin 1 keeps the right edge and shrinks from the left.
    Require(mH1->vertices[2].position.x > mH1->vertices[0].position.x, "fill origin 1 still has width");

    auto* mV = MeshOf(frame, fillV->GetGUID());
    Require(mV && mV->vertices.size() == 4, "fill vertical quad");
    float minY = mV->vertices[0].position.y, maxY = mV->vertices[2].position.y;
    Require(maxY - minY > 1 && maxY - minY < 45, "fill vertical 0.5 shrinks height");

    Require(MeshOf(frame, empty->GetGUID()) == nullptr || MeshOf(frame, empty->GetGUID())->vertices.empty(),
        "fill amount 0 produces no geometry");

    auto* m90 = MeshOf(frame, r90->GetGUID());
    Require(m90 && m90->vertices.size() == 4, "radial90 emits a cut quad");
    auto* m360 = MeshOf(frame, r360->GetGUID());
    Require(m360 && m360->vertices.size() >= 4, "radial360 emits cut quads");
}

static void CheckPixels()
{
    // SimpleFill is solid red at bottom-left sample point.
    ExpectColor(20, 20, 255, 0, 0, "simple red");
    // SlicedFill is solid blue (tint) at its center.
    ExpectColor(100, 20, 0, 0, 255, "sliced blue");
    // FillH at 0.5 covers only the left half of its rect; sample both sides.
    // Rect is centered at (160, 160) size 64x40 → left 128..160, right 160..192.
    ExpectColor(140, 160, 0, 255, 255, "fill h left painted");
    ExpectColor(180, 160, 0, 0, 0, "fill h right empty", 12);
}

int wmain(int argc, wchar_t** argv) try
{
    Require(argc == 5, "image arguments: runtime, host, ImageFixture.dll, default/render-thread");
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
    Require(scripts.LoadCSharpProject(host, project, assembly, "ImageFixture"), "load ImageFixture");
    auto& scene = *engine.scenes.GetCurrentScene();
    auto& canvas = scene.CreateActor("Canvas");
    canvas.AddComponent<CCanvas>();

    struct Spec { const char* name; const char* type; float x, y, w, h; };
    const Spec specs[] = {
        {"SimpleFill", "ImageFixture.SimpleFill", 40, 20, 40, 40},
        {"SimpleAspect", "ImageFixture.SimpleAspect", 40, 80, 80, 80},
        {"SlicedFill", "ImageFixture.SlicedFill", 140, 20, 80, 60},
        {"SlicedNoCenter", "ImageFixture.SlicedNoCenter", 140, 100, 80, 60},
        {"TiledFill", "ImageFixture.TiledFill", 260, 20, 80, 60},
        {"FillH", "ImageFixture.FillH", 160, 160, 64, 40},
        {"FillHOrigin", "ImageFixture.FillHOrigin", 160, 120, 64, 30},
        {"FillV", "ImageFixture.FillV", 240, 160, 40, 64},
        {"FillEmpty", "ImageFixture.FillEmpty", 280, 160, 40, 40},
        {"FillRadial90", "ImageFixture.FillRadial90", 60, 160, 50, 50},
        {"FillRadial360", "ImageFixture.FillRadial360", 100, 160, 50, 50},
    };
    for (const auto& s : specs) {
        auto& actor = scene.CreateActor(s.name);
        actor.SetParent(canvas);
        actor.AddComponent<CRectTransform>();
        Add(actor, s.type);
        Place(actor, s.x, s.y, s.w, s.h);
    }
    auto first = OvCore::UI::ExtractFrame(scene, {kWidth, kHeight});
    Require(!first.meshes.empty(), "first frame builds image meshes");
    CheckGeometry(scene);
    Render(engine, scene);
    CheckPixels();

    const auto out = project / (std::wstring(argv[4]) + L".rgba");
    std::ofstream capture(out, std::ios::binary);
    capture.write(reinterpret_cast<const char*>(g_pixels.data()), std::streamsize(g_pixels.size()));
    capture.close();
    scripts.ClearCSharpProject();
    engine.scenes.UnloadCurrentScene();
    host.Shutdown();
    std::puts("PASS U3 Image modes geometry and pixels");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "U3 image validation failed: %s\n", error.what());
    return 1;
}
