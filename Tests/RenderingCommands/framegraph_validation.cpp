#define main UnusedSkinningValidationMain
#include "../Skinning/serial_validation.cpp"
#undef main
#include <OvRendering/Data/RenderFrame.h>
#include <iostream>
using namespace OvRendering::Data;
namespace
{
    PlanResourceDesc ColorDesc(unsigned w=64,unsigned h=32)
    {
        PlanResourceDesc desc;desc.texture.width=w;desc.texture.height=h;
        desc.texture.useMipMaps=false;desc.texture.internalFormat=baregl::types::EInternalFormat::RGBA32F;
        desc.texture.minFilter=baregl::types::ETextureFilteringMode::LINEAR;
        desc.texture.mutableDesc=baregl::data::MutableTextureDesc{baregl::types::EFormat::RGBA,baregl::types::EPixelDataType::FLOAT,nullptr};
        return desc;
    }
    RenderPlanPass Node(std::string name,std::vector<ResourceAccess> accesses)
    {
        RenderPlanPass pass;pass.name=std::move(name);pass.view=RenderPlanPass::NoView;pass.accesses=std::move(accesses);
        pass.operation=[] { return [](auto&) {}; };return pass;
    }
    void ContractValidation(const std::filesystem::path& output)
    {
        RenderPlan plan;const auto a=plan.CreateTransient("a",ColorDesc()),b=plan.CreateTransient("b",ColorDesc());
        ResourceAccess clear{a,{},ResourceAccessType::Attachment,AttachmentLoad::Clear};
        ResourceAccess write{b,{},ResourceAccessType::Attachment,AttachmentLoad::DontCare,AttachmentStore::Store,true};
        plan.passes.push_back(Node("clear",{clear}));
        plan.passes.push_back(Node("sample",{{a,{},ResourceAccessType::Sample},write}));
        plan.passes.push_back(Node("readback",{{b,{},ResourceAccessType::Readback}}));
        const auto compiled=plan.Compile();Require(compiled.lifetimes[0].first==0&&compiled.lifetimes[0].last==1&&compiled.lifetimes[1].first==1&&compiled.lifetimes[1].last==2,"fixed resource lifetimes incorrect");
        Require(compiled.estimatedPeakBytes==2*64*32*16&&compiled.dependencies.size()==2,"fixed RAW dependencies/peak incorrect");
        std::ofstream(output/"compiled-plan.txt")<<compiled.Dump();
        const auto reject=[&](auto change,const char* label) {auto invalid=plan;change(invalid);bool rejected=false;try{invalid.Compile();}catch(const std::invalid_argument&){rejected=true;}Require(rejected,label);};
        reject([](auto& p){p.passes[0].accesses[0].load=AttachmentLoad::Load;},"uninitialized attachment Load accepted");
        reject([](auto& p){p.passes[0].accesses[0].load=AttachmentLoad::DontCare;},"partial draw initialized unknown content");
        reject([](auto& p){p.passes[0].accesses[0].store=AttachmentStore::Discard;},"Discard content sampled");
        reject([](auto& p){p.passes[1].accesses[0].range.aspect=ResourceAspect::Depth;},"wrong aspect accepted");
        reject([](auto& p){p.passes[1].accesses[0].range.firstMip=1;},"wrong mip accepted");
        reject([](auto& p){p.passes[1].accesses[0].range.firstLayer=1;},"wrong layer accepted");
        reject([](auto& p){p.passes[1].accesses[0].resource.generation++;},"stale resource accepted");
        reject([](auto& p){p.passes[1].accesses[0].resource.scope++;},"foreign graph resource accepted");
        reject([](auto& p){p.passes[1].accesses[1].resource=p.passes[1].accesses[0].resource;},"attachment sample feedback accepted");
        reject([](auto& p){p.passes[0].after={1};},"fixed-order explicit cycle accepted");
        reject([](auto& p){p.passes[1].operation={};},"unowned operation accepted");
        reject([](auto& p){p.passes[1].gpuScopesEnd=1;},"unbalanced GPU scope accepted before recording");
        // Clear a destination, then load/blend without sampling that attachment.
        auto blend=plan;blend.passes.push_back(Node("blend",{{b,{},ResourceAccessType::Attachment,AttachmentLoad::Load}}));
        const auto blendCompiled=blend.Compile();Require(std::any_of(blendCompiled.dependencies.begin(),blendCompiled.dependencies.end(),[](auto e){return e.kind==DependencyKind::WAR;}),"WAR edge missing");
        RenderPlan cube;auto desc=ColorDesc(16,16);desc.kind=PlanResourceKind::TextureCube;desc.layers=6;desc.mips=5;desc.texture.useMipMaps=true;desc.texture.mutableDesc.reset();
        const auto handle=cube.CreateTransient("cube",desc);
        cube.passes.push_back(Node("faces",{{handle,{ResourceAspect::Color,0,1,0,6},ResourceAccessType::Attachment,AttachmentLoad::Clear}}));
        cube.passes.push_back(Node("mipmaps",{{handle,{ResourceAspect::Color,0,1,0,6},ResourceAccessType::MipmapSource},
            {handle,{ResourceAspect::Color,1,4,0,6},ResourceAccessType::MipmapDestination,AttachmentLoad::DontCare,AttachmentStore::Store,true}}));
        cube.passes.push_back(Node("sample-mips",{{handle,{ResourceAspect::Color,0,5,0,6},ResourceAccessType::Sample}}));cube.Compile();
        cube.passes[0].accesses[0].range.layerCount=5;bool missingFace=false;try{cube.Compile();}catch(const std::invalid_argument&){missingFace=true;}Require(missingFace,"incomplete cube faces accepted for mipmaps");
        auto depth=ColorDesc();depth.kind=PlanResourceKind::Renderbuffer;depth.aspect=ResourceAspect::DepthStencil;depth.texture.internalFormat=baregl::types::EInternalFormat::DEPTH_STENCIL;depth.sampleable=false;
        RenderPlan renderbuffers;const auto d=renderbuffers.CreateTransient("depth/stencil",depth);
        renderbuffers.passes.push_back(Node("clear-depth",{{d,{ResourceAspect::DepthStencil},ResourceAccessType::Attachment,AttachmentLoad::Clear}}));
        renderbuffers.passes.push_back(Node("bad-sample",{{d,{ResourceAspect::Depth},ResourceAccessType::Sample}}));
        bool badSample=false;try{renderbuffers.Compile();}catch(const std::invalid_argument&){badSample=true;}Require(badSample,"renderbuffer accepted as sampler");
        std::cout<<"PASS: framegraph fixed order, scoped/generation handles, RAW/WAR/WAW, Load/Clear/Discard, partial writes, aspect/mip/layer, cube mips, feedback and cycles\n";
    }
    void OperationValidation()
    {
        OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;
        OvWindowing::Context::Device device(ds);OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=64;ws.height=32;ws.samples=1;
        OvWindowing::Window window(device,ws);window.MakeCurrentContext();OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
        auto source=std::make_unique<baregl::Framebuffer>(),target=std::make_unique<baregl::Framebuffer>();
        OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*source,64,32,false,false,false);OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*target,64,32,false,false,false);
        const float color[]={.25f,.5f,.75f,1};glClearNamedFramebufferfv(source->GetID(),GL_COLOR,0,color);
        const auto sourceSnapshot=source->RecordTarget(),targetSnapshot=target->RecordTarget();
        RenderPlan plan;
        const auto src=plan.Import("source",source->RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR)->RetainStorage(),ResourceOrigin::Imported,{{}});
        const auto again=plan.Import("same source",source->RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR)->RetainStorage(),ResourceOrigin::Imported,{{}});
        Require(src==again&&plan.resources.size()==1,"duplicate import was not normalized");
        bool promotion=false;try{plan.Import("wrong",source->RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR)->RetainStorage(),ResourceOrigin::Imported,{});}catch(const std::invalid_argument&){promotion=true;}Require(promotion,"duplicate import changed availability");
        const auto dst=plan.Import("target",target->RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR)->RetainStorage(),ResourceOrigin::CurrentFrame);
        auto copy=Node("owned copy",{{src,{},ResourceAccessType::CopySource},{dst,{},ResourceAccessType::CopyDestination,AttachmentLoad::DontCare,AttachmentStore::Store,true}});
        copy.operation=[sourceSnapshot,targetSnapshot] {return [sourceSnapshot,targetSnapshot](auto&){glBlitNamedFramebuffer(sourceSnapshot.GetID(),targetSnapshot.GetID(),0,0,64,32,0,0,64,32,GL_COLOR_BUFFER_BIT,GL_NEAREST);};};
        plan.passes.push_back(std::move(copy));
        OvCore::Rendering::RenderFrontend frontend;auto frame=frontend.ComposeFrame({},std::move(plan));
        source->Resize(8,8);target->Resize(8,8);source.reset();target.reset();
        frame=frontend.PrepareFrame(std::move(frame),driver.CreatePipelineState());auto commands=frontend.RecordFrame(std::move(frame));
        RenderFrame product;product.Append(std::move(commands));product.Seal();product.Submit(driver);product.Wait();product.GetCompletion().RethrowIfFailed();
        std::vector<uint8_t> a(64*32*4),b(a.size());sourceSnapshot.ReadRGBA8(a);targetSnapshot.ReadRGBA8(b);
        Require(a==b&&a[0]>0&&glGetError()==GL_NO_ERROR,"independent copy output/lifetime failure");
        std::cout<<"PASS: view-independent owned GL copy executes through Compose/Prepare/Record/RenderFrame after source resize/destruction; duplicate imports normalize\n";
    }
    void PostProcessValidation(const std::filesystem::path& output)
    {
        OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;
        OvWindowing::Context::Device device(ds);OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=96;ws.height=64;ws.samples=1;
        OvWindowing::Window window(device,ws);window.MakeCurrentContext();OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
        OvCore::ResourceManagement::ShaderManager shaders;const auto assets=std::filesystem::current_path()/"Data/Engine";
        shaders.ProvideAssetPaths(assets,assets);ServiceLocator::Provide(shaders);
        OvCore::Rendering::PostProcess::PostProcessPipeline pipeline;
        baregl::Framebuffer target;OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,96,64,false,false,false);
        std::ofstream dump(output/"postprocess-plan.txt");
        for(const auto size:{std::pair<uint16_t,uint16_t>{1,1},{1,32},{63,17},{96,64}})for(unsigned mask=0;mask<16;++mask)
        {
            target.Resize(size.first,size.second);const float color[]={.4f,.8f,1.5f,1};glClearNamedFramebufferfv(target.GetID(),GL_COLOR,0,color);
            OvCore::Rendering::PostProcess::PostProcessSnapshot settings;
            settings.exposure.enabled=(mask&1)!=0;settings.exposure.progressive=false;settings.bloom.enabled=(mask&2)!=0;
            settings.bloom.passes=(mask&4)?10:1;settings.tonemapping.enabled=(mask&4)!=0;settings.fxaa.enabled=(mask&8)!=0;
            RenderPlan plan;plan.Import("input",target.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR)->RetainStorage(),ResourceOrigin::Imported,{{}});
            RenderCommandList commands;pipeline.Build(plan,commands,settings,target,driver.CreatePipelineState());const auto compiled=plan.Compile();
            Require(plan.passes.front().name=="PostProcess.InputBlit"&&plan.passes.back().name=="PostProcess.OutputBlit","postprocess lost entry/exit blits");
            if(!mask)Require(plan.passes.size()==2,"disabled effects still emit nodes");
            bool bloomLoad=false,luminanceMip=false;
            for(const auto& pass:plan.passes) {
                Require(pass.view==RenderPlanPass::NoView&&!pass.accesses.empty(),"postprocess still needs implicit views/undeclared access");
                if(pass.name=="Bloom.UpsampleLoadBlend") {
                    bloomLoad=true;const auto writes=std::find_if(pass.accesses.begin(),pass.accesses.end(),[](const auto& access){return access.type==ResourceAccessType::Attachment;});
                    Require(writes!=pass.accesses.end()&&writes->load==AttachmentLoad::Load&&!writes->fullCoverage,"Bloom additive accumulation did not Load existing content");
                }
                if(pass.name=="Exposure.LuminanceMipmaps")luminanceMip=true;
            }
            Require(bloomLoad==settings.bloom.enabled&&luminanceMip==settings.exposure.enabled,"effect node declarations differ from enabled settings");
            dump<<"size="<<size.first<<'x'<<size.second<<" mask="<<mask<<'\n'<<compiled.Dump();
            plan.RecordOperations(commands);commands.Seal();RenderFrame frame;frame.Append(std::move(commands));frame.Seal();frame.Submit(driver);frame.Wait();frame.GetCompletion().RethrowIfFailed();
            std::vector<float> pixels(size_t(size.first)*size.second*4);const auto texture=target.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR);
            glGetTextureImage(texture->GetID(),0,GL_RGBA,GL_FLOAT,static_cast<GLsizei>(pixels.size()*sizeof(float)),pixels.data());
            Require(std::all_of(pixels.begin(),pixels.end(),[](float value){return std::isfinite(value);})&&glGetError()==GL_NO_ERROR,"postprocess boundary produced invalid pixels/GL state");
        }
        std::cout<<"PASS: 16 postprocess effect combinations x 1x1/thin/odd/normal extents, Bloom pass bounds/Load blending, explicit luminance mip/history nodes and finite GL output\n";
    }
}
int main(int argc,char** argv) try
{
    Require(argc==2,"expected evidence directory");ContractValidation(argv[1]);OperationValidation();PostProcessValidation(argv[1]);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
