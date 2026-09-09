// Included by the existing real-GL frontend harness in --stages mode.
#include <OvRendering/Core/PipelineRunner.h>
#include <OvRendering/Core/ViewPreparer.h>
using Stage = OvRendering::Data::RenderStage;
using SceneRequest = OvCore::Rendering::StaticSceneRequest;
using Legacy = OvCore::Rendering::SceneRenderer;
namespace
{
    enum class GateMode { Off, Forward, Reverse, Fail, Cancel, SubmitFail };
    struct RangeGate
    {
        std::mutex mutex;
        std::condition_variable changed;
        GateMode mode = GateMode::Off;
        std::array<bool,2> entered{}, release{}, exited{};
        std::array<std::thread::id,2> threads{};
        std::vector<size_t> finished;
        std::weak_ptr<const SceneSnapshot> input;
        bool failed = false, submitFailureSeen = false;
    } rangeGate;
    template<class Predicate> void AwaitRange(std::unique_lock<std::mutex>& lock, Predicate predicate)
    {
        if (!rangeGate.changed.wait_for(lock, std::chrono::seconds(10), predicate))
        {
            rangeGate.failed = true;
            rangeGate.release.fill(true); rangeGate.changed.notify_all();
            throw std::runtime_error("A2 range gate timeout");
        }
    }
}
void StageRangeEntry(size_t begin)
{
    Require(!glfwGetCurrentContext(), "candidate Prepare has a current GL context");
    std::unique_lock lock(rangeGate.mutex);
    if (rangeGate.mode == GateMode::Off) return;
    const size_t slot = begin == 0 ? 0 : 1;
    rangeGate.threads[slot] = std::this_thread::get_id();
    rangeGate.entered[slot] = true; rangeGate.changed.notify_all();
    AwaitRange(lock, [&] { return rangeGate.release[slot]; });
    Require(!rangeGate.input.expired(), "input released while a candidate task is running");
}
void StageRangeExit(size_t begin)
{
    std::lock_guard lock(rangeGate.mutex);
    if (rangeGate.mode == GateMode::Off) return;
    const size_t slot = begin == 0 ? 0 : 1;
    rangeGate.exited[slot] = true; rangeGate.finished.push_back(slot); rangeGate.changed.notify_all();
}
void StageRangeDraw(size_t begin)
{
    std::lock_guard lock(rangeGate.mutex);
    if (rangeGate.mode == GateMode::Fail && begin == 0)
        throw std::runtime_error("A2 injected failure after private candidate output");
}
void StageBeforeSubmit(size_t slot)
{
    std::unique_lock lock(rangeGate.mutex);
    if (rangeGate.mode == GateMode::SubmitFail && slot == 1)
    {
        AwaitRange(lock, [] { return rangeGate.entered[0]; });
        rangeGate.submitFailureSeen = true; rangeGate.changed.notify_all();
        throw std::runtime_error("A2 injected second submission failure");
    }
}
std::vector<std::pair<Stage,uint64_t>> StageOrder(const Prepared& frame)
{
    std::vector<std::pair<Stage,uint64_t>> result;
    OvRendering::Core::PipelineRunner::Run(frame, [&](Stage stage, auto draws) {
        for (auto& draw : draws) result.emplace_back(stage, frame.GetScene().GetMeshes()[draw.sourceIndex].collectionIndex);
    });
    return result;
}
void StageConcurrency(Frontend& frontend, OvTools::Jobs::JobSystem& jobs,
    std::array<std::shared_ptr<const SceneSnapshot>,6> snapshots, OvRendering::Data::PipelineState state)
{
    const auto main = std::this_thread::get_id();
    const auto serial = frontend.Prepare(snapshots[5], state);
    const auto expected = StageOrder(*serial);
    size_t index = 0;
    for (const auto mode : {GateMode::Forward, GateMode::Reverse, GateMode::Fail, GateMode::Cancel, GateMode::SubmitFail})
    {
        std::atomic_bool cancel{false};
        {
            std::lock_guard lock(rangeGate.mutex);
            rangeGate.mode = mode; rangeGate.entered.fill(false); rangeGate.release.fill(false);
            rangeGate.exited.fill(false); rangeGate.finished.clear(); rangeGate.failed = false; rangeGate.submitFailureSeen = false;
            rangeGate.input = snapshots[index];
        }
        auto observer = std::async(std::launch::async, [&] {
            std::unique_lock lock(rangeGate.mutex);
            AwaitRange(lock, [&] { return rangeGate.entered[0] && (mode == GateMode::SubmitFail ? rangeGate.submitFailureSeen : rangeGate.entered[1]); });
            Require(rangeGate.threads[0] != main, "range executed on coordinator");
            if (mode != GateMode::SubmitFail)
                Require(rangeGate.threads[1] != main && rangeGate.threads[0] != rangeGate.threads[1], "independent ranges did not overlap on two Workers");
            if (mode == GateMode::Cancel) cancel.store(true, std::memory_order_release);
            const size_t first = mode == GateMode::Reverse ? 1 : 0;
            rangeGate.release[first] = true; rangeGate.changed.notify_all();
            AwaitRange(lock, [&] { return rangeGate.exited[first]; });
            if (mode != GateMode::SubmitFail) Require(!rangeGate.input.expired(), "input expired before coordinator joined all work");
            rangeGate.release[1-first] = true; rangeGate.changed.notify_all();
        });
        std::shared_ptr<const Prepared> published;
        bool rejected = false;
        try { published = frontend.Prepare(std::move(snapshots[index]), state, &jobs, &cancel); }
        catch (const std::runtime_error&) { rejected = true; }
        observer.get();
        {
            std::lock_guard lock(rangeGate.mutex);
            Require(!rangeGate.failed, "concurrency probe timed out");
            Require(rangeGate.exited[0] && (mode == GateMode::SubmitFail || rangeGate.exited[1]), "Prepare returned before join");
            if (mode == GateMode::Forward || mode == GateMode::Reverse)
            {
                Require(!rejected && published && StageOrder(*published) == expected, "completion order changed global stage lists");
                Require(rangeGate.finished.front() == (mode == GateMode::Reverse ? 1u : 0u), "requested completion order not observed");
            }
            else Require(rejected && !published && rangeGate.input.expired(), "failed branch published or retained partial results");
            rangeGate.mode = GateMode::Off;
        }
        ++index;
    }
    auto recovered = frontend.Prepare(std::move(snapshots[5]), state, &jobs);
    Require(StageOrder(*recovered) == expected, "recovery changed stage output");
    OvTools::Jobs::JobSystem single(1);
    auto one = frontend.Prepare(std::shared_ptr<const SceneSnapshot>(serial, &serial->GetScene()), state, &single);
    Require(StageOrder(*one) == expected && one->GetPreparationThreads().size() == 2, "single Worker range schedule differs");
    std::puts("PASS: two Workers overlap; forward/reverse completion identical; private-output failure, partial submission and cancel join; recovery; single Worker matches");
}
struct StageFixture
{
    std::unique_ptr<Scene> scene;
    std::unique_ptr<Model> model;
    std::array<OvCore::Resources::Material,6> materials; // Contiguous stable source-address ordering across cold processes.
    StageFixture(const std::filesystem::path& assets, OvRendering::Resources::Shader* shader, unsigned frame)
    {
        scene = std::make_unique<Scene>(assets, assets);
        model = std::make_unique<Model>("A2 mixed stages");
        const std::array<uint32_t,3> indices{0,1,2};
        for (unsigned m=0; m<3; ++m)
        {
            const float x = float(m)*.12f, z = float(m)*.03f;
            const std::array<OvRendering::Geometry::Vertex,3> vertices{{
                {{-.4f+x,-.4f,z},{0,0},{0,0,1}}, {{.4f+x,-.4f,z},{1,0},{0,0,1}}, {{x,.4f,z},{.5f,1},{0,0,1}}
            }};
            model->m_meshes.push_back(new OvRendering::Resources::Mesh(vertices,indices,m));
        }
        for (unsigned m=0; m<6; ++m)
        {
            auto* material = &materials[m];
            material->SetShader(shader); material->SetReceiveShadows(false); material->SetReceiveReflections(false);
            material->SetBackfaceCulling(false); material->SetBlendable(m%3 != 0); material->SetUserInterface(m%3 == 2);
            material->SetDepthWriting(m%3 == 0); material->SetDepthTest(m%3 != 2);
            material->SetGPUInstances(frame%2 ? 2 : 1);
            material->SetDrawOrder(frame%3 == 1 ? int(m/3) : 0);
            material->SetProperty("u_EmissiveIntensity", .7f);
            material->SetProperty("u_EmissiveColor", FVector3{.2f+float(m)*.1f,.5f-float(m)*.04f,.3f});
            material->SetProperty("u_Albedo", FVector4{1,1,1,m%3 == 0 ? 1.f : .5f});
            if (frame == 6) material->AddFeature("MISSING_A2_VARIANT"); // Defined empty-feature fallback.
        }
        for (unsigned i=0; i<6; ++i)
        {
            auto& actor = scene->CreateActor("mixed"+std::to_string(i)); actor.SetGUID(100+i);
            actor.AddComponent<CModelRenderer>().SetModel(model.get());
            auto& mr = actor.AddComponent<CMaterialRenderer>();
            for (uint8_t m=0; m<3; ++m) mr.SetMaterialAtIndex(m, materials[m+(i%2)*3]);
            actor.transform.SetLocalPosition({i == 5 ? 100.f : (float(i%3)-1)*.9f,0,-float(i%2)*.2f+float(frame)*.01f});
            if (i == 3) actor.SetActive(false);
            if (i == 4) actor.transform.SetLocalPosition(scene->GetActors()[0]->transform.GetWorldPosition());
        }
    }
};
void CompareStageOracle(Frontend& frontend, Legacy& legacy, SceneRequest request, OvRendering::Data::PipelineState state,
    OvTools::Jobs::JobSystem& jobs, OvWindowing::Window& window)
{
    auto parsed = legacy.ParseScene({request.scene});
    auto filtered = legacy.FilterDrawables(parsed, {.camera=request.camera,
        .overrideMaterial=request.overrideMaterial ? OvTools::Utils::OptRef<OvRendering::Data::Material>(*request.overrideMaterial) : std::nullopt,
        .fallbackMaterial=request.fallbackMaterial ? OvTools::Utils::OptRef<OvRendering::Data::Material>(*request.fallbackMaterial) : std::nullopt,
        .requiredVisibilityFlags=OvCore::Rendering::EVisibilityFlags::GEOMETRY,
        .includeUI=request.includeUI, .includeTransparent=request.includeTransparent, .includeOpaque=request.includeOpaque});
    std::vector<std::pair<Stage,uint64_t>> expected;
    auto append = [&](Stage stage, const auto& list) {
        if (std::find(request.pipeline.stages.begin(), request.pipeline.stages.end(), stage)==request.pipeline.stages.end()) return;
        for (const auto& [key, draw] : list)
        {
            size_t ordinal=0;
            for (const auto& original : parsed.drawables)
            {
                if (&original.mesh.value()==&draw.mesh.value() &&
                    &original.template GetDescriptor<Legacy::SceneDrawableDescriptor>().actor==&draw.template GetDescriptor<Legacy::SceneDrawableDescriptor>().actor) break;
                ++ordinal;
            }
            expected.emplace_back(stage,ordinal);
        }
    };
    for (auto stage : request.pipeline.stages)
    {
        if(stage==Stage::Opaque)append(stage,filtered.opaques);
        if(stage==Stage::Transparent)append(stage,filtered.transparents);
        if(stage==Stage::SceneUI)append(stage,filtered.ui);
    }
    auto snapshot=frontend.ExtractScene(request);
    glfwMakeContextCurrent(nullptr);
    auto serial=frontend.Prepare(snapshot,state);
    auto parallel=frontend.Prepare(snapshot,state,&jobs);
    Require(StageOrder(*serial)==expected && StageOrder(*parallel)==expected,"stage lists differ from unchanged legacy filter");
    window.MakeCurrentContext();
}
void StageSemantics(const std::filesystem::path& assets, OvRendering::Resources::Shader* shader,
    OvRendering::Context::Driver& driver, OvTools::Jobs::JobSystem& jobs, OvWindowing::Window& window)
{
    Frontend frontend; Legacy legacy(driver); StageFixture fixture(assets,shader,0);
    OvRendering::Entities::Camera camera; camera.SetPosition({0,0,3}); camera.SetRotation(FQuaternion(FVector3{0,180,0})); camera.CacheMatrices(96,64);
    const auto state=driver.CreatePipelineState();
    SceneRequest request{{*fixture.scene,camera,96,64}};
    CompareStageOracle(frontend,legacy,request,state,jobs,window);
    auto first=frontend.ExtractScene(request);
    std::array<bool,3> participation{};
    for(const auto& mesh:first->GetMeshes())if(mesh.actorId==100)
        participation[mesh.stage==Stage::Opaque?0:mesh.stage==Stage::Transparent?1:2]=true;
    Require(std::all_of(participation.begin(),participation.end(),[](bool b){return b;}),"one object did not participate in all three stages");
    request.overrideMaterial=&fixture.materials[1]; CompareStageOracle(frontend,legacy,request,state,jobs,window);
    request.fallbackMaterial=&fixture.materials[2]; CompareStageOracle(frontend,legacy,request,state,jobs,window);
    request.overrideMaterial=nullptr;
    fixture.scene->GetActors()[0]->GetComponent<CMaterialRenderer>()->RemoveMaterialAtIndex(0);
    CompareStageOracle(frontend,legacy,request,state,jobs,window);
    OvCore::Resources::Material invalid;
    fixture.scene->GetActors()[1]->GetComponent<CMaterialRenderer>()->SetMaterialAtIndex(0,invalid);
    CompareStageOracle(frontend,legacy,request,state,jobs,window);
    request.includeOpaque=false; request.includeTransparent=false; request.includeUI=false;
    CompareStageOracle(frontend,legacy,request,state,jobs,window); // Fallback remains included in actual category.
    request.includeOpaque=true; request.includeTransparent=true; request.includeUI=true;
    request.pipeline.stages={Stage::SceneUI,Stage::Opaque}; CompareStageOracle(frontend,legacy,request,state,jobs,window);
    for(auto stage : {Stage::ShadowCaster,Stage::ReflectionCapture,Stage::Picking,static_cast<Stage>(999)})
    {
        request.pipeline.stages={stage}; bool rejected=false;
        try{frontend.ExtractScene(request);}catch(const std::invalid_argument&){rejected=true;}
        Require(rejected,"unsupported stage silently selected a default technique");
    }
    request.pipeline.stages={Stage::Opaque,Stage::Opaque}; bool duplicate=false;
    try{frontend.ExtractScene(request);}catch(const std::invalid_argument&){duplicate=true;}
    Require(duplicate,"duplicate stage submitted geometry twice");
    Require(&shader->GetVariant("MISSING_A2_TECHNIQUE",{"MISSING_A2_VARIANT"})==&shader->GetVariant(),"missing technique fallback changed");
    std::puts("PASS: unchanged filter oracle: three stages per object, global/equal-key order, override/fallback/invalid material, inclusion exception, pipeline subset/order, unsupported/duplicate stage rejection");
}
void StageValidation(const std::filesystem::path& dir, std::optional<unsigned> selectedMode)
{
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion=4; ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible=false; ws.focused=false; ws.width=96; ws.height=64; ws.samples=1;
    OvWindowing::Window window(device,ws); window.MakeCurrentContext();
    OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
    OvTools::Jobs::JobSystem jobs(2); ServiceLocator::Provide(jobs);
    OvCore::ResourceManagement::ShaderManager shaders; ServiceLocator::Provide(shaders);
    const auto assets=std::filesystem::current_path()/"Data/Engine"; shaders.ProvideAssetPaths(assets,assets);
    auto* shader=shaders.GetResource(":Shaders/Standard.ovfx"); Require(shader,"missing Standard shader");
    baregl::Buffer neutral, poison, shortRange;
    neutral.Allocate(128,baregl::types::EAccessSpecifier::STATIC_DRAW); const auto neutralData=NeutralReflection(); neutral.Upload(neutralData.data());
    std::array<float,128> poisonData{};poisonData.fill(71.f);
    poison.Allocate(512,baregl::types::EAccessSpecifier::STATIC_DRAW);poison.Upload(poisonData.data());
    shortRange.Allocate(512,baregl::types::EAccessSpecifier::STATIC_DRAW);shortRange.Upload(poisonData.data());
    poisonBuffer=poison.GetID();shortBuffer=shortRange.GetID();
    for(unsigned mode=selectedMode.value_or(0);mode<selectedMode.value_or(2)+1;++mode)
    {
        std::vector<unsigned char> allPixels; std::ostringstream trace(std::ios::binary); Hooks hooks(trace); newPath=mode!=0;
        for(unsigned frame=0;frame<8;++frame)
        {
            auto fixture=std::make_unique<StageFixture>(assets,shader,frame);
            OvRendering::Entities::Camera camera;camera.SetPosition({0,0,3});camera.SetRotation(FQuaternion(FVector3{0,180,0}));camera.SetClearColor({.1f,.15f,.25f});
            if(frame%2){camera.SetProjectionMode(OvRendering::Settings::EProjectionMode::ORTHOGRAPHIC);camera.SetSize(1.5f);}
            auto target=std::make_unique<baregl::Framebuffer>(); OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*target,96,64,true,true,true);
            const auto output=target->RecordTarget();expectedTime=float(frame)*.125f;
            if(mode==0)
            {
                Legacy legacy(driver);neutral.Bind(baregl::types::EBufferType::UNIFORM,1);
#ifndef FRONTEND_IMMEDIATE_ORACLE
                legacy.SetDrawRecordingEnabled(true);legacy.SetCommandRecordingJobs(nullptr);legacy.BeginCommandRecording();
#endif
                legacy.AddDescriptor<Legacy::SceneDescriptor>({.scene=*fixture->scene});
                legacy.BeginFrame({.renderWidth=96,.renderHeight=64,.camera=camera,.outputBuffer=*target});legacy.DrawFrame();legacy.EndFrame();
#ifndef FRONTEND_IMMEDIATE_ORACLE
                auto commands=legacy.FinishCommandRecording();commands.Execute(driver);
#endif
            }
            else
            {
                auto frontend=std::make_unique<Frontend>(); SceneRequest request{{*fixture->scene,camera,96,64,7,9,expectedTime}};
                auto snapshot=frontend->ExtractScene(request);auto state=driver.CreatePipelineState();
                std::array<std::shared_ptr<const SceneSnapshot>,6> failures;
                if(mode==2 && frame==0)for(auto& failure:failures)failure=frontend->ExtractScene(request);
                request.pipeline.stages.clear(); // Published snapshot owns its frozen configuration.
                fixture.reset();camera.SetPosition({500,500,500});glfwMakeContextCurrent(nullptr);
                auto prepared=frontend->Prepare(snapshot,state,mode==2?&jobs:nullptr);
                Require(prepared->GetLists().size()==3 && prepared->GetDraws().size()==12,"mixed stage draw count mismatch");
                if(mode==2)Require(prepared->GetPreparationThreads().size()==2,"parallel range schedule not used");
                auto commands=frontend->Record(prepared,output);
                if(mode==2 && frame==0)StageConcurrency(*frontend,jobs,std::move(failures),state);
                snapshot.reset();prepared.reset();frontend.reset();window.MakeCurrentContext();target->Resize(48,32);target.reset();
                if(bindingPolicy=="poison")PolluteBindings(frame);else{glBindBufferBase(GL_UNIFORM_BUFFER,0,0);glBindBufferBase(GL_UNIFORM_BUFFER,1,0);}
                OvRendering::Data::RenderFrame product;product.Append(std::move(commands));product.Seal();product.Submit(driver);product.Wait();product.GetCompletion().RethrowIfFailed();
                Require(product.GetCompletion().Succeeded(),"stage product frame failed");
            }
            auto pixels=Pixels(output);size_t nonBackground=0;
            for(size_t i=4;i<pixels.size();i+=4)if(std::memcmp(pixels.data(),pixels.data()+i,3))++nonBackground;
            Require(nonBackground>100,"empty mixed stage image");allPixels.insert(allPixels.end(),pixels.begin(),pixels.end());TracyGpuCollect;
        }
        auto finalTrace=hooks.Finish(trace.str());
        std::ofstream imageFile(dir/("frontend-"+std::to_string(mode)+".rgba"),std::ios::binary);imageFile.write(reinterpret_cast<const char*>(allPixels.data()),allPixels.size());
        std::ofstream traceFile(dir/("frontend-"+std::to_string(mode)+".draws"),std::ios::binary);traceFile<<finalTrace;
    }
    StageSemantics(assets,shader,driver,jobs,window);
    std::puts("PASS: A2 eight cold mixed-stage frames; 96 draws; perspective/orthographic, instances, missing variant; source/target/frontend lifetime; explicit UBO pollution recovery");
}
