#define main SerialValidationMain
#include "serial_validation.cpp"
#undef main
#include <atomic>
#include <mutex>
#include <sstream>
#include <sol/sol.hpp>

thread_local int submissionFailureCountdown = -1;
void* operator new(std::size_t size, std::align_val_t alignment)
{
    if (submissionFailureCountdown >= 0 && submissionFailureCountdown-- == 0) throw std::bad_alloc();
    if (auto* p = _aligned_malloc(size, static_cast<size_t>(alignment))) return p;
    throw std::bad_alloc();
}
void operator delete(void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }

using Jobs = OvTools::Jobs::JobSystem;
std::string State(const Skin& skin) { std::ostringstream stream(std::ios::binary); Snapshot(stream, skin); return stream.str(); }
void Grow(Model& model)
{
    auto& skeleton = *model.m_skeleton;
    while (skeleton.nodes.size() < 64)
    {
        const auto index = static_cast<uint32_t>(skeleton.nodes.size());
        auto node = skeleton.nodes[skeleton.nodeByName.at("node" + std::to_string(index % 3))]; node.name = "node" + std::to_string(index);
        node.parentIndex = static_cast<int32_t>(skeleton.nodeByName.at("node" + std::to_string((index - 1) / 2))); node.boneIndex = static_cast<int32_t>(index);
        skeleton.nodeByName.emplace(node.name, index); skeleton.boneByName.emplace(node.name, index);
        skeleton.nodes.push_back(node); skeleton.bones.push_back({node.name, index, FMatrix4::Identity});
    }
}
Skin& Animated(Scene& scene, OvRendering::Resources::Model& model, size_t index = 0)
{
    auto& skin = Add(scene, &model); skin.SetAnimation(std::optional<uint32_t>{0});
    skin.SetTime(float(index % 17) * 0.03f); skin.m_poseEvaluationRate = 0;
    return skin;
}

void StatisticsValidation(Jobs& jobs)
{
    Model model("stats"); Populate(model); Grow(model);
    for (size_t count : {0u, 1u, 4u, 8u, 16u})
    {
        Scene scene; for (size_t i = 0; i < count; ++i) Animated(scene, model, i);
        OvCore::SceneSystem::SkinningUpdateStats stats;
        scene.Update(0.02f, jobs, &stats);
        Require(stats.snapshotActors == count && stats.candidateActors == count, "candidate counters");
        Require(stats.evaluatedInstances == count && stats.evaluatedNodes == count * 64, "evaluation counters");
        Require(stats.parallelInstances == (count >= 8 ? count : 0), "parallel counters");
        Require(stats.batches == (count ? 1u : 0u), "batch counters");
        Require(stats.parallelBatches == (count >= 8 ? 1u : 0u), "parallel batch counters");
        Require(stats.submittedJobs == (count >= 8 ? std::min(count, size_t(jobs.GetWorkerCount()) * 2 + 1) - 1 : 0), "submission counters");
        for (auto* actor : scene.GetActors()) actor->GetComponent<Skin>()->Pause();
        scene.Update(0.02f, jobs, &stats);
        Require(stats.evaluatedInstances == 0 && stats.evaluatedNodes == 0 && stats.parallelInstances == 0 && stats.submittedJobs == 0, "stale paused counters");
    }
    std::puts("PASS: optional per-update batch statistics, thresholds and paused reset");
}

void PassiveWaitValidation(Jobs& jobs)
{
    bool mainRan = false;
    auto main = jobs.Submit([&] { mainRan = true; }, {}, OvTools::Jobs::EAffinity::MainThread);
    std::atomic<bool> ran = false;
    auto work = jobs.Submit([&] { ran.store(true); });
    std::array handles{work}; jobs.WaitAllPassive(handles);
    Require(ran && !mainRan && !main.IsCompleted(), "passive wait ran queued main work");
    bool wrongAffinity = false;
    try { std::array h{main}; jobs.WaitAllPassive(h); } catch (const std::invalid_argument&) { wrongAffinity = true; }
    Require(wrongAffinity, "passive wait accepted main affinity");
    jobs.Wait(main); Require(mainRan, "normal wait stopped pumping");
    Jobs foreign(1); auto other = foreign.Submit([] {});
    bool wrongOwner = false;
    try { std::array h{other}; jobs.WaitAllPassive(h); } catch (const std::invalid_argument&) { wrongOwner = true; }
    foreign.Wait(other); Require(wrongOwner, "foreign passive wait accepted");
    std::atomic<bool> wrongThread = false;
    auto worker = jobs.Submit([&] { try { jobs.WaitAllPassive({}); } catch (const std::invalid_argument&) { wrongThread = true; } });
    std::array workerHandles{worker}; jobs.WaitAllPassive(workerHandles); Require(wrongThread, "worker passive wait accepted");
    auto failed = jobs.Submit([] { throw std::runtime_error("expected"); });
    std::array f{failed}; jobs.WaitAllPassive(f); Require(failed.IsCompleted(), "passive wait did not finish failed job");
    std::puts("PASS: passive wait isolation, terminal failure and contract rejection");
}

void ParallelStates(Jobs& jobs)
{
    Model model("parallel"), source("reordered"), missing("missing"), invalid("incompatible"), other("replacement");
    Populate(model); Grow(model); Populate(source, true); Grow(source); Populate(missing); Grow(missing);
    missing.m_animations.clear(); Populate(invalid); Grow(invalid); invalid.m_skeleton->nodes[0].localBindTransform.data[3] = 17;
    Populate(other); Grow(other);
    size_t comparisons = 0;
    for (size_t count : {0u, 1u, 4u, 7u, 8u, 16u, 64u})
    {
        Scene serial, parallel;
        std::vector<Skin*> a, b;
        for (size_t i = 0; i < count; ++i) { a.push_back(&Animated(serial, model, i)); b.push_back(&Animated(parallel, model, i)); }
        for (int frame = 0; frame < 96; ++frame)
        {
            for (auto* scene : {&serial, &parallel}) for (size_t i = 0; i < scene->GetActors().size(); ++i)
            {
                auto& actor = *scene->GetActors()[i]; auto& skin = *actor.GetComponent<Skin>();
                if (frame == 0) { skin.SetLooping(i % 2 == 0); skin.SetPlaybackSpeed(i % 3 == 0 ? -1.5f : 1); skin.m_poseEvaluationRate = i % 4 == 0 ? 60.0f : 0; }
                if (frame == 10) skin.Pause();
                if (frame == 12) skin.SetTime(-1.1f);
                if (frame == 14) { skin.SetBoneLocalPosition(1, {1, 2, 3}); skin.SetBoneLocalRotation(2, FQuaternion(FVector3{20, 10, 0})); skin.SetBoneLocalScale(3, {2, 1, 1}); }
                if (frame == 16) skin.Play();
                if (frame == 20) { skin.Stop(); if (i % 2) skin.Play(); }
                if (frame == 24) { skin.SetAnimationSourceModel(&source); Require(skin.HasCompatibleAnimationSource() && skin.m_animationNodeMap[1] == 2, "reordered source fixture is not compatible/remapped"); skin.SetAnimation("motion"); skin.Play(); }
                if (frame == 28 && i % 3 == 0) actor.SetActive(false);
                if (frame == 30) actor.SetActive(true);
                if (frame == 32 && i % 5 == 0) skin.SetAnimationSourceModel(&invalid);
                if (frame == 36) { skin.SetAnimationSourceModel(nullptr); skin.SetAnimation(1u); }
                if (frame == 40) skin.SetAnimation(2u);
                if (frame == 44) skin.SetAnimation(3u);
                if (frame == 48) skin.SetAnimationSourceModel(&missing);
                if (frame == 52) { skin.SetAnimationSourceModel(nullptr); skin.SetAnimation("motion"); }
                if (frame == 56 && i % 2) actor.GetComponent<CModelRenderer>()->SetModel(nullptr);
                if (frame == 60) { actor.GetComponent<CModelRenderer>()->SetModel(&other); skin.SetAnimation("motion"); skin.Play(); }
                if (frame == 64) skin.SetAnimation(std::nullopt);
                if (frame == 65) skin.SetBoneLocalPosition(0, {3, 4, 5});
                if (frame == 68) skin.SetAnimation("motion");
                if (frame == 72) skin.SetPlaybackSpeed(0);
                if (frame == 76) skin.SetPlaybackSpeed(2);
            }
            const auto dt = std::array<float, 6>{0, 1.0f/240, 1.0f/60, 0.08f, -0.01f, 0.02f}[frame % 6];
            serial.Update(dt); parallel.Update(dt, jobs);
            serial.LateUpdate(dt); parallel.LateUpdate(dt);
            for (size_t i = 0; i < count; ++i) { Require(State(*a[i]) == State(*b[i]), "parallel playback/pose mismatch"); ++comparisons; }
        }
    }
    std::printf("PASS: %zu serial/parallel per-instance snapshots at seven batch sizes\n", comparisons);
}

struct UpdateProbe : CModelRenderer
{
    using CModelRenderer::CModelRenderer;
    std::function<void()> callback;
    void OnUpdate(float) override { if (callback) callback(); }
};
struct ScriptFixture : OvCore::Scripting::ScriptEngine
{
    using Base = OvCore::Scripting::ScriptEngine;
    using Base::Base;
    sol::state& State() { return *m_context.luaState; }
};

std::string ObserveOrder(Jobs& jobs, Model& model, const std::filesystem::path& dir, bool parallel, bool lua)
{
    ScriptFixture scripts(dir, dir); ServiceLocator::Provide(static_cast<OvCore::Scripting::ScriptEngine&>(scripts));
    Scene scene;
    std::vector<Skin*> skins;
    for (size_t i = 0; i < 33; ++i) skins.push_back(&Animated(scene, model, i));
    std::vector<uint64_t> versions; for (auto* skin : skins) versions.push_back(skin->GetPoseVersion());
    std::string observed;
    int calls = 0;
    auto observe = [&]
    {
        ++calls;
        Require(skins.front()->GetPoseVersion() == versions.front() + 1, "prior batch not published before observer");
        Require(skins[16]->GetPoseVersion() == versions[16] + 1, "own pose not ready before observer");
        Require(skins.back()->GetPoseVersion() == versions.back(), "future actor advanced before observer");
        observed = State(*skins.front()) + State(*skins[16]) + State(*skins.back());
        skins.back()->SetTime(0.8f); skins.front()->SetBoneLocalPosition(0, {5, 6, 7});
        // Change component lifetime and resource binding before the next batch.
        auto& future = skins[20]->owner;
        future.RemoveComponent<Skin>(); skins[20] = &future.AddComponent<Skin>(); skins[20]->SetAnimation("motion");
        skins[21]->owner.GetComponent<CModelRenderer>()->SetModel(nullptr);
    };
    if (lua)
    {
        std::ofstream script(dir / "batch-observer.lua"); script << "local t = {}\nfunction t:OnUpdate(dt) observe() end\nreturn t\n"; script.close();
        scripts.State().set_function("observe", observe);
        skins[16]->owner.AddBehaviour("batch-observer.lua"); Require(scripts.IsOk(), "Lua observer failed to register");
    }
    else skins[16]->owner.AddComponent<UpdateProbe>().callback = observe;
    if (parallel) scene.Update(0.02f, jobs); else scene.Update(0.02f);
    Require(calls == 1, "observer did not execute exactly once");
    for (auto* skin : skins) observed += State(*skin);
    return observed;
}

void BoundaryValidation(Jobs& jobs, const std::filesystem::path& dir)
{
    Model model("boundary"); Populate(model); Grow(model);
    for (bool lua : {false, true}) Require(ObserveOrder(jobs, model, dir, false, lua) == ObserveOrder(jobs, model, dir, true, lua), "callback boundary changed observable order");
    Scene scene; std::vector<Skin*> skins;
    for (int i = 0; i < 64; ++i) skins.push_back(&Animated(scene, model));
    bool ran = false;
    auto queued = jobs.Submit([&] { ran = true; scene.DestroyActor(skins.back()->owner); }, {}, OvTools::Jobs::EAffinity::MainThread);
    scene.Update(0.02f, jobs); Require(!ran, "scene wait re-entered main mutation"); jobs.Wait(queued); Require(ran, "queued mutation did not execute later");
    bool rejected = false;
    std::thread foreign([&] { try { scene.Update(0.02f, jobs); } catch (const std::invalid_argument&) { rejected = true; } }); foreign.join();
    Require(rejected, "foreign scene update accepted");
    for (bool batched : {false, true})
    {
        Scene deleting; std::vector<Skin*> actors;
        for (int i = 0; i < 33; ++i) actors.push_back(&Animated(deleting, model));
        Skin* created = nullptr; uint64_t initialVersion = 0;
        actors[16]->owner.AddComponent<UpdateProbe>().callback = [&]
        {
            deleting.DestroyActor(actors[25]->owner);
            created = &Animated(deleting, model); initialVersion = created->GetPoseVersion();
        };
        if (batched) deleting.Update(0.02f, jobs); else deleting.Update(0.02f);
        Require(created && created->GetPoseVersion() == initialVersion && deleting.GetActors().size() == 33, "deleted snapshot entry/new actor was updated");
    }
    std::puts("PASS: Lua/custom observer order, component/Actor replacement, queued main deletion and thread ownership");
}

void SingleWorkerFallback()
{
    Jobs jobs(1);
    Model model("single-worker"); Populate(model); Grow(model);
    Scene serial, batched;
    std::vector<Skin*> a, b;
    for (size_t i = 0; i < 32; ++i) { a.push_back(&Animated(serial, model, i)); b.push_back(&Animated(batched, model, i)); }
    for (int frame = 0; frame < 32; ++frame)
    {
        serial.Update(0.02f); batched.Update(0.02f, jobs);
        for (size_t i = 0; i < a.size(); ++i) Require(State(*a[i]) == State(*b[i]), "single-worker fallback mismatch");
    }
    std::puts("PASS: 1024 single-worker fallback snapshots");
}

void BatchFailures(Jobs& jobs)
{
    Model good("good"), bad("bad"); Populate(good); Grow(good); Populate(bad); Grow(bad);
    {
        Scene scene; std::vector<Skin*> skins;
        for (size_t i = 0; i < 32; ++i) skins.push_back(&Animated(scene, i == 10 ? bad : good));
        const auto version = skins[10]->GetPoseVersion(); const auto oldPose = skins[10]->m_boneMatrices;
        bad.m_skeleton->nodes.back().parentIndex = 63;
        bool threw = false; try { scene.Update(0.02f, jobs); } catch (const std::invalid_argument&) { threw = true; }
        Require(threw && skins[10]->GetPoseVersion() == version && Equal(oldPose, skins[10]->m_boneMatrices), "worker failure published partial output/lost error");
        Require(skins[0]->GetPoseVersion() == version + 1 && skins.back()->GetPoseVersion() == version + 1, "successful peers not published deterministically");
        bad.m_skeleton->nodes.back().parentIndex = 31; scene.Update(0.02f, jobs);
    }
    for (int fault = 0; fault < 4; ++fault)
    {
        Scene scene; std::vector<Skin*> skins;
        for (int i = 0; i < 32; ++i) skins.push_back(&Animated(scene, good));
        const auto version = skins.front()->GetPoseVersion();
        submissionFailureCountdown = fault;
        bool threw = false; try { scene.Update(0.02f, jobs); } catch (const std::bad_alloc&) { threw = true; }
        submissionFailureCountdown = -1;
        Require(threw, "submission fault was not hit");
        for (const auto* skin : skins) Require(skin->GetPoseVersion() == version, "submission failure published part of batch");
        // Immediate destruction tests the exception-path drain under ASan.
    }
    for (int iteration = 0; iteration < 40; ++iteration)
    {
        Scene scene; for (int i = 0; i < 32; ++i) Animated(scene, good, static_cast<size_t>(i));
        scene.Update(0.02f, jobs); scene.DestroyActor(*scene.GetActors().back());
    }
    std::puts("PASS: worker error propagation/recovery, four partial-submit failures and 40 scene teardown cycles");
}

void RenderAndBenchmark(Jobs& jobs, bool trace)
{
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion=4; ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible=false; ws.focused=false; ws.width=320; ws.height=240; ws.samples=1;
    OvWindowing::Window window(device,ws); window.MakeCurrentContext();
    OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
    OvCore::ResourceManagement::ShaderManager shaders; OvCore::ResourceManagement::ModelManager models;
    const auto assets=std::filesystem::current_path()/"Data/Engine";
    shaders.ProvideAssetPaths(assets,assets); models.ProvideAssetPaths(assets,assets);
    ServiceLocator::Provide(shaders); ServiceLocator::Provide(models); ServiceLocator::Provide(jobs);
    auto* mannequin=models.GetResource(":Models/Mannequin.fbx"); auto* shader=shaders.GetResource(":Shaders/Standard.ovfx");
    Require(mannequin&&shader,"missing render assets");
    OvCore::Resources::Material material; material.SetShader(shader);
    OvRendering::Entities::Camera camera; const auto bounds=mannequin->GetBoundingSphere();
    camera.SetPosition(bounds.position+FVector3{0,0,std::max(bounds.radius*2.8f,2.0f)}); camera.SetRotation(FQuaternion(FVector3{0,180,0}));
    camera.SetFar(1000); camera.SetClearColor({0.15f,0.2f,0.3f}); camera.CacheMatrices(320,240);
    baregl::Framebuffer fb("Parallel Skinning"); OvCore::Rendering::FramebufferUtil::SetupFramebuffer(fb,320,240,true,true,false);
    OvCore::Rendering::SceneRenderer renderer(driver);
    Scene serial(assets,assets), parallel(assets,assets);
    for(auto* scene:{&serial,&parallel}) for(size_t i=0;i<32;++i)
    {
        auto& skin=Animated(*scene,*mannequin,i); skin.owner.AddComponent<CMaterialRenderer>().FillWithMaterial(material);
        skin.owner.GetComponent<CModelRenderer>()->SetFrustumBehaviour(CModelRenderer::EFrustumBehaviour::DISABLED);
        skin.owner.transform.SetLocalPosition({float(int(i%8)-4)*0.1f,0,float(i/8)*0.1f});
    }
    auto render=[&](Scene& scene)
    {
        renderer.AddDescriptor<OvCore::Rendering::SceneRenderer::SceneDescriptor>({.scene=scene,.fallbackMaterial=material});
        OvRendering::Data::FrameDescriptor frame{.renderWidth=320,.renderHeight=240,.camera=camera,.outputBuffer=fb};
        renderer.BeginFrame(frame); renderer.DrawFrame(); std::vector<unsigned char> pixels(320*240*4);
        glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data()); renderer.EndFrame(); TracyGpuCollect; return pixels;
    };
    std::vector<unsigned char> previous; size_t transitions=0;
    for(int frame=0;frame<24;++frame)
    {
        serial.Update(0.033f); parallel.Update(0.033f,jobs);
        auto a=render(serial), b=render(parallel); Require(a==b,"parallel multi-instance framebuffer mismatch");
        if(!previous.empty()&&a!=previous)++transitions;
        size_t nonBackground=0; for(size_t i=4;i<a.size();i+=4)if(std::memcmp(a.data(),a.data()+i,3))++nonBackground;
        Require(nonBackground>100,"empty parallel framebuffer"); previous=std::move(a);
    }
    Require(transitions>0,"parallel animation did not move");
    std::printf("PASS: 24 exact multi-instance skinning frames, %zu moving transitions\n",transitions);
    for(size_t count:{1u,4u,8u,16u,32u,64u,128u,256u})
    {
        Scene a,b; for(size_t i=0;i<count;++i){Animated(a,*mannequin,i);Animated(b,*mannequin,i);}
        std::vector<double> oldTimes,newTimes;
        for(int repeat=0;repeat<80;++repeat)
        {
            auto measure=[&](Scene& scene,bool batched){const auto start=std::chrono::steady_clock::now(); if(batched)scene.Update(0.02f,jobs);else scene.Update(0.02f);return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();};
            double oldTime,newTime;
            if(repeat%2){oldTime=measure(a,false);newTime=measure(b,true);}else{newTime=measure(b,true);oldTime=measure(a,false);}
            if(repeat>=16){oldTimes.push_back(oldTime);newTimes.push_back(newTime);}
        }
        std::sort(oldTimes.begin(),oldTimes.end());std::sort(newTimes.begin(),newTimes.end());
        std::printf("BENCH: workers=%u instances=%zu serial_us=%.3f batched_us=%.3f\n",jobs.GetWorkerCount(),count,oldTimes[32],newTimes[32]);
    }
    if(trace)
    {
        Scene traceScene; for(size_t i=0;i<256;++i)Animated(traceScene,*mannequin,i);
        std::puts("TRACE READY"); const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(std::chrono::steady_clock::now()<end){traceScene.Update(0.02f,jobs);FrameMark;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    }
}

int main(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0); _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    Require(argc>1,"pass output directory");
    Jobs jobs(argc>3?static_cast<uint32_t>(std::strtoul(argv[3],nullptr,10)):4);
    PassiveWaitValidation(jobs);
    jobs.SetErrorHandler([](auto) { std::fputs("UNEXPECTED WORKER ERROR CALLBACK\n", stderr); std::abort(); });
    SingleWorkerFallback(); StatisticsValidation(jobs); ParallelStates(jobs); BoundaryValidation(jobs,argv[1]); BatchFailures(jobs); RenderAndBenchmark(jobs,argc>2);
    return 0;
}
catch(const std::exception& error){allocationFailureCountdown=-1;submissionFailureCountdown=-1;std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
