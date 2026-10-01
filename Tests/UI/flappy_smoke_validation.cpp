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
#include <baregl/detail/glad/glad.h>
#include <array>
#include <fstream>
#include <string>
#include <vector>

using namespace OvCore::ECS::Components;
using OvCore::Scripting::ScriptIdentity;

static constexpr uint16_t kWidth = 400, kHeight = 600;
static std::vector<uint8_t> g_pixels;

static Behaviour& Add(OvCore::ECS::Actor& actor, const char* name)
{ return actor.AddBehaviour(ScriptIdentity::CSharp("GameScripts", name)); }

static void WriteTga(const std::filesystem::path& path)
{
    std::ofstream tga(path, std::ios::binary);
    uint8_t header[18] = {};
    header[2] = 2;
    header[12] = uint8_t(kWidth & 255); header[13] = uint8_t(kWidth >> 8);
    header[14] = uint8_t(kHeight & 255); header[15] = uint8_t(kHeight >> 8);
    header[16] = 32; header[17] = 8;
    tga.write(reinterpret_cast<const char*>(header), 18);
    for (int y = kHeight - 1; y >= 0; y--) {
        for (int x = 0; x < kWidth; x++) {
            const auto* p = g_pixels.data() + (size_t(y) * kWidth + x) * 4;
            uint8_t bgra[4] = { p[2], p[1], p[0], p[3] };
            tga.write(reinterpret_cast<const char*>(bgra), 4);
        }
    }
}

static void CountColors(int& sky, int& ground, int& pipe, int& bird, int& other)
{
    sky = ground = pipe = bird = other = 0;
    for (size_t i = 0; i + 2 < g_pixels.size(); i += 4) {
        int r = g_pixels[i], g = g_pixels[i + 1], b = g_pixels[i + 2];
        if (r > 80 && r < 140 && g > 170 && g < 220 && b > 200) sky++;
        else if (r > 180 && g > 150 && g < 210 && b < 140) ground++;
        else if (g > 100 && g > r + 30 && g > b + 30) pipe++;
        else if (r > 200 && g > 160 && b < 120) bird++;
        else other++;
    }
}

static void RenderCapture(Engine& engine, OvCore::SceneSystem::Scene& scene, const std::filesystem::path& outPath)
{
    baregl::Framebuffer target;
    auto setup = [&](auto&) { OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target, kWidth, kHeight, true, true, true); };
    if (engine.render) engine.render->Invoke(setup); else setup(*engine.driver);
    auto output = target.RecordTarget();
    OvRendering::Entities::Camera camera;
    camera.SetClearColor({0.1f, 0.1f, 0.1f});
    OvRendering::Data::Material fallback;
    OvCore::Rendering::SceneRenderRequest main{{scene, camera, kWidth, kHeight, 1, 1}};
    OvCore::Rendering::SceneFrameRequest request{main, output, fallback};
    request.generateShadows = request.captureReflections = false;
    OvCore::Rendering::RenderFrontend frontend;
    auto commands = frontend.RecordFrame(frontend.PrepareFrame(frontend.ExtractFrame(request), {}));
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
    WriteTga(outPath);
    std::ofstream rgba(outPath.string() + ".rgba", std::ios::binary);
    rgba.write(reinterpret_cast<const char*>(g_pixels.data()), std::streamsize(g_pixels.size()));
}

int wmain(int argc, wchar_t** argv) try
{
    Require(argc == 5, "flappy arguments: runtime, host, GameScripts.dll, out.tga");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    OvReflection::MainThread authority;
    RuntimeHost host(authority);
    host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(), argv[1], argv[2]));
    std::filesystem::path assembly = argv[3], project = assembly.parent_path();
    std::filesystem::path outPath = argv[4];
    OvCore::ResourceManagement::FontManager::ProvideAssetPaths(
        project, std::filesystem::current_path() / "Resources" / "Engine");
    OvCore::Scripting::ScriptEngine scripts(project, project);
    OvCore::Global::ServiceLocator::Provide(scripts);
    Engine engine(false);
    OvCore::Global::ServiceLocator::Provide(engine.scenes);
    Require(scripts.LoadCSharpProject(host, project, assembly, "GameScripts"), "load GameScripts");

    auto& scene = *engine.scenes.GetCurrentScene();
    auto& canvas = scene.CreateActor("Canvas");
    canvas.AddComponent<CCanvas>();
    auto& rect = canvas.AddComponent<CRectTransform>();
    rect.SetAnchorMin({0, 0});
    rect.SetAnchorMax({1, 1});
    rect.SetPivot({0.5f, 0.5f});
    rect.SetAnchoredPosition({0, 0});
    rect.SetSizeDelta({0, 0});
    auto& game = Add(canvas, "Flappy.FlappyBirdGame");
    OvCore::UI::ExtractFrame(scene, {float(kWidth), float(kHeight)});
    Require(scripts.PrepareCSharp(game), "prepare FlappyBirdGame");
    Require(game.GetScript() && game.GetScript()->IsValid(), "FlappyBirdGame prepared");

    auto* renderer = canvas.GetComponent<CCanvasRenderer>();
    Require(renderer != nullptr, "CanvasRenderer exists after UI rebuild");
    std::printf("edit-mode vertices=%zu indices=%zu\n",
        renderer->GetVertices().size(), renderer->GetIndices().size());
    Require(renderer->GetVertices().size() >= 4, "graphic mesh has vertices");
    Require(renderer->GetVertices().size() >= 20, "graphic mesh has several quads (not one box)");
    for (size_t i = 0; i < renderer->GetVertices().size(); i++) {
        const auto& v = renderer->GetVertices()[i];
        if (i < 8) std::printf("  v[%zu]=(%.1f,%.1f) col=(%.2f,%.2f,%.2f,%.2f)\n",
            i, v.position.x, v.position.y, v.color.x, v.color.y, v.color.z, v.color.w);
    }

    RenderCapture(engine, scene, outPath);
    int sky, ground, pipe, bird, other;
    CountColors(sky, ground, pipe, bird, other);
    std::printf("colors sky=%d ground=%d pipe=%d bird=%d other=%d\n", sky, ground, pipe, bird, other);
    Require(sky > 1000, "sky pixels present");
    Require(ground > 500, "ground pixels present");
    Require(pipe > 200, "pipe pixels present");
    Require(bird > 50, "bird pixels present");

    scene.Play();
    for (int i = 0; i < 30; i++) scene.Update(1.0f / 60.0f);
    RenderCapture(engine, scene, outPath.string() + ".play.tga");
    CountColors(sky, ground, pipe, bird, other);
    std::printf("play colors sky=%d ground=%d pipe=%d bird=%d other=%d\n", sky, ground, pipe, bird, other);
    Require(sky > 1000 && ground > 500 && pipe > 200 && bird > 50, "play frame still draws the board");

    scripts.ClearCSharpProject();
    engine.scenes.UnloadCurrentScene();
    host.Shutdown();
    std::puts("PASS FlappyBird render capture");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "FlappyBird capture failed: %s\n", error.what());
    return 1;
}
