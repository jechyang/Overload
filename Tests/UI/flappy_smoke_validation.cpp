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
#include <tinyxml2.h>
#include <OvWindowing/Inputs/InputManager.h>
#include <baregl/ResourceExecutor.h>
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
    header[16] = 32; header[17] = 0x28;
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

static int Score(Behaviour& game, const char* name = "m_Score")
{
    for (const auto& field : game.GetScript()->ManagedFields())
        if (field.name == name) return game.GetScript()->ReadManagedField(field.token)->scalar.int32_value;
    throw std::runtime_error("missing score field");
}
static float BirdY(const CCanvasRenderer& renderer)
{
    for (const auto& v : renderer.GetVertices())
        if (v.color.x > .99f && v.color.y > .8f && v.color.y < .9f && v.color.z < .25f) return v.position.y;
    throw std::runtime_error("bird geometry missing");
}
static bool Dead(const CCanvasRenderer& renderer)
{
    const auto& color = renderer.GetVertices().back().color;
    return color.w > .9f && color.w < .96f;
}
static std::pair<float, float> NextPipe(const CCanvasRenderer& renderer)
{
    float x = 10000, gap = 0;
    for (const auto& v : renderer.GetVertices())
        if (v.color.x > .20f && v.color.x < .24f && v.color.y > .70f && v.color.y < .74f && v.position.y < 299)
        {
            // The first vertex of each top pipe is its bottom-left corner.
            float center = v.position.x + 32;
            if (center >= -148 && center < x) { x = center; gap = v.position.y - 85; }
        }
    return {x, gap};
}

int wmain(int argc, wchar_t** argv) try
{
    Require(argc == 6, "flappy arguments: runtime, host, GameScripts.dll, out.tga");
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
    Engine engine(std::wstring(argv[5]) == L"render-thread");
    OvWindowing::Inputs::InputManager input(engine.window);
    OvCore::Global::ServiceLocator::Provide(input);
    input.SetUIViewport({{0,0},{kWidth,kHeight},{1,1}});
    std::unique_ptr<baregl::ResourceExecutor> executor;
    if (engine.render) executor = std::make_unique<baregl::ResourceExecutor>([&](std::function<void()> fn) { engine.render->Invoke([&](auto&) { fn(); }); });
    OvCore::Global::ServiceLocator::Provide(engine.scenes);
    Require(scripts.LoadCSharpProject(host, project, assembly, "GameScripts"), "load GameScripts");

    auto& scene = *engine.scenes.GetCurrentScene();
    tinyxml2::XMLDocument doc;
    Require(doc.LoadFile("Examples/FlappyBird/Assets/Scenes/FlappyBird.ovscene") == tinyxml2::XML_SUCCESS, "load shipped FlappyBird scene");
    scene.OnDeserialize(doc, doc.RootElement()->FirstChildElement("scene"));
    OvCore::UI::ExtractFrame(scene, {float(kWidth), float(kHeight)});
    auto& canvas = *scene.FindActorByName("Canvas");
    auto& game = canvas.GetBehaviours().at(ScriptIdentity::CSharp("GameScripts", "Flappy.FlappyBirdGame").Key());
    Require(scene.FindActorByName("EventSystem") == nullptr, "edit-mode preview does not create gameplay actors");

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
    for (int i = 0; i < 3; ++i) { scene.Update(1.f/60); scene.LateUpdate(1.f/60); input.ClearEvents(); }
    auto* events = scene.FindActorByName("EventSystem");
    Require(events != nullptr, "sample creates EventSystem");
    for (auto& [key, behaviour] : events->GetBehaviours())
        Require(behaviour.GetScript() && behaviour.GetScript()->IsValid(), "built-in input behaviour is registered and live");

    using namespace OvWindowing::Inputs;
    auto push = [&](UIInputKind kind, int key) { engine.window.UIInputEvent.Invoke({kind,key,{100,300}}); };
    auto tick = [&](float dt = 1.f/60) {
        scene.Update(dt); scene.LateUpdate(dt); input.ClearEvents();
        OvCore::UI::ExtractFrame(scene, {float(kWidth),float(kHeight)});
        if (!game.GetScript()->IsValid()) throw std::runtime_error("gameplay faulted");
    };
    auto flap = [&](bool mouse = false) {
        push(mouse ? UIInputKind::PointerDown : UIInputKind::KeyDown, mouse ? 0 : 32);
        push(mouse ? UIInputKind::PointerUp : UIInputKind::KeyUp, mouse ? 0 : 32);
        tick();
    };
    push(UIInputKind::Focus,1); tick();
    const float readyPipe = NextPipe(*renderer).first;
    flap(); tick();
    Require(NextPipe(*renderer).first < readyPipe && Score(game) == 0 && !Dead(*renderer), "Space starts a safe round, without awarding a phantom point");
    float before = BirdY(*renderer);
    flap(true);
    Require(BirdY(*renderer) > before, "same-frame mouse click flaps through native InputManager");
    before = BirdY(*renderer);
    tick(0);
    Require(std::abs(BirdY(*renderer) - before) < .001f, "zero delta does not advance gameplay");
    push(UIInputKind::Focus,0); tick(); flap();
    Require(std::abs(BirdY(*renderer) - before) < .001f, "unfocused game pauses and ignores gameplay input");
    push(UIInputKind::Focus,1); tick();
    float previousY = BirdY(*renderer);
    for (int frame = 0; frame < 1600 && Score(game) < 6; ++frame)
    {
        float y = BirdY(*renderer), target = NextPipe(*renderer).second;
        bool press = y < target - 20 && y <= previousY;
        previousY = y;
        if (press) {
            push(UIInputKind::KeyDown,32); push(UIInputKind::KeyUp,32);
        }
        tick();
        if (Dead(*renderer)) throw std::runtime_error("autopilot collided before six pipes");
    }
    Require(Score(game) >= 6, "six real pipe crossings including pool recycling score successfully");
    RenderCapture(engine, scene, outPath.string() + ".play.tga");
    for (int i = 0; i < 240 && !Dead(*renderer); ++i) tick();
    Require(Dead(*renderer) && Score(game,"m_Best") >= 6, "no input leads to collision and records best score");
    for (int i = 0; i < 120; ++i) tick();
    RenderCapture(engine, scene, outPath.string() + ".dead.tga");
    const int best = Score(game,"m_Best");
    flap(true);
    Require(!Dead(*renderer) && Score(game) == 0 && Score(game,"m_Best") == best, "mouse reset clears score and retains best");
    flap();
    Require(!Dead(*renderer), "second round starts with Space");
    // Portrait, landscape and HiDPI viewports keep a complete 400x600 board.
    for (const auto viewport : {OvMaths::FVector2{960,540}, OvMaths::FVector2{420,640}, OvMaths::FVector2{800,1200}})
    {
        OvCore::UI::ExtractFrame(scene,viewport);
        auto rect = canvas.GetComponent<CRectTransform>()->GetRect();
        Require(rect.size.x >= 399.9f && rect.size.y >= 599.9f, "viewport resize preserves the logical playfield");
    }
    Require(scripts.LoadCSharpProject(host, project, assembly, "GameScripts", true), "reload the actual FlappyBird project");
    tick(); flap(); tick();
    Require(!Dead(*renderer) && Score(game) == 0, "hot reload restores a playable round");
    int eventSystems = 0;
    for (auto* actor : scene.GetActors()) if (actor->GetName() == "EventSystem") ++eventSystems;
    Require(eventSystems == 1, "reload does not duplicate EventSystem actors");

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
