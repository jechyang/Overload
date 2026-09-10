// Reuse real assets and the existing hidden-context test support.
#define main UnusedSkinningValidationMain
#include "../Skinning/serial_validation.cpp"
#undef main
#include <OvCore/Rendering/RenderFrontend.h>
#include <OvCore/Rendering/EngineBufferRenderFeature.h>
#include <OvCore/ECS/Components/CPostProcessStack.h>
#include <set>
#ifdef A5_POSTPROCESS
constexpr size_t extraViews = 1;
#else
constexpr size_t extraViews = 0;
#endif
#include <condition_variable>
#include <mutex>
#include <future>
using namespace OvRendering;
using namespace OvCore::Rendering;

namespace {
    struct Gate {
        std::mutex mutex; std::condition_variable changed;
        int phase=0, failure=0; bool reverse=false;
        std::array<bool,2> entered{}, release{}, exited{};
        std::array<std::thread::id,2> threads{};
        std::atomic_bool cancel{false};
    } gate;
}
void AuxiliaryEntry(int phase,size_t slot)
{
    std::unique_lock lock(gate.mutex);
    if(gate.phase!=phase || slot>=2)return;
    gate.entered[slot]=true;gate.threads[slot]=std::this_thread::get_id();gate.changed.notify_all();
    if(!gate.changed.wait_for(lock,std::chrono::seconds(10),[&]{return gate.release[slot];}))throw std::runtime_error("A4 task gate timeout");
    if(gate.failure==1&&slot==0)throw std::runtime_error("injected view task failure");
}
void AuxiliaryExit(int phase,size_t slot)
{
    std::lock_guard lock(gate.mutex);
    if(gate.phase!=phase||slot>=2)return;
    gate.exited[slot]=true;gate.changed.notify_all();
}
void AuxiliaryBeforeSubmit(size_t slot)
{
    if(gate.phase&&gate.failure==3&&slot==1)throw std::runtime_error("injected partial view submission");
}

void ValidatePlanFailures(const SceneFrame& frame)
{
    const auto reject = [&](auto change) {
        auto plan = frame.GetPlan(); change(plan); bool failed=false;
        try { plan.Validate(frame.GetViews()); } catch (const std::invalid_argument&) { failed=true; }
        Require(failed,"invalid resource plan accepted");
    };
    reject([](auto& plan) { plan.passes.back().viewId++; });
    reject([](auto& plan) { plan.passes.back().stages = {Data::RenderStage::Picking}; });
    reject([](auto& plan) { plan.resources[0].sceneGeneration++; });
    reject([](auto& plan) { plan.resources[0].width++; });
    reject([](auto& plan) { std::swap(plan.passes.front(),plan.passes.back()); });
    reject([](auto& plan) { plan.passes[1].writes[0].subresources=64; });
    reject([](auto& plan) { plan.passes[1].target=plan.passes[1].target->WithDrawBuffer(1); });
    reject([](auto& plan) { plan.passes[1].reads=plan.passes[1].writes; });
    reject([](auto& plan) { plan.resources[0].available=1; });
    reject([](auto& plan) { plan.resources[0].origin=Data::ResourceOrigin::History;plan.resources[0].available=0;plan.passes[0].writes.clear(); });
    reject([](auto& plan) { plan.passes[1].writes.clear(); });
    std::puts("PASS: missing producer, wrong View/Stage/generation/size/face, feedback and incomplete cubemap rejected");
}

void AuxiliaryValidation(const std::filesystem::path& dir, unsigned mode)
{
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion=4;ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=96;ws.height=64;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();Context::Driver driver(Settings::DriverSettings{true});
    OvCore::ResourceManagement::ShaderManager shaders;OvCore::ResourceManagement::ModelManager models;
    const auto assets=std::filesystem::current_path()/"Data/Engine";shaders.ProvideAssetPaths(assets,assets);models.ProvideAssetPaths(assets,assets);
    ServiceLocator::Provide(shaders);ServiceLocator::Provide(models);
    OvTools::Jobs::JobSystem jobs(mode==3?1:2);ServiceLocator::Provide(jobs);
    auto* model=models.GetResource(":Models/Mannequin.fbx");auto* shader=shaders.GetResource(":Shaders/Standard.ovfx");
    Require(model&&shader,"missing A4 assets");
    const auto bounds=model->GetBoundingSphere();const auto shadowArea=std::max(bounds.radius*2.0f,2.0f);
    OvCore::Resources::Material material,fallback;material.SetShader(shader);fallback.SetShader(shaders.GetResource(":Shaders/ShadowFallback.ovfx"));
    std::ifstream standardFile(assets/"Shaders/Standard.ovfx",std::ios::binary);
    std::string standardSource((std::istreambuf_iterator<char>(standardFile)),{});
    const auto marker=standardSource.find("#pass SHADOW_PASS");Require(marker!=std::string::npos,"shadow technique fixture missing");
    standardSource.erase(marker,std::string("#pass SHADOW_PASS").size());
    {std::ofstream custom(dir/"without-shadow.ovfx",std::ios::binary);custom<<standardSource;}
    auto* fallbackShader=shaders.GetResource((dir/"without-shadow.ovfx").string());Require(fallbackShader,"fallback fixture shader missing");
    OvCore::Resources::Material transparent;transparent.SetShader(fallbackShader);transparent.SetBlendable(true);transparent.SetDepthWriting(false);
    transparent.SetProperty("u_Albedo",FVector4{.2f,.7f,.8f,.45f});
    Scene scene(assets,assets);auto& skin=Add(scene,model);skin.SetAnimation(std::optional<uint32_t>{0u});
#ifdef A5_POSTPROCESS
    auto& postSettings = scene.CreateActor("post process").AddComponent<CPostProcessStack>();
    auto exposure = postSettings.GetAutoExposureSettings(); exposure.progressive = false;
    postSettings.SetAutoExposureSettings(exposure);
    OvCore::Resources::Material uiMaterial;
    uiMaterial.SetShader(shaders.GetResource(":Shaders/Unlit.ovfx"));
    uiMaterial.SetUserInterface(true);uiMaterial.SetBlendable(true);uiMaterial.SetDepthWriting(false);uiMaterial.SetDepthTest(false);
    uiMaterial.SetProperty("u_Diffuse",FVector4{.9f,.15f,.4f,.6f});
    auto& uiActor=scene.CreateActor("scene UI after effects");
    uiActor.AddComponent<CModelRenderer>().SetModel(model);
    uiActor.AddComponent<CMaterialRenderer>().FillWithMaterial(uiMaterial);
    uiActor.transform.SetLocalScale(FVector3{.25f,.25f,.25f});
    uiActor.transform.SetLocalPosition(bounds.position+FVector3{bounds.radius*.5f,0,bounds.radius});
#endif
    skin.owner.AddComponent<CMaterialRenderer>().FillWithMaterial(material);
    skin.owner.GetComponent<CModelRenderer>()->SetFrustumBehaviour(CModelRenderer::EFrustumBehaviour::DISABLED);
    auto& ghost=Add(scene,model);ghost.SetAnimation(std::optional<uint32_t>{0u});
    ghost.owner.transform.SetLocalPosition({.35f,0,.15f});
    ghost.owner.AddComponent<CMaterialRenderer>().FillWithMaterial(transparent);
    ghost.owner.GetComponent<CModelRenderer>()->SetFrustumBehaviour(CModelRenderer::EFrustumBehaviour::DISABLED);
    auto& light=scene.CreateActor("directional").AddComponent<CDirectionalLight>();light.GetData().castShadows=true;light.GetData().shadowMapResolution=32;
    light.GetData().shadowFollowCamera=false;light.GetData().shadowAreaSize=shadowArea;
    light.owner.transform.SetLocalPosition(bounds.position+FVector3{0,shadowArea,0});light.owner.transform.SetLocalRotation(FQuaternion({90,0,0}));
    auto& secondLight=scene.CreateActor("second directional").AddComponent<CDirectionalLight>();
    secondLight.GetData().castShadows=true;secondLight.GetData().shadowMapResolution=32;secondLight.owner.transform.SetLocalRotation(FQuaternion({90,0,0}));
    secondLight.GetData().shadowFollowCamera=false;secondLight.GetData().shadowAreaSize=shadowArea;
    secondLight.owner.transform.SetLocalPosition(bounds.position+FVector3{shadowArea*.2f,shadowArea,0});
    secondLight.owner.SetActive(false);
    auto& probe=scene.CreateActor("progressive").AddComponent<CReflectionProbe>();probe.SetCubemapResolution(16);
    probe.SetCaptureSpeed(CReflectionProbe::ECaptureSpeed::ONE_FACE);
    Entities::Camera camera;
    camera.SetPosition(bounds.position+FVector3{0,0,std::max(bounds.radius*2.8f,2.0f)});camera.SetRotation(FQuaternion(FVector3{0,180,0}));
    camera.SetFar(1000);camera.SetClearColor({.15f,.2f,.3f});camera.CacheMatrices(96,64);
    baregl::Framebuffer target;FramebufferUtil::SetupFramebuffer(target,96,64,true,true,false);
    SceneRenderer legacy(driver);legacy.SetDrawRecordingEnabled(mode!=4);RenderFrontend frontend;
    PostProcess::PostProcessPipeline postProcess;
    const auto extract = [&](const SceneRenderRequest& main, bool shadows=true, bool reflections=true) {
        SceneFrameRequest request{main,target.RecordTarget(),fallback,shadows,reflections};
#ifdef A5_POSTPROCESS
        request.postProcess=&postProcess;request.postProcessTarget=&target;request.postProcessState=driver.CreatePipelineState();
#endif
        return frontend.ExtractFrame(request);
    };
    // Fixed neutral input for the legacy path's unused reflection block before capture.
    baregl::Buffer neutral;Data::ReflectionConstants constants;neutral.Allocate(sizeof(constants),baregl::types::EAccessSpecifier::STREAM_DRAW);neutral.Upload(&constants);
    std::ofstream images(dir/"frames.bin",std::ios::binary), cubes(dir/"cubes.bin",std::ios::binary), shadows(dir/"shadows.bin",std::ios::binary);
    const auto record = [&](unsigned index) {
        SceneRenderRequest main{{scene,camera,96,64,7,9,float(index)*.125f}};
        auto frame=extract(main);
        if(index==0) { Require(frame->GetViews().size()==8+extraViews,"first capture must initialize six faces");ValidatePlanFailures(*frame); }
        else Require(frame->GetViews().size()==(index>=7?4:3)+extraViews,"progressive capture changed faces per frame");
        frame=frontend.PrepareFrame(std::move(frame),driver.CreatePipelineState(),mode==1?nullptr:&jobs);
        size_t shadowIndex=0;
        for(const auto& view:frame->GetPreparedViews())if(view->GetScene().GetPipeline().stages==std::vector{Data::RenderStage::ShadowCaster})
        {
            const auto& expected=*(shadowIndex++?secondLight.GetData().shadowCamera:light.GetData().shadowCamera);
            Require(std::memcmp(view->GetView().view.data,expected.GetViewMatrix().data,sizeof(FMatrix4))==0,"shadow view matrix differs from frozen camera");
            Require(std::memcmp(view->GetView().projection.data,expected.GetProjectionMatrix().data,sizeof(FMatrix4))==0,"shadow projection differs from frozen camera");
        }
        if(mode!=1) for(const auto& view:frame->GetPreparedViews())
            Require(view->GetPreparationThread()!=std::this_thread::get_id(),"auxiliary prepare did not run on Worker");
        return frontend.RecordFrame(std::move(frame),mode==1?nullptr:&jobs);
    };
    std::vector<unsigned char> previousPixels;size_t changedFrames=0;
    bool shadowContent=false,cubeContent=false;
    for(unsigned i=0;i<14;++i)
    {
        skin.SetTime(skin.GetAnimationDurationSeconds()*float(i)/14.0f);
        ghost.SetTime(ghost.GetAnimationDurationSeconds()*float(i)/14.0f);
        if(i==7)secondLight.owner.SetActive(true);
        light.owner.transform.SetLocalRotation(FQuaternion({85+float(i),0,0}));
        if(i==7){probe.SetInfluencePolicy(CReflectionProbe::EInfluencePolicy::LOCAL);probe.SetInfluenceSize({5,5,5});probe.SetBoxProjection(true);}
        probe.SetBrightness(.8f+float(i)*.01f);
        neutral.Bind(baregl::types::EBufferType::UNIFORM,1);
        if(mode==0||mode==4)
        {
            legacy.AddDescriptor<SceneRenderer::SceneDescriptor>({.scene=scene,.fallbackMaterial=material});
            if(mode==0)legacy.BeginCommandRecording();
            legacy.BeginFrame({96,64,camera,target});legacy.DrawFrame();legacy.EndFrame();
            if(mode==0){auto commands=legacy.FinishCommandRecording();commands.Execute(driver);}
        }
        else {auto commands=record(i);commands.Execute(driver);}
        std::vector<unsigned char> pixels(96*64*4);target.RecordTarget().ReadRGBA8(pixels);
        bool nonBackground=false;for(size_t p=4;p<pixels.size();p+=4)nonBackground=nonBackground||std::memcmp(pixels.data(),pixels.data()+p,3)!=0;
        Require(nonBackground,"A4 main image is blank");
        if(!previousPixels.empty()&&pixels!=previousPixels)++changedFrames;previousPixels=pixels;
        images.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
        std::vector<float> depth(32*32);const auto shadow=light.GetData().shadowBuffer->RetainTextureAttachment(baregl::types::EFramebufferAttachment::DEPTH);
        glGetTextureImage(shadow->GetID(),0,GL_DEPTH_COMPONENT,GL_FLOAT,static_cast<GLsizei>(depth.size()*sizeof(float)),depth.data());
        shadowContent=shadowContent||std::any_of(depth.begin(),depth.end(),[](float value){return value<1.0f;});
        shadows.write(reinterpret_cast<const char*>(depth.data()),depth.size()*sizeof(float));
        if(i>=7)
        {
            const auto secondShadow=secondLight.GetData().shadowBuffer->RetainTextureAttachment(baregl::types::EFramebufferAttachment::DEPTH);
            glGetTextureImage(secondShadow->GetID(),0,GL_DEPTH_COMPONENT,GL_FLOAT,static_cast<GLsizei>(depth.size()*sizeof(float)),depth.data());
            shadows.write(reinterpret_cast<const char*>(depth.data()),depth.size()*sizeof(float));
        }
        for(int mip=0,size=16;size;size/=2,++mip)
        {
            std::vector<float> cube(size*size*6*4);
            glGetTextureImage(probe.GetCubemap()->GetID(),mip,GL_RGBA,GL_FLOAT,static_cast<GLsizei>(cube.size()*sizeof(float)),cube.data());
            for(size_t c=0;c<cube.size();c+=4)cubeContent=cubeContent||cube[c]>0||cube[c+1]>0||cube[c+2]>0;
            cubes.write(reinterpret_cast<const char*>(cube.data()),cube.size()*sizeof(float));
        }
        Require(glGetError()==GL_NO_ERROR,"A4 image GL failure");
    }
    Require(changedFrames>0&&shadowContent&&cubeContent,"A4 dynamic shadow/cube fixture produced no useful image evidence");
    if(mode==2)
    {
        secondLight.owner.SetActive(false);
        probe.SetRefreshMode(CReflectionProbe::ERefreshMode::ON_DEMAND);
        const auto capture=[&](const std::atomic_bool* cancel=nullptr) {
            SceneRenderRequest main{{scene,camera,96,64,7,9,0}};
            auto frame=extract(main);
            Require(frame->GetViews().size()==8+extraViews,"failed history did not rebuild all faces");
            return frame;
        };
        probe.RequestCapture(true);
        auto referenceFrame=frontend.PrepareFrame(capture(),driver.CreatePipelineState());
        auto referenceCommands=frontend.RecordFrame(std::move(referenceFrame));referenceCommands.Execute(driver);
        std::vector<unsigned char> reference(96*64*4);target.RecordTarget().ReadRGBA8(reference);
        for(int phase:{1,2})for(int failure:{0,1,2,3})for(bool reverse:{false,true})
        {
            probe.RequestCapture(true);auto frame=capture();
            if(phase==2)frame=frontend.PrepareFrame(std::move(frame),driver.CreatePipelineState(),&jobs);
            {std::lock_guard lock(gate.mutex);gate.phase=phase;gate.failure=failure;gate.reverse=reverse;
             gate.entered={};gate.release={};gate.exited={};gate.threads={};gate.cancel=false;}
            bool observed=false;
            auto controller=std::async(std::launch::async,[&] {
                std::unique_lock lock(gate.mutex);
                observed=gate.changed.wait_for(lock,std::chrono::seconds(10),[&]{return gate.entered[0]&&(failure==3||gate.entered[1]);});
                if(failure==2)gate.cancel=true;
                const size_t first=reverse&&failure!=3?1:0;
                gate.release[first]=true;gate.changed.notify_all();
                if(observed) observed=gate.changed.wait_for(lock,std::chrono::seconds(10),[&]{return gate.exited[first];});
                gate.release={true,true};gate.changed.notify_all();
            });
            bool rejected=false;std::optional<Data::RenderCommandList> commands;
            try {
                if(phase==1)frame=frontend.PrepareFrame(std::move(frame),driver.CreatePipelineState(),&jobs,&gate.cancel);
                else commands.emplace(frontend.RecordFrame(std::move(frame),&jobs,&gate.cancel));
            } catch(const std::runtime_error&){rejected=true;}
            controller.get();
            {std::lock_guard lock(gate.mutex);
             Require(observed&&gate.exited[0]&&(failure==3||gate.exited[1]),"view failure did not join all submitted workers");
             if(failure!=3)Require(gate.threads[0]!=gate.threads[1],"independent views did not overlap on two Workers");
             gate.phase=0;gate.cancel=false;}
            Require(rejected==(failure!=0),"view failure outcome incorrect");
            if(!rejected)
            {
                if(phase==1)commands.emplace(frontend.RecordFrame(std::move(frame),&jobs));
                commands->Execute(driver);
                std::vector<unsigned char> image(reference.size());target.RecordTarget().ReadRGBA8(image);
                Require(image==reference,"out-of-order view completion changed image");
            }
            else
            {
                auto recovery=frontend.PrepareFrame(capture(),driver.CreatePipelineState(),&jobs);
                auto recovered=frontend.RecordFrame(std::move(recovery),&jobs);recovered.Execute(driver);
            }
        }
        // Queued descendants must reject an invalid prefix before executing any GPU work.
        probe.RequestCapture(true);auto first=frontend.RecordFrame(frontend.PrepareFrame(capture(),driver.CreatePipelineState(),&jobs),&jobs);
        probe.RequestCapture(true);auto second=frontend.RecordFrame(frontend.PrepareFrame(capture(),driver.CreatePipelineState(),&jobs),&jobs);
        first.Cancel();bool rejected=false;try{second.Execute(driver);}catch(const std::logic_error&){rejected=true;}
        Require(rejected&&second.GetExecutedCount()==0,"cancelled history descendant executed GPU work");
        auto rebuilt=frontend.RecordFrame(frontend.PrepareFrame(capture(),driver.CreatePipelineState(),&jobs),&jobs);rebuilt.Execute(driver);
        probe.RequestCapture(true);auto prefix=frontend.RecordFrame(frontend.PrepareFrame(capture(),driver.CreatePipelineState(),&jobs),&jobs);
        Data::RenderCommandList broken;broken.Append(std::move(prefix));broken.Append([](auto&){throw std::runtime_error("partial GPU execution");});broken.Seal();
        probe.RequestCapture(true);auto dependent=frontend.RecordFrame(frontend.PrepareFrame(capture(),driver.CreatePipelineState(),&jobs),&jobs);
        bool failed=false;try{broken.Execute(driver);}catch(const std::runtime_error&){failed=true;}
        Require(failed&&broken.GetExecutedCount()>0,"partial GPU failure was not injected");
        failed=false;try{dependent.Execute(driver);}catch(const std::logic_error&){failed=true;}
        Require(failed&&dependent.GetExecutedCount()==0,"failed GPU prefix did not invalidate descendant");
        auto recovered=frontend.RecordFrame(frontend.PrepareFrame(capture(),driver.CreatePipelineState(),&jobs),&jobs);recovered.Execute(driver);
        std::vector<unsigned char> recoveredImage(reference.size());target.RecordTarget().ReadRGBA8(recoveredImage);
        Require(recoveredImage==reference,"GPU failure recovery changed image");
        {
            SceneRenderRequest main{{scene,camera,96,64,7,9,0}};
            auto disabled=extract(main,false,false);
            Require(disabled->GetViews().size()==1+extraViews,"disabled producers still submitted auxiliary views");
            auto disabledCommands=frontend.RecordFrame(frontend.PrepareFrame(std::move(disabled),driver.CreatePipelineState(),&jobs),&jobs);
            disabledCommands.Execute(driver);
        }
        // Retained target generations and all view inputs survive source mutations/destruction.
        probe.RequestCapture(true);auto detached=capture();const auto retainedTarget=target.RecordTarget();
#ifdef A5_POSTPROCESS
        scene.DestroyActor(postSettings.owner);scene.DestroyActor(uiActor);
#endif
        target.Resize(80,48);probe.SetCubemapResolution(32);material.SetProperty("u_EmissiveIntensity",5.0f);
        scene.DestroyActor(skin.owner);scene.DestroyActor(ghost.owner);scene.DestroyActor(light.owner);scene.DestroyActor(secondLight.owner);scene.DestroyActor(probe.owner);
        auto owned=frontend.RecordFrame(frontend.PrepareFrame(std::move(detached),driver.CreatePipelineState(),&jobs),&jobs);owned.Execute(driver);
        retainedTarget.ReadRGBA8(recoveredImage);Require(recoveredImage==reference,"source destruction/resize changed retained view output");
        std::puts("PASS: source destruction, target/probe resize, partial GPU failure and descendant rejection preserve owned inputs and rebuild history");
        std::puts("PASS: independent views overlap on two Workers, forward/reverse completion, Prepare/Record failure, cancellation, partial submit, joins and history rebuild");
    }
    Require(images.good()&&cubes.good()&&shadows.good(),"A4 image output failed");
    std::puts("PASS: 14 animated main/shadow/progressive reflection frames and every published cube mip captured");
}
int main(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    Require(argc==3,"pass output directory and mode");AuxiliaryValidation(argv[1],std::stoul(argv[2]));return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
