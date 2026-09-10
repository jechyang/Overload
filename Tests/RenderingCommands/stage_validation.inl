// Included by the existing real-GL frontend harness in --stages mode.
#include <OvRendering/Core/PipelineRunner.h>
#include <OvRendering/Core/ViewPreparer.h>
#ifdef FRONTEND_BINDING_VALIDATION
#include <OvCore/ECS/Components/CPointLight.h>
#endif
using Stage = OvRendering::Data::RenderStage;
using SceneRequest = OvCore::Rendering::SceneRenderRequest;
namespace
{
#ifdef FRONTEND_BINDING_VALIDATION
    int activeBindingPhase = 0;
    thread_local bool bindingProbe = false;
#endif
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
#ifdef FRONTEND_BINDING_VALIDATION
    if (activeBindingPhase != 0 && !bindingProbe) return;
#endif
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
#ifdef FRONTEND_BINDING_VALIDATION
    if (activeBindingPhase != 0 && !bindingProbe) return;
#endif
    std::lock_guard lock(rangeGate.mutex);
    if (rangeGate.mode == GateMode::Off) return;
    const size_t slot = begin == 0 ? 0 : 1;
    rangeGate.exited[slot] = true; rangeGate.finished.push_back(slot); rangeGate.changed.notify_all();
}
void StageRangeDraw(size_t begin)
{
#ifdef FRONTEND_BINDING_VALIDATION
    if (activeBindingPhase != 0 && !bindingProbe) return;
#endif
    std::lock_guard lock(rangeGate.mutex);
    if (rangeGate.mode == GateMode::Fail && begin == 0)
        throw std::runtime_error("A2 injected failure after private candidate output");
}
void StageBeforeSubmit(size_t slot)
{
#ifdef FRONTEND_BINDING_VALIDATION
    if (activeBindingPhase != 0 && !bindingProbe) return;
#endif
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
#ifdef FRONTEND_BINDING_VALIDATION
        Populate(*model);
#endif
        for (unsigned m=0; m<3; ++m)
        {
            const float x = float(m)*.12f, z = float(m)*.03f;
            const std::array<OvRendering::Geometry::Vertex,3> vertices{{
                {{-.4f+x,-.4f,z},{0,0},{0,0,1}}, {{.4f+x,-.4f,z},{1,0},{0,0,1}}, {{x,.4f,z},{.5f,1},{0,0,1}}
            }};
#ifdef FRONTEND_BINDING_VALIDATION
            if (m != 2)
            {
                std::array<OvRendering::Geometry::SkinnedVertex,3> skinned;
                for (size_t v=0;v<3;++v) { static_cast<OvRendering::Geometry::Vertex&>(skinned[v])=vertices[v]; skinned[v].boneIDs[0]=0;skinned[v].boneWeights[0]=1; }
                model->m_meshes.push_back(new OvRendering::Resources::Mesh(skinned,indices,m));
            }
            else
#endif
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
#ifdef FRONTEND_BINDING_VALIDATION
            material->SetProperty("u_EmissiveIntensity", .2f + float(frame)*.07f, true);
            if(m%3==1)material->AddFeature("SKINNING"); // Identity when a component is absent.
#else
            material->SetProperty("u_EmissiveIntensity", .7f);
#endif
            material->SetProperty("u_EmissiveColor", FVector3{.2f+float(m)*.1f,.5f-float(m)*.04f,.3f});
            material->SetProperty("u_Albedo", FVector4{1,1,1,m%3 == 0 ? 1.f : .5f});
            if (frame == 6) material->AddFeature("MISSING_A2_VARIANT"); // Defined empty-feature fallback.
        }
        for (unsigned i=0; i<6; ++i)
        {
            auto& actor = scene->CreateActor("mixed"+std::to_string(i)); actor.SetGUID(100+i);
            actor.AddComponent<CModelRenderer>().SetModel(model.get());
#ifdef FRONTEND_BINDING_VALIDATION
            if(i%2==0)
            {
                auto& skin=actor.AddComponent<Skin>(); skin.Pause();
                skin.SetBoneLocalPosition(0,{float(frame)*.025f,float(i)*.012f,0});
                Require(skin.HasSkinningData(), "A3 palette fixture missing");
            }
#endif
            auto& mr = actor.AddComponent<CMaterialRenderer>();
            for (uint8_t m=0; m<3; ++m) mr.SetMaterialAtIndex(m, materials[m+(i%2)*3]);
            actor.transform.SetLocalPosition({i == 5 ? 100.f : (float(i%3)-1)*.9f,0,-float(i%2)*.2f+float(frame)*.01f});
            if (i == 3) actor.SetActive(false);
            if (i == 4) actor.transform.SetLocalPosition(scene->GetActors()[0]->transform.GetWorldPosition());
        }
#ifdef FRONTEND_BINDING_VALIDATION
        auto& lightActor=scene->CreateActor("directional");
        lightActor.transform.SetLocalRotation(FQuaternion(FVector3{20,30,0}));
        auto& light=lightActor.AddComponent<CDirectionalLight>().GetData();
        light.castShadows=false; light.intensity=.5f+float(frame)*.1f;
        auto& farLight=scene->CreateActor("culled light");farLight.transform.SetLocalPosition({100000,0,0});
        farLight.AddComponent<CPointLight>().GetData().castShadows=false;
#endif
    }
};
void CompareStageModes(Frontend& frontend, SceneRequest request, OvRendering::Data::PipelineState state,
    OvTools::Jobs::JobSystem& jobs, OvWindowing::Window& window,
    std::initializer_list<std::pair<Stage,std::vector<uint64_t>>> order)
{
    std::vector<std::pair<Stage,uint64_t>> expected;
    for(const auto& [stage,indices]:order)for(auto index:indices)expected.emplace_back(stage,index);
    auto snapshot=frontend.ExtractScene(request);
    glfwMakeContextCurrent(nullptr);
    auto serial=frontend.Prepare(snapshot,state);
    auto parallel=frontend.Prepare(snapshot,state,&jobs);
    Require(StageOrder(*serial)==expected && StageOrder(*parallel)==expected,"stage order differs from explicit fixture expectation");
    for(const auto& draw:serial->GetDraws())
    {
        const auto& mesh=serial->GetScene().GetMeshes()[draw.sourceIndex];
        Require(mesh.actorId!=103 && mesh.actorId!=105,"inactive/far object entered stage output");
    }
    window.MakeCurrentContext();
}
void StageSemantics(const std::filesystem::path& assets, OvRendering::Resources::Shader* shader,
    OvRendering::Context::Driver& driver, OvTools::Jobs::JobSystem& jobs, OvWindowing::Window& window)
{
    Frontend frontend; StageFixture fixture(assets,shader,0);
    OvRendering::Entities::Camera camera; camera.SetPosition({0,0,3}); camera.SetRotation(FQuaternion(FVector3{0,180,0})); camera.CacheMatrices(96,64);
    const auto state=driver.CreatePipelineState();
    SceneRequest request{{*fixture.scene,camera,96,64}};
    CompareStageModes(frontend,request,state,jobs,window,{{Stage::Opaque,{0,6,9,3}},{Stage::Transparent,{4,1,7,10}},{Stage::SceneUI,{5,2,8,11}}});
    auto first=frontend.ExtractScene(request);
    std::array<bool,3> participation{};
    for(const auto& mesh:first->GetMeshes())if(mesh.actorId==100)
        participation[mesh.stage==Stage::Opaque?0:mesh.stage==Stage::Transparent?1:2]=true;
    Require(std::all_of(participation.begin(),participation.end(),[](bool b){return b;}),"one object did not participate in all three stages");
    request.overrideMaterial=&fixture.materials[1]; CompareStageModes(frontend,request,state,jobs,window,{{Stage::Transparent,{3,4,5,0,1,2,6,7,8,9,10,11}}});
    request.fallbackMaterial=&fixture.materials[2]; CompareStageModes(frontend,request,state,jobs,window,{{Stage::Transparent,{3,4,5,0,1,2,6,7,8,9,10,11}}});
    request.overrideMaterial=nullptr;
    fixture.scene->GetActors()[0]->GetComponent<CMaterialRenderer>()->RemoveMaterialAtIndex(0);
    CompareStageModes(frontend,request,state,jobs,window,{{Stage::Opaque,{6,9,3}},{Stage::Transparent,{4,1,7,10}},{Stage::SceneUI,{5,0,2,8,11}}});
    OvCore::Resources::Material invalid;
    fixture.scene->GetActors()[1]->GetComponent<CMaterialRenderer>()->SetMaterialAtIndex(0,invalid);
    CompareStageModes(frontend,request,state,jobs,window,{{Stage::Opaque,{6,9}},{Stage::Transparent,{4,1,7,10}},{Stage::SceneUI,{5,0,2,8,11}}});
    request.includeOpaque=false; request.includeTransparent=false; request.includeUI=false;
    CompareStageModes(frontend,request,state,jobs,window,{{Stage::SceneUI,{0,2,8,11}}}); // Fallback remains included in actual category.
    request.includeOpaque=true; request.includeTransparent=true; request.includeUI=true;
    request.pipeline.stages={Stage::SceneUI,Stage::Opaque}; CompareStageModes(frontend,request,state,jobs,window,{{Stage::SceneUI,{5,0,2,8,11}},{Stage::Opaque,{6,9}}});
    for(auto stage : {Stage::Picking,static_cast<Stage>(999)})
    {
        request.pipeline.stages={stage}; bool rejected=false;
        try{frontend.ExtractScene(request);}catch(const std::invalid_argument&){rejected=true;}
        Require(rejected,"unsupported stage silently selected a default technique");
    }
    request.pipeline.stages={Stage::Opaque,Stage::Opaque}; bool duplicate=false;
    try{frontend.ExtractScene(request);}catch(const std::invalid_argument&){duplicate=true;}
    Require(duplicate,"duplicate stage submitted geometry twice");
    Require(&shader->GetVariant("MISSING_A2_TECHNIQUE",{"MISSING_A2_VARIANT"})==&shader->GetVariant(),"missing technique fallback changed");
    std::puts("PASS: serial/Worker stage contracts: three stages per object, global/equal-key order, override/fallback/invalid material, inclusion exception, pipeline subset/order, unsupported/duplicate stage rejection");
}
#ifdef FRONTEND_BINDING_VALIDATION
#include "binding_validation.inl"
#endif
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
        std::vector<unsigned char> allPixels; std::ostringstream trace(std::ios::binary); Hooks hooks(trace); newPath=true;
        for(unsigned frame=0;frame<8;++frame)
        {
            auto fixture=std::make_unique<StageFixture>(assets,shader,frame);
            OvRendering::Entities::Camera camera;camera.SetPosition({0,0,3});camera.SetRotation(FQuaternion(FVector3{0,180,0}));camera.SetClearColor({.1f,.15f,.25f});
            if(frame%2){camera.SetProjectionMode(OvRendering::Settings::EProjectionMode::ORTHOGRAPHIC);camera.SetSize(1.5f);}
            auto target=std::make_unique<baregl::Framebuffer>(); OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*target,96,64,true,true,true);
            const auto output=target->RecordTarget();expectedTime=float(frame)*.125f;
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
#ifdef FRONTEND_BINDING_VALIDATION
                Require(prepared->GetScene().GetLights().size()==2 && prepared->GetViewBindings().lights.size()==1,"light culling did not preserve the infinite directional light and remove the distant point light");
#endif
                auto commands=frontend->Record(prepared,output,mode==2?&jobs:nullptr);
#ifdef FRONTEND_BINDING_VALIDATION
                if(mode==2 && frame==0)BindingConcurrency(*frontend,jobs,snapshot,state,output,driver,window);
#endif
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
#ifdef FRONTEND_BINDING_VALIDATION
    BindingIsolation(assets,shader,driver,jobs,window);
#endif
    std::puts("PASS: A2 eight cold mixed-stage frames; 96 draws; perspective/orthographic, instances, missing variant; source/target/frontend lifetime; explicit UBO pollution recovery");
}
