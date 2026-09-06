#define TARGET_STATE_VALIDATION_NO_MAIN
#include "target_state_validation.cpp"

struct CommandPass : PacketPass
{
    using PacketPass::PacketPass;
    bool SupportsCommandRecording() const override { return typeid(*this)==typeid(CommandPass); }
};

struct GPUCounts {unsigned draws=0,uploads=0,allocations=0,clears=0,targets=0;};
GPUCounts gpuCounts;
PFNGLDRAWELEMENTSPROC commandDraw;
PFNGLNAMEDBUFFERSUBDATAPROC commandUpload;
PFNGLNAMEDBUFFERDATAPROC commandAllocate;
PFNGLCLEARPROC commandClear;
PFNGLBINDFRAMEBUFFERPROC commandTarget;
void APIENTRY CountDraw(GLenum mode,GLsizei count,GLenum type,const void* indices){++gpuCounts.draws;commandDraw(mode,count,type,indices);}
void APIENTRY CountUpload(GLuint buffer,GLintptr offset,GLsizeiptr size,const void* data){++gpuCounts.uploads;commandUpload(buffer,offset,size,data);}
void APIENTRY CountAllocate(GLuint buffer,GLsizeiptr size,const void* data,GLenum usage){++gpuCounts.allocations;commandAllocate(buffer,size,data,usage);}
void APIENTRY CountClear(GLbitfield mask){++gpuCounts.clears;commandClear(mask);}
void APIENTRY CountTarget(GLenum target,GLuint buffer){++gpuCounts.targets;commandTarget(target,buffer);}
struct CommandHooks
{
    CommandHooks(){gpuCounts={};commandDraw=glad_glDrawElements;glad_glDrawElements=CountDraw;commandUpload=glad_glNamedBufferSubData;glad_glNamedBufferSubData=CountUpload;
        commandAllocate=glad_glNamedBufferData;glad_glNamedBufferData=CountAllocate;commandClear=glad_glClear;glad_glClear=CountClear;commandTarget=glad_glBindFramebuffer;glad_glBindFramebuffer=CountTarget;}
    ~CommandHooks(){glad_glDrawElements=commandDraw;glad_glNamedBufferSubData=commandUpload;glad_glNamedBufferData=commandAllocate;glad_glClear=commandClear;glad_glBindFramebuffer=commandTarget;}
};

void HistoryValidation(Context::Driver& driver)
{
    using List=Data::RenderCommandList;
    List::History history;
    int calls=0;
    auto record=[&]() { List list; list.UseHistory(history); list.Append([&](auto&){++calls;}); list.Seal(); return list; };
    auto first=record();auto second=record();first.Execute(driver);second.Execute(driver);
    Require(calls==2 && !history.PrepareImmediate(),"ordered history did not complete");
    for(int mode=0;mode<4;++mode)
    {
        List parent;Require(!parent.UseHistory(history),"unexpected healthy history reset");
        parent.Append([&](auto&){++calls;throw std::runtime_error("history execution failure");});
        List child;child.UseHistory(history);child.Append([&](auto&){++calls;});child.Seal();
        if(mode==0)parent.Cancel();
        if(mode==1)parent.FailRecording();
        if(mode==2){parent.Seal();try{parent.Execute(driver);}catch(const std::runtime_error&){}}
        if(mode==3){List abandoned(std::move(parent));}
        const auto before=calls;bool rejected=false;
        try{child.Execute(driver);}catch(const std::logic_error&){rejected=true;}
        Require(rejected && calls==before && child.GetExecutedCount()==0,"invalid descendant executed GPU prefix");
        List recovery;Require(recovery.UseHistory(history),"invalid history did not request reset");
        Require(!recovery.UseHistory(history),"same-list history registered twice");recovery.Seal();recovery.Execute(driver);
    }
    {auto parent=record();auto child=record();const auto before=calls;bool rejected=false;
     try{child.Execute(driver);}catch(const std::logic_error&){rejected=true;}
     Require(rejected && calls==before,"out-of-order history was consumed");parent.Cancel();
     Require(history.PrepareImmediate(),"immediate fallback did not reset poisoned history");}
    {auto pending=record();bool rejected=false;try{history.PrepareImmediate();}catch(const std::logic_error&){rejected=true;}
     Require(rejected,"immediate work overtook a pending temporal frame");pending.Execute(driver);}
    {List::History other;List parent;parent.UseHistory(history);List bridge;bridge.UseHistory(history);bridge.UseHistory(other);bridge.Seal();
     List tail;tail.UseHistory(other);tail.Append([&](auto&){++calls;});tail.Seal();parent.Cancel();
     try{bridge.Execute(driver);}catch(const std::logic_error&){}const auto before=calls;
     try{tail.Execute(driver);}catch(const std::logic_error&){}
     Require(tail.GetState()==List::State::FAILED && calls==before,"failure did not propagate between histories");}
    {List::History detached;List survivor;survivor.UseHistory(detached);survivor.Seal();
     List moved(std::move(survivor));moved.Execute(driver);Require(!detached.PrepareImmediate(),"move invalidated history");}
    {List surviving;{List::History destroyed;surviving.UseHistory(destroyed);}surviving.Seal();surviving.Execute(driver);}
    std::puts("PASS: temporal ordering, cancel/record/execute/abandon failure, dependent rejection, reset, move and owner destruction");
}

void CommandLifetimeValidation(const std::filesystem::path& dir)
{
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=64;ws.height=32;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();Context::Driver driver(Settings::DriverSettings{true});
    std::ofstream images(dir/"command-lifetime-frames.bin",std::ios::binary);
    for(unsigned iteration=0;iteration<8;++iteration)
    {
        std::vector<unsigned char> reference;
        for(bool delayed:{false,true})
        {
            std::optional<Data::RenderCommandList> commands;baregl::Framebuffer::Snapshot output;
            {
                Core::CompositeRenderer renderer(driver);renderer.SetDrawRecordingEnabled(true);
                renderer.AddFeature<EngineFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
                renderer.AddFeature<PaletteFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
                renderer.AddFeature<Features::LightingRenderFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
                renderer.AddFeature<OvCore::Rendering::ReflectionRenderFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
                renderer.AddFeature<OvCore::Rendering::ShadowRenderFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
                Scene scene;auto& probe=scene.CreateActor("owned probe").AddComponent<OvCore::ECS::Components::CReflectionProbe>();
                probe.SetCubemapResolution(4);probe.SetCaptureSpeed(OvCore::ECS::Components::CReflectionProbe::ECaptureSpeed::SIX_FACES);
                probe.m_isAnyCubemapComplete=true;
                for(auto& texture:probe.m_cubemaps)if(texture){std::vector<unsigned char> data(4*4*6*4,80);texture->Upload(data.data(),EFormat::RGBA,EPixelDataType::UNSIGNED_BYTE);}
                Entities::Light light;light.type=Settings::ELightType::DIRECTIONAL;light.castShadows=true;light.shadowCamera=Entities::Camera{};light.lightSpaceMatrix=FMatrix4::Identity;
                light.shadowBuffer=std::make_unique<baregl::Framebuffer>();auto depth=std::make_shared<baregl::Texture>(ETextureType::TEXTURE_2D);
                depth->Allocate({.width=4,.height=4,.minFilter=ETextureFilteringMode::NEAREST,.magFilter=ETextureFilteringMode::NEAREST,.internalFormat=EInternalFormat::DEPTH_COMPONENT,.useMipMaps=false,
                    .mutableDesc=baregl::data::MutableTextureDesc{.format=EFormat::DEPTH_COMPONENT,.type=EPixelDataType::FLOAT}});
                light.shadowBuffer->Attach(depth,EFramebufferAttachment::DEPTH);Require(light.shadowBuffer->Validate(),"command shadow invalid");
                light.shadowBuffer->SetTargetDrawBuffer({});light.shadowBuffer->SetTargetReadBuffer({});const float value=.25f;glClearTexImage(depth->GetID(),0,GL_DEPTH_COMPONENT,GL_FLOAT,&value);
                const auto destroy=[](Resources::Shader* p){Resources::Loaders::ShaderLoader::Destroy(p);};
                std::unique_ptr<Resources::Shader,decltype(destroy)> shader(Resources::Loaders::ShaderLoader::Create((dir/"packet.ovfx").string()),destroy);
                Require(shader!=nullptr,"command shader missing");Mat material(shader.get());material.SetReceiveReflections(true);material.SetReceiveShadows(true);
                std::array<Geometry::SkinnedVertex,3> vertices{};vertices[0].position[0]=-.8f;vertices[0].position[1]=-.8f;vertices[1].position[0]=.8f;vertices[1].position[1]=-.8f;vertices[2].position[1]=.8f;for(auto& v:vertices)v.boneWeights[0]=1;
                const std::array<uint32_t,3> indices{0,1,2};Resources::Mesh mesh(vertices,indices);std::vector<FMatrix4> palette{FMatrix4::Identity};
                Entities::Drawable drawable;drawable.mesh=mesh;drawable.material=material;drawable.stateMask=Mask(2);drawable.featureSetOverride=Data::FeatureSet{"SKINNING"};
                drawable.AddDescriptor<OvCore::Rendering::EngineDrawableDescriptor>({FMatrix4::Translation({float(iteration)*.02f,0,0}),FMatrix4::Identity});
                drawable.AddDescriptor<OvCore::Rendering::SkinningDrawableDescriptor>({palette.data(),1,1,1});
                baregl::Framebuffer target;OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,64,32,true,true,false);output=target.RecordTarget();
                Entities::Camera camera;camera.SetClearColor({0,0,0});Data::FrameDescriptor frame{.renderWidth=64,.renderHeight=32,.camera=camera,.outputBuffer=target};
                renderer.AddDescriptor<Features::LightingRenderFeature::LightingDescriptor>({{light},{}});
                renderer.AddDescriptor<OvCore::Rendering::ReflectionRenderFeature::ReflectionDescriptor>({{probe}});
                renderer.AddPass<CommandPass>("owned",0,std::function<void(Data::PipelineState)>([&](auto pso){renderer.DrawEntity(pso,drawable);}));
                {CommandHooks hooks;if(delayed)renderer.BeginCommandRecording();renderer.BeginFrame(frame);renderer.DrawFrame();renderer.EndFrame();
                 if(delayed){commands.emplace(renderer.FinishCommandRecording());Require(gpuCounts.draws==0 && gpuCounts.uploads==0 && gpuCounts.allocations==0 && gpuCounts.clears==0 && gpuCounts.targets==0,"recording performed GPU work");}}
            }
            // Every scene/feature/material/mesh/target wrapper above has been destroyed.
            if(commands){commands->Execute(driver);Require(commands->GetState()==Data::RenderCommandList::State::COMPLETED && commands->GetPendingCount()==0,"command completion failed");}
            auto image=ReadTarget(output);Require(image[(16*64+32)*4]>0,"command image blank");
            if(!delayed)reference=image;else Require(image==reference,"destroyed-source frame command image differs");images.write(reinterpret_cast<const char*>(image.data()),image.size());
        }
    }
    {
        baregl::Framebuffer target;OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,8,8,true,true,false);Entities::Camera camera;
        Data::FrameDescriptor frame{.renderWidth=8,.renderHeight=8,.camera=camera,.outputBuffer=target};
        {Core::CompositeRenderer renderer(driver);renderer.SetDrawRecordingEnabled(true);bool called=false;
         renderer.AddPass<PacketPass>("unreviewed",0,std::function<void(Data::PipelineState)>([&](auto){called=true;}));renderer.BeginCommandRecording();
         bool rejected=false;try{renderer.BeginFrame(frame);}catch(const std::invalid_argument&){rejected=true;}
         Require(rejected && !called && !renderer.IsDrawing(),"unreviewed pass entered frame recording");renderer.CancelCommandRecording();}
        {Core::CompositeRenderer renderer(driver);renderer.SetDrawRecordingEnabled(true);bool fail=true;
         renderer.AddPass<CommandPass>("failure",0,std::function<void(Data::PipelineState)>([&](auto){renderer.Clear(true,true,true,{0,1,0,1});if(fail)throw std::runtime_error("record failure");}));
         renderer.BeginCommandRecording();renderer.BeginFrame(frame);bool rejected=false;try{renderer.DrawFrame();}catch(const std::runtime_error&){rejected=true;}renderer.EndFrame();
         Require(rejected,"recording failure did not propagate");rejected=false;try{auto invalid=renderer.FinishCommandRecording();}catch(const std::logic_error&){rejected=true;}
         Require(rejected,"failed recording produced an executable frame");renderer.CancelCommandRecording();fail=false;
         renderer.BeginCommandRecording();renderer.BeginFrame(frame);renderer.DrawFrame();renderer.EndFrame();auto next=renderer.FinishCommandRecording();next.Execute(driver);
         auto pixels=ReadTarget(target.RecordTarget());Require(pixels[0]==0 && pixels[1]==255,"next frame failed to recover after cancelled recording");}
    }
    HistoryValidation(driver);
    using List=Data::RenderCommandList;
    {List allocation;bool failed=false;allocationFailureSize=sizeof(List::Operation);allocationFailureCountdown=0;
     try{allocation.Append([](auto&){});}catch(const std::bad_alloc&){failed=true;}allocationFailureCountdown=-1;
     Require(failed && allocation.GetState()==List::State::FAILED,"append allocation failure did not invalidate sequence");}
    std::vector<int> order;List failure;
    failure.Append([&](auto&){order.push_back(1);});failure.Append([&](auto&){order.push_back(2);throw std::runtime_error("expected");});failure.Append([&](auto&){order.push_back(3);});failure.Seal();
    bool failed=false;try{failure.Execute(driver);}catch(const std::runtime_error&){failed=true;}
    Require(failed && failure.GetState()==List::State::FAILED && failure.GetExecutedCount()==1 && order==std::vector<int>{1,2},"failure ran suffix or reported completion");
    bool rejected=false;try{failure.Execute(driver);}catch(const std::logic_error&){rejected=true;}Require(rejected,"failed sequence replay accepted");
    auto retained=std::make_shared<int>(7);std::weak_ptr<int> weak=retained;List cancelled;cancelled.Append([retained](auto&){});retained.reset();cancelled.Seal();cancelled.Cancel();Require(weak.expired() && cancelled.GetState()==List::State::CANCELLED,"cancel did not release captures");
    List source;source.Append([](auto&){});source.Seal();List moved(std::move(source));Require(source.GetState()==List::State::CANCELLED,"move source remained executable");moved.Execute(driver);
    Require(glGetError()==GL_NO_ERROR && images.good(),"command lifetime GL/output failure");std::puts("PASS: 8 destroyed renderer/scene/feature/material/geometry/target command pairs; zero recording GPU side effects; terminal failure, cancellation and move ownership");
}

int main(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);Require(argc==2,"pass output directory");
    PacketValidation(argv[1]);CommandLifetimeValidation(argv[1]);TargetStateValidation();TargetPacketValidation(argv[1]);FrameInputValidation(argv[1]);FramePacketValidation(argv[1]);
    const auto immediate=std::filesystem::path(argv[1])/"immediate",delayed=std::filesystem::path(argv[1])/"delayed";std::filesystem::create_directories(immediate);std::filesystem::create_directories(delayed);
    ImageValidation(immediate,false,false);ImageValidation(delayed,false,true,true);
    const auto read=[](const auto& p){std::ifstream f(p,std::ios::binary);return std::vector<char>(std::istreambuf_iterator<char>(f),{});};Require(read(immediate/"frames.bin")==read(delayed/"frames.bin"),"SceneRenderer delayed command images differ");
    std::puts("PASS: 24 real Mannequin immediate/delayed full SceneRenderer command frames exactly match");
    const auto dynamicImmediate=std::filesystem::path(argv[1])/"dynamic-immediate",dynamicDelayed=std::filesystem::path(argv[1])/"dynamic-delayed";
    std::filesystem::create_directories(dynamicImmediate);std::filesystem::create_directories(dynamicDelayed);
    ImageValidation(dynamicImmediate,false,false,false,true);ImageValidation(dynamicDelayed,false,true,true,true);
    const auto dynamicRepeat=std::filesystem::path(argv[1])/"dynamic-repeat",dynamicPacket=std::filesystem::path(argv[1])/"dynamic-packet";
    std::filesystem::create_directories(dynamicRepeat);std::filesystem::create_directories(dynamicPacket);
    ImageValidation(dynamicRepeat,false,false,false,true);ImageValidation(dynamicPacket,false,true,false,true);
    Require(read(dynamicImmediate/"frames.bin")==read(dynamicRepeat/"frames.bin"),"repeated dynamic immediate image changed");
    Require(read(dynamicImmediate/"frames.bin")==read(dynamicPacket/"frames.bin"),"dynamic same-draw packet image differs");
    Require(read(dynamicImmediate/"frames.bin")==read(dynamicDelayed/"frames.bin"),"dynamic shadow/reflection/exposure/bloom command images differ");
    const auto recovered=std::filesystem::path(argv[1])/"dynamic-recovered";
    std::filesystem::create_directories(recovered);ImageValidation(recovered,false,true,true,true,true);
    Require(read(dynamicImmediate/"frames.bin")==read(recovered/"frames.bin"),"cancelled/failed temporal history did not rebuild exact dynamic image");
    std::puts("PASS: 24 dynamic frames recover exactly after cancellation, partial execution failure, abandonment and dependent cancellation; progressive exposure and on-demand progressive reflection reset");
    std::puts("PASS: 24 dynamic shadow/reflection/mipmap/exposure/bloom/tonemapping/FXAA immediate-delayed image pairs");return 0;
}
catch(const std::exception& e){allocationFailureCountdown=-1;std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
