#define main SerialValidationMain
#include "serial_validation.cpp"
#undef main
#include <sstream>
#include <tinyxml2.h>
#include <OvEditor/Core/Editor.h>
#include <OvEditor/Panels/AssetView.h>
#include <OvEditor/Panels/GameView.h>
#include <OvEditor/Panels/SceneView.h>
#include <OvCore/SceneSystem/SceneManager.h>

std::string SceneState(Scene& scene)
{
    std::ostringstream out(std::ios::binary);
    for (auto* actor : scene.GetActors()) if (auto skin = actor->GetComponent<Skin>()) Snapshot(out, *skin);
    return out.str();
}

void Save(Scene& scene, const std::filesystem::path& path)
{
    tinyxml2::XMLDocument doc; auto* node = doc.NewElement("root"); doc.InsertFirstChild(node);
    scene.OnSerialize(doc, node); Require(doc.SaveFile(path.string().c_str()) == tinyxml2::XML_SUCCESS, "save project scene");
}

std::vector<unsigned char> ViewPixels(OvEditor::Panels::AView& view)
{
#ifdef EDITOR_COMMANDS_TEST
    view.m_renderer->SetDrawRecordingEnabled(false);
    view.Render();
    const auto [referenceWidth,referenceHeight]=view.GetSafeSize();
    std::vector<uint8_t> reference(size_t(referenceWidth)*referenceHeight*4);
    view.m_framebuffer.RecordTarget().ReadRGBA8(reference);
    auto commands=view.RecordRenderCommands();
    commands.Execute(*EDITOR_CONTEXT(driver));
    view.CompleteRender();
#else
    view.Render();
#endif
    Require(view.m_image->textureID.id == view.m_framebuffer.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR)->GetID(), "view image retained a stale attachment after resize");
    const auto [width, height] = view.GetSafeSize();
    Require(width > 0 && height > 0, "view has no render target");
    std::vector<unsigned char> pixels(size_t(width) * height * 4);
    view.m_framebuffer.Bind();
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    view.m_framebuffer.Unbind();
#ifdef EDITOR_COMMANDS_TEST
    Require(pixels==reference,"real view immediate/delayed command images differ");
#endif
    return pixels;
}

#ifdef EDITOR_COMMANDS_TEST
#include "../RenderingCommands/editor_command_validation.inl"
#endif

#ifdef EDITOR_UI_TEST
#include "../RenderingCommands/editor_ui_validation.inl"
#endif

int main(int argc, char** argv) try
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    Require(argc > 1, "pass output directory");
    const auto project = std::filesystem::path(argv[1]) / "SkinningProject";
    std::filesystem::create_directories(project / "Assets");
    std::ofstream(project / "SkinningProject.ovproject") << "gravity=-9.81\nx_resolution=640\ny_resolution=480\nfullscreen=false\nexecutable_name=Game\nstart_scene=crowd.ovscene\nvsync=false\nmultisampling=false\nsamples=1\nbuild_type=0\nwindow_icon=\n";
    std::ofstream(project / "Assets/observer.lua") << "local t = {}\nfunction t:OnUpdate(dt) end\nreturn t\n";
#ifdef EDITOR_UI_TEST
    OvRendering::Utils::ResourceTracking::TrackResources(); // Match the real Main.cpp startup.
#endif
    OvEditor::Core::Context context(project);
#ifdef EDITOR_UI_TEST
    struct LogSubscriptionGuard
    {
        uint64_t before = OvDebug::Logger::LogEvent.GetListenerCount();
        ~LogSubscriptionGuard()
        {
            if (OvDebug::Logger::LogEvent.GetListenerCount() != before)
            { std::fprintf(stderr,"FAIL: Editor left a global log subscription after destruction\n"); std::abort(); }
            std::puts("PASS: Editor destruction removes global Console log subscription");
        }
    } logSubscriptionGuard;
#endif
    OvEditor::Core::Editor editor(context);
    auto& actions = editor.m_editorActions;
    auto& game = editor.m_panelsManager.GetPanelAs<OvEditor::Panels::GameView>("Game View");
    auto& view = editor.m_panelsManager.GetPanelAs<OvEditor::Panels::SceneView>("Scene View");
    auto& asset = editor.m_panelsManager.GetPanelAs<OvEditor::Panels::AssetView>("Asset View");
    for (auto* panel : std::array<OvEditor::Panels::AView*, 3>{&game, &view, &asset}) { panel->Open(); panel->SetSize({320, 260}); }
#ifdef DRAW_PACKETS_TEST
    for (auto* panel : std::array<OvEditor::Panels::AView*, 3>{&game, &view, &asset}) panel->m_renderer->SetDrawRecordingEnabled(true);
#endif
    context.uiManager->Render(); // Initialize ImGui font/frame state for the real view renderers.
    auto* model = context.modelManager.GetResource(":Models/Mannequin.fbx");
    auto* material = context.materialManager.GetResource(":Materials/Default.ovmat");
    Require(model && material, "project engine assets");
    asset.SetResource(OvEditor::Panels::AssetView::ViewableResource{model});
#ifdef EDITOR_COMMANDS_TEST
    EditorCommandValidation(context, model, material, argv[1]);
    // Fix temporal inputs for exact immediate/delayed Asset View comparisons.
    for(auto* stack:asset.GetScene()->GetFastAccessComponents().postProcessStacks)
    {auto settings=stack->GetAutoExposureSettings();settings.progressive=false;stack->SetAutoExposureSettings(settings);}
    for(auto* probe:asset.GetScene()->GetFastAccessComponents().reflectionProbes)
        probe->SetCaptureSpeed(OvCore::ECS::Components::CReflectionProbe::ECaptureSpeed::SIX_FACES);
#endif
    const auto bounds = model->GetBoundingSphere();
    const auto cameraPosition = bounds.position + FVector3{0, bounds.radius * 7.0f, std::max(bounds.radius * 13.0f, 2.0f)};
    const auto cameraRotation = FQuaternion(FVector3{28, 180, 0});
    for (const std::string name : {"crowd", "fragmented", "rate_limited"})
    {
        context.sceneManager.LoadEmptyScene(); auto& scene = *context.sceneManager.GetCurrentScene();
        auto& cameraActor = scene.CreateActor("Camera"); cameraActor.AddComponent<CCamera>();
        cameraActor.transform.SetLocalPosition(cameraPosition); cameraActor.transform.SetLocalRotation(cameraRotation);
        scene.AddDefaultLights();
        for (size_t i = 0; i < 64; ++i)
        {
            auto& skin = Add(scene, model); skin.owner.SetName("Mannequin " + std::to_string(i));
            skin.SetAnimation(std::optional<uint32_t>{0}); skin.SetTime(float(i % 17) * 0.03f);
            skin.m_poseEvaluationRate = name == "rate_limited" ? 15.0f : 0.0f;
            skin.owner.AddComponent<CMaterialRenderer>().FillWithMaterial(*material);
            skin.owner.transform.SetLocalPosition({(float(i % 8) - 3.5f) * bounds.radius * 1.6f, 0, (float(i / 8) - 3.5f) * bounds.radius * 1.6f});
            if (name == "fragmented" && i % 4 == 3) skin.owner.AddBehaviour("observer.lua");
        }
        Save(scene, project / "Assets" / (name + ".ovscene"));
    }
    std::ofstream metrics(std::filesystem::path(argv[1]) / "project-batches.csv");
    metrics << "scene,updates,candidate_instances,evaluated_instances,parallel_instances,parallel_batches,submitted_jobs\n";
    for (const std::string name : {"crowd", "fragmented", "rate_limited"})
    {
        Require(context.sceneManager.LoadScene(name + ".ovscene"), "load saved validation scene");
        auto& scene = *context.sceneManager.GetCurrentScene(); scene.Play();
        size_t candidates = 0, evaluated = 0, parallel = 0, batches = 0, jobs = 0;
        for (int frame = 0; frame < 96; ++frame)
        {
            OvCore::SceneSystem::SkinningUpdateStats stats;
            scene.Update(1.0f / 60, *context.jobSystem, &stats);
            candidates += stats.candidateActors; evaluated += stats.evaluatedInstances;
            parallel += stats.parallelInstances; batches += stats.parallelBatches; jobs += stats.submittedJobs;
        }
        Require(name == "fragmented" ? parallel == 0 : parallel > 0, "unexpected saved-project batch hit rate");
        if (name == "rate_limited") Require(evaluated < candidates / 2, "rate-limited scene did not skip evaluations");
        metrics << name << ",96," << candidates << ',' << evaluated << ',' << parallel << ',' << batches << ',' << jobs << '\n';
        std::printf("PASS: project %s candidates=%zu evaluations=%zu parallel=%zu batches=%zu jobs=%zu\n", name.c_str(), candidates, evaluated, parallel, batches, jobs);
    }
    // Run the real Editor/EditorActions mode implementation against a serial Scene.
    Require(context.sceneManager.LoadScene("crowd.ovscene"), "load interaction scene");
    tinyxml2::XMLDocument original; Require(original.LoadFile((project / "Assets/crowd.ovscene").string().c_str()) == tinyxml2::XML_SUCCESS, "read serial scene");
    Scene serial(project / "Assets", context.engineAssetsPath);
    serial.OnDeserialize(original, original.FirstChildElement("root")->FirstChildElement("scene"));
    auto current = [&]() -> Scene& { return *context.sceneManager.GetCurrentScene(); };
    Require(SceneState(current()) == SceneState(serial), "loaded scene state mismatch");
    const auto initial = SceneState(current());
    editor.UpdateCurrentEditorMode(0.02f); Require(SceneState(current()) == initial, "Edit advanced skinning");
    actions.StartPlaying(); serial.Play();
    Require(actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::PLAY, "Editor did not start playing");
    auto tick = [&]() { editor.UpdateCurrentEditorMode(0.02f); serial.Update(0.02f); serial.LateUpdate(0.02f); Require(SceneState(current()) == SceneState(serial), "Editor vs serial state mismatch"); };
    for (int i = 0; i < 12; ++i) tick();
    actions.PauseGame(); const auto paused = SceneState(current());
    for (int i = 0; i < 8; ++i) editor.UpdateCurrentEditorMode(0.02f);
    Require(SceneState(current()) == paused, "Pause advanced skinning");
    for (int i = 0; i < 8; ++i)
    {
        actions.NextFrame(); tick();
        Require(actions.GetCurrentEditorMode() == OvEditor::Core::EditorActions::EEditorMode::PAUSE, "single step did not pause");
        auto state = SceneState(current()); editor.UpdateCurrentEditorMode(0.02f); Require(SceneState(current()) == state, "single step ran twice");
    }
    actions.StartPlaying(); for (int i = 0; i < 12; ++i) tick(); actions.PauseGame();
    view.GetCamera()->SetPosition(cameraPosition); view.GetCamera()->SetRotation(cameraRotation);
    // Independent Scene/Game/Asset renderers may consume a pose repeatedly.
    size_t imagePairs = 0;
    std::vector<unsigned char> previousGame; size_t transitions = 0;
    for (int frame = 0; frame < 12; ++frame)
    {
        actions.NextFrame(); tick(); const auto state = SceneState(current());
        auto gameA = ViewPixels(game); auto sceneA = ViewPixels(view); ViewPixels(asset);
        if (frame == 0)
        {
            std::ofstream image(std::filesystem::path(argv[1]) / "editor-game.rgba", std::ios::binary);
            image.write(reinterpret_cast<const char*>(gameA.data()), static_cast<std::streamsize>(gameA.size()));
            const auto [width, height] = game.GetSafeSize();
            std::ofstream(std::filesystem::path(argv[1]) / "editor-game-size.txt") << width << ' ' << height;
        }
        auto sceneB = ViewPixels(view); auto gameB = ViewPixels(game);
        Require(gameA == gameB && sceneA == sceneB, "view order polluted shared skinning state");
        Require(SceneState(current()) == state, "view consumption advanced/replaced a pose");
        size_t nonBackground = 0; for (size_t i = 4; i < gameA.size(); i += 4) if (std::memcmp(gameA.data(), gameA.data() + i, 3)) ++nonBackground;
        Require(nonBackground > 100, "Game view rendered no geometry");
        if (!previousGame.empty() && previousGame != gameA) ++transitions;
        previousGame = std::move(gameA); imagePairs += 2; TracyGpuCollect;
    }
    Require(transitions > 0, "Editor view animation did not move");
    actions.StopPlaying(); Require(SceneState(current()) == initial, "Stop did not restore saved scene pose");
    for (int cycle = 0; cycle < 8; ++cycle)
    {
        actions.StartPlaying(); editor.UpdateCurrentEditorMode(0.02f);
        context.sceneManager.LoadAndPlayDelayed(cycle % 2 ? "crowd.ovscene" : "fragmented.ovscene");
        editor.UpdateCurrentEditorMode(0.02f); editor.UpdateCurrentEditorMode(0.02f);
        game.SetSize({float(256 + cycle * 8), 240}); ViewPixels(game); ViewPixels(view);
        actions.StopPlaying(); Require(SceneState(current()) == initial, "scene switch/stop did not restore original");
    }
    std::printf("PASS: real Editor Edit/Play/Pause/8 steps/resume/Stop, %zu exact view-order image pairs, %zu moving transitions, 8 delayed scene switches/resizes\n", imagePairs, transitions);
#ifdef EDITOR_UI_TEST
    EditorUIValidation(editor, context, argv[1]);
#endif
    std::printf("PASS: saved project %s\n", project.string().c_str());
    return 0;
}
catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
