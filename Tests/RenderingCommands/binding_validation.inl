// Included by the stage harness; all instrumentation lives in compile-time copies.
namespace { struct BindingProbeScope { BindingProbeScope(){bindingProbe=true;} ~BindingProbeScope(){bindingProbe=false;} }; }
void BindingEntry(int phase, size_t begin)
{
    BindingProbeScope scope;
    Require(!glfwGetCurrentContext(), "A3 Worker/serial CPU operation has a GL context");
    if(activeBindingPhase == phase) StageRangeEntry(begin);
}
void BindingExit(int phase, size_t begin)
{
    BindingProbeScope scope;
    if(activeBindingPhase == phase) StageRangeExit(begin);
}
void BindingOutput(int phase, size_t begin)
{
    BindingProbeScope scope;
    if(activeBindingPhase == phase) StageRangeDraw(begin);
}
void BindingSubmit(int phase, size_t slot)
{
    BindingProbeScope scope;
    if(activeBindingPhase == phase) StageBeforeSubmit(slot);
}
void BindingConcurrency(Frontend& frontend, OvTools::Jobs::JobSystem& jobs,
    std::shared_ptr<const SceneSnapshot> snapshot, OvRendering::Data::PipelineState state,
    const baregl::Framebuffer::Snapshot& target, OvRendering::Context::Driver& driver, OvWindowing::Window& window)
{
    const auto main=std::this_thread::get_id();
    auto serial=frontend.Prepare(snapshot,state);
    auto serialCommands=frontend.Record(serial,target);
    const auto replay = [&](OvRendering::Data::RenderCommandList& list) {
        // Extra concurrency comparisons must not change the cold harness trace or its capacity.
        struct RestoreHooks
        {
            PFNGLDRAWELEMENTSPROC elements=glad_glDrawElements;
            PFNGLDRAWELEMENTSINSTANCEDPROC instances=glad_glDrawElementsInstanced;
            ~RestoreHooks(){glad_glDrawElements=elements;glad_glDrawElementsInstanced=instances;glfwMakeContextCurrent(nullptr);}
        } restore;
        if(traceMode!=TraceMode::Disabled){glad_glDrawElements=oldElements;glad_glDrawElementsInstanced=oldInstances;}
        window.MakeCurrentContext();list.Execute(driver);return Pixels(target);
    };
    const auto referencePixels=replay(serialCommands);
    for(int phase : {1,2}) for(auto mode : {GateMode::Forward,GateMode::Reverse,GateMode::Fail,GateMode::Cancel,GateMode::SubmitFail})
    {
        std::atomic_bool cancel{false};
        auto recordInput=phase==2?frontend.Prepare(snapshot,state):std::shared_ptr<const Prepared>{};
        std::weak_ptr<const Prepared> recordLifetime=recordInput;
        {
            std::lock_guard lock(rangeGate.mutex);
            activeBindingPhase=phase;rangeGate.mode=mode;rangeGate.entered.fill(false);rangeGate.release.fill(false);
            rangeGate.exited.fill(false);rangeGate.finished.clear();rangeGate.failed=false;rangeGate.submitFailureSeen=false;
            rangeGate.input=snapshot;
        }
        auto observer=std::async(std::launch::async,[&] {
            std::unique_lock lock(rangeGate.mutex);
            AwaitRange(lock,[&]{return rangeGate.entered[0] && (mode==GateMode::SubmitFail?rangeGate.submitFailureSeen:rangeGate.entered[1]);});
            Require(rangeGate.threads[0]!=main,"A3 range executed on coordinator");
            if(mode!=GateMode::SubmitFail)Require(rangeGate.threads[1]!=main && rangeGate.threads[0]!=rangeGate.threads[1],"A3 tasks did not overlap on two Workers");
            if(mode==GateMode::Cancel)cancel.store(true,std::memory_order_release);
            if(phase==2)Require(!recordLifetime.expired(),"published frame retired while submitted record work was blocked");
            const size_t first=mode==GateMode::Reverse?1:0;
            rangeGate.release[first]=true;rangeGate.changed.notify_all();
            AwaitRange(lock,[&]{return rangeGate.exited[first];});
            Require(!rangeGate.input.expired(),"A3 input retired before all tasks exited");
            // On partial submission only task 0 exists; after it exits the coordinator may retire the input.
            if(phase==2 && mode!=GateMode::SubmitFail)Require(!recordLifetime.expired(),"published frame retired while record task remained in flight");
            rangeGate.release[1-first]=true;rangeGate.changed.notify_all();
        });
        std::shared_ptr<const Prepared> prepared;
        std::optional<OvRendering::Data::RenderCommandList> commands;
        bool failed=false;
        try
        {
            if(phase==1)prepared=frontend.Prepare(snapshot,state,&jobs,&cancel);
            else commands.emplace(frontend.Record(std::move(recordInput),target,&jobs,&cancel));
        }
        catch(const std::runtime_error&){failed=true;}
        observer.get();
        {
            std::lock_guard lock(rangeGate.mutex);
            Require(!rangeGate.failed && rangeGate.exited[0] && (mode==GateMode::SubmitFail || rangeGate.exited[1]),"A3 returned before all submitted tasks exited");
            if(mode==GateMode::Forward || mode==GateMode::Reverse)
            {
                Require(!failed && (phase==1?bool(prepared):bool(commands)),"A3 successful tasks were not published");
                if(prepared)Require(StageOrder(*prepared)==StageOrder(*serial),"A3 completion order changed draws");
                Require(rangeGate.finished.front()==(mode==GateMode::Reverse?1u:0u),"A3 forced order was not observed");
            }
            else
            {
                Require(failed && !prepared && !commands,"A3 failure published partial output");
                if(phase==2)Require(recordLifetime.expired(),"failed recording retained its published input after join");
            }
            rangeGate.mode=GateMode::Off;activeBindingPhase=0;
        }
        if(prepared)commands.emplace(frontend.Record(prepared,target));
        if(commands)Require(replay(*commands)==referencePixels,"forced binding/record completion order changed pixels");
    }
    auto recovered=frontend.Prepare(snapshot,state,&jobs);
    auto commands=frontend.Record(recovered,target,&jobs);
    OvTools::Jobs::JobSystem single(1);
    auto one=frontend.Prepare(snapshot,state,&single);
    auto oneCommands=frontend.Record(one,target,&single);
    Require(StageOrder(*one)==StageOrder(*recovered),"single Worker changed A3 output");
    Require(commands.GetState()==OvRendering::Data::RenderCommandList::State::READY && oneCommands.GetState()==commands.GetState(),"A3 recovery failed");
    Require(replay(commands)==referencePixels && replay(oneCommands)==referencePixels,"recovery or single Worker changed pixels");
    std::puts("PASS: A3 binding and recording each overlap on two Workers; forward/reverse, private-output failure, cancel, partial submission join; recovery and single Worker");
}

void BindingIsolation(const std::filesystem::path& assets, OvRendering::Resources::Shader* shader,
    OvRendering::Context::Driver& driver, OvTools::Jobs::JobSystem& jobs, OvWindowing::Window& window)
{
    auto fixture=std::make_unique<StageFixture>(assets,shader,2);
    Frontend frontend;
    OvRendering::Entities::Camera a,b;
    a.SetPosition({0,0,3});a.SetRotation(FQuaternion(FVector3{0,180,0}));
    b.SetPosition({.1f,0,4});b.SetRotation(FQuaternion(FVector3{0,180,0}));
    SceneRequest request{{*fixture->scene,a,96,64,20,1,.25f}};
    for(auto& material:fixture->materials) { material.SetReceiveReflections(true);material.SetReceiveShadows(true); }
    auto cube=std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_CUBE);
    auto shadow=std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D);
    const baregl::data::TextureDesc desc{.width=1,.height=1,
        .minFilter=baregl::types::ETextureFilteringMode::NEAREST,.magFilter=baregl::types::ETextureFilteringMode::NEAREST,
        .horizontalWrap=baregl::types::ETextureWrapMode::REPEAT,.verticalWrap=baregl::types::ETextureWrapMode::REPEAT,
        .internalFormat=baregl::types::EInternalFormat::RGBA8,.useMipMaps=false};
    const std::array<uint8_t,24> colors{80,100,120,255,80,100,120,255,80,100,120,255,80,100,120,255,80,100,120,255,80,100,120,255};
    cube->Allocate(desc);cube->Upload(colors.data(),baregl::types::EFormat::RGBA,baregl::types::EPixelDataType::UNSIGNED_BYTE);
    shadow->Allocate(desc);shadow->Upload(colors.data(),baregl::types::EFormat::RGBA,baregl::types::EPixelDataType::UNSIGNED_BYTE);
    request.reflection.map=cube;request.reflection.constants.brightness=.25f;request.shadow.map=shadow;
    const auto sampler = [](const OvRendering::Data::MaterialPropertyType& value) {
        return std::pair{value.index(),std::visit([](const auto& v)->const void* {
            if constexpr (std::is_pointer_v<std::decay_t<decltype(v)>>) return v;
            else return nullptr;
        },value)};
    };
    const auto environment=sampler(fixture->materials[0].GetProperty("_EnvironmentMap")->value);
    const auto shadowValue=sampler(fixture->materials[0].GetProperty("_ShadowMap")->value);
    const auto sourceVersion=shader->GetVersion();
    const auto stable=fixture->materials[0].GetProperty("u_EmissiveColor")->value;
    fixture->materials[0].SetProperty("u_EmissiveColor",false);
    bool rejected=false;try{frontend.ExtractScene(request);}catch(const std::bad_variant_access&){rejected=true;}
    Require(rejected && std::get<float>(fixture->materials[0].GetProperty("u_EmissiveIntensity")->value)>.3f,"invalid snapshot consumed singleUse");
    fixture->materials[0].SetProperty("u_EmissiveColor",stable);
    auto first=frontend.ExtractScene(request);
    Require(sampler(fixture->materials[0].GetProperty("_EnvironmentMap")->value)==environment &&
        sampler(fixture->materials[0].GetProperty("_ShadowMap")->value)==shadowValue,"receiver bindings mutated source material");
    Require(std::get<float>(fixture->materials[0].GetProperty("u_EmissiveIntensity")->value)==
        std::get<float>(fixture->materials[0].GetProperty("u_EmissiveIntensity")->defaultValue),"successful extraction did not consume singleUse");
    for(auto& material:fixture->materials)material.SetProperty("u_EmissiveIntensity",1.f,true);
    fixture->scene->GetActors()[0]->GetComponent<Skin>()->SetBoneLocalPosition(0,{.15f,.12f,0});
    SceneRequest next{{*fixture->scene,b,96,64,21,1,.75f}};
    auto second=frontend.ExtractScene(next);
    Require(sampler(fixture->materials[0].GetProperty("_EnvironmentMap")->value)==environment,"neutral receiver mutated source material");
    fixture.reset();cube.reset();shadow.reset();request.reflection.map.reset();request.shadow.map.reset();
    const auto state=driver.CreatePipelineState();
    std::array<std::unique_ptr<baregl::Framebuffer>,2> targets;
    std::array<baregl::Framebuffer::Snapshot,2> outputs;
    for(size_t i=0;i<2;++i){targets[i]=std::make_unique<baregl::Framebuffer>();OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*targets[i],96,64,true,true,true);outputs[i]=targets[i]->RecordTarget();}
    glfwMakeContextCurrent(nullptr);
    auto preparedA=frontend.Prepare(first,state,&jobs),preparedB=frontend.Prepare(second,state,&jobs);
    Require(preparedA->GetDraws()[0].material->GetShaderVersion()==sourceVersion,"material version changed after Extract");
    Require(preparedA->GetScene().GetMeshes()[0].skinning.poseVersion!=preparedB->GetScene().GetMeshes()[0].skinning.poseVersion,"pose versions were not captured independently");
    auto aSerial=frontend.Record(preparedA,outputs[0]);auto bSerial=frontend.Record(preparedB,outputs[1]);
    auto bParallel=frontend.Record(preparedB,outputs[1],&jobs);auto aParallel=frontend.Record(preparedA,outputs[0],&jobs);
    window.MakeCurrentContext();aSerial.Execute(driver);bSerial.Execute(driver);
    const auto pixelsA=Pixels(outputs[0]),pixelsB=Pixels(outputs[1]);
    bParallel.Execute(driver);aParallel.Execute(driver);
    Require(Pixels(outputs[0])==pixelsA && Pixels(outputs[1])==pixelsB && pixelsA!=pixelsB,"shared material/palette/receiver bindings leaked between views");
    OvRendering::Core::MeshCommandRecorder resources;
    OvCore::Resources::Material material;material.SetShader(shader);material.SetProperty("u_EmissiveIntensity",.35f);
    OvRendering::Data::Material::PropertyOverrides overrideValues{{"u_EmissiveIntensity",.9f}};
    auto overridden=material.PrepareSnapshot(std::nullopt,std::nullopt,resources.GetFallback2D(),resources.GetFallbackCube(),{}, {},true,true,false,overrideValues);
    auto restored=material.PrepareSnapshot(std::nullopt,std::nullopt,resources.GetFallback2D(),resources.GetFallbackCube(),{},overridden,false,false);
    OvRendering::Data::Material::EncodeSnapshot(overridden)->Execute();
    GLfloat value=0;const auto program=shader->GetVariant().GetID();const auto location=glGetUniformLocation(program,"u_EmissiveIntensity");
    glGetUniformfv(program,location,&value);Require(value==.9f,"owned override not applied");
    OvRendering::Data::Material::EncodeSnapshot(restored)->Execute();glGetUniformfv(program,location,&value);
    Require(value==.35f && std::get<float>(material.GetProperty("u_EmissiveIntensity")->value)==.35f,"removing override leaked its cached value into stable material");
    std::puts("PASS: two independent views, shared material, changed pose and singleUse, imported receiver maps, source destruction, shader version, reverse recording/replay isolation");
}
