#define main UnusedSkinningValidationMain
#include "../Skinning/serial_validation.cpp"
#undef main
#include <OvRendering/Data/RenderFrame.h>
#include <OvRendering/Core/RenderTargetPool.h>
#include <baregl/ResourceLifetime.h>
#include <baregl/ResourceExecutor.h>
#include <iostream>
using namespace OvRendering::Data;
namespace
{
    struct PoolSyncHooks
    {
        inline static PFNGLFENCESYNCPROC fence;
        inline static PFNGLCLIENTWAITSYNCPROC wait;
        inline static bool timeout=false,failFence=false,failWait=false;
        static GLsync APIENTRY Fence(GLenum c,GLbitfield f){return failFence?nullptr:fence(c,f);}
        static GLenum APIENTRY Wait(GLsync s,GLbitfield f,GLuint64 n){return failWait?GL_WAIT_FAILED:timeout?GL_TIMEOUT_EXPIRED:wait(s,f,n);}
        PoolSyncHooks(){fence=glad_glFenceSync;wait=glad_glClientWaitSync;timeout=failFence=failWait=false;glad_glFenceSync=Fence;glad_glClientWaitSync=Wait;}
        ~PoolSyncHooks(){glad_glFenceSync=fence;glad_glClientWaitSync=wait;}
    };
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
    void PoolValidation(const std::filesystem::path& output)
    {
        OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;
        OvWindowing::Context::Device device(ds);OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=64;ws.height=32;ws.samples=1;
        OvWindowing::Window window(device,ws);window.MakeCurrentContext();OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
        auto domain=std::make_unique<baregl::DeletionQueue>();
        auto pool=std::make_shared<OvRendering::Core::RenderTargetPool>();
        RenderPlan plan;const auto a=plan.CreateTransient("A",ColorDesc()),b=plan.CreateTransient("B",ColorDesc());
        auto result=std::make_shared<std::vector<uint8_t>>();
        for(const auto handle:{a,b}) {
            auto clear=Node("clear",{{handle,{},ResourceAccessType::Attachment,AttachmentLoad::Clear}});clear.operation={};
            clear.prepareOperation=[handle](const auto& mapping)->RenderCommandList::OperationFactory {
                const auto target=mapping.Target(handle);return [target,handle] {return [target,handle](auto&) {const float color[]={handle.index?.75f:.25f,.5f,1,1};glClearNamedFramebufferfv(target.GetID(),GL_COLOR,0,color);};};
            };plan.passes.push_back(std::move(clear));
            auto read=Node("readback",{{handle,{},ResourceAccessType::Readback}});read.operation={};
            read.prepareOperation=[handle,result](const auto& mapping)->RenderCommandList::OperationFactory {
                const auto target=mapping.Target(handle);return [target,result] {return [target,result](auto&) {std::vector<uint8_t> pixels(64*32*4);target.ReadRGBA8(pixels);result->insert(result->end(),pixels.begin(),pixels.end());};};
            };plan.passes.push_back(std::move(read));
        }
        const auto compiled=plan.Compile();
        auto mapping=pool->Realize(plan,compiled);
        Require(mapping->TextureStorage(a)==mapping->TextureStorage(b)&&mapping->PhysicalID(a)==mapping->PhysicalID(b),"nonoverlapping transients did not share real GL storage");
        const auto original=mapping->TextureStorage(a).GetID();
        std::ofstream(output/"pooled-plan.txt")<<compiled.Dump()<<mapping->Dump();
        RenderCommandList commands;plan.RecordOperations(commands,{},mapping);commands.Seal();mapping.reset();
        PoolSyncHooks hooks;RenderFrame frame;frame.Append(std::move(commands));frame.Seal();Require(frame.Submit(driver),"pooled frame submission failed");
        PoolSyncHooks::timeout=true;Require(!frame.Poll()&&pool->GetStatistics().leasedTargets==1,"GPU timeout returned an active lease to pool");
        auto inFlight=pool->Realize(plan,compiled);Require(inFlight->TextureStorage(a).GetID()!=original,"in-flight storage was reused by another frame");inFlight.reset();
        PoolSyncHooks::timeout=false;frame.Wait();Require(frame.GetCompletion().Succeeded()&&pool->GetStatistics().leasedTargets==0,"frame retirement did not release pool lease");
        Require(result->size()==64*32*4*2&&(*result)[0]==64&&(*result)[64*32*4]==191,"aliased logical contents were corrupted");
        const auto golden=*result;
        const auto allocations=pool->GetStatistics().allocations;
        {auto ready=pool->Realize(plan,compiled);RenderCommandList list;plan.RecordOperations(list,{},ready);ready.reset();list.Seal();list.Execute(driver);Require(list.GetCompletion().Succeeded()&&pool->GetStatistics().leasedTargets==0,"immediate lease did not use GPU retirement");}
        Require(pool->GetStatistics().allocations==allocations,"warm pool allocated another target");
        {
            auto previous=pool->Realize(plan,compiled);const auto retained=previous->TextureStorage(a);previous.reset();
            auto inspection=plan;const auto external=inspection.Import("Debugger.PreviousGeneration",retained,ResourceOrigin::Imported,{{}});
            inspection.passes.push_back(Node("UI.PreviousGeneration",{{external,{},ResourceAccessType::Sample}}));
            auto first=pool->Realize(inspection,inspection.Compile());auto second=pool->Realize(inspection,inspection.Compile());
            Require(first->TextureStorage(a)!=retained&&second->TextureStorage(a)!=retained,"external UI sampler was overwritten by pooled scratch");
            first.reset();auto unrelated=pool->Realize(plan,compiled);
            Require(unrelated->TextureStorage(a)!=retained,"retiring one UI frame released another frame's external pool pin");
        }
        {
            OvTools::Jobs::JobSystem jobs(2);auto map=pool->Realize(plan,compiled);
            RenderCommandList parallel;plan.RecordOperations(parallel,{},map);map.reset();
            std::atomic_bool workerStarted=false;std::atomic_uint encoded=0;
            for(unsigned i=0;i<256;++i)parallel.AppendDeferred([&,i] {
                if(!jobs.IsMainThread())workerStarted=true;
                if(i==0)while(!workerStarted.load())std::this_thread::yield();
                ++encoded;return [](auto&){};
            });
            parallel.Seal(&jobs);Require(encoded==256&&workerStarted,"immutable pool commands did not encode on Workers");
            parallel.Execute(driver);Require(pool->GetStatistics().leasedTargets==0,"parallel immediate commands stranded lease");
        }
        {auto noReuse=pool->Realize(plan,compiled,false);Require(noReuse->TextureStorage(a)!=noReuse->TextureStorage(b),"diagnostic no-reuse still aliases resources");
            result->clear();RenderCommandList list;plan.RecordOperations(list,{},noReuse);noReuse.reset();list.Seal();list.Execute(driver);Require(*result==golden,"reuse/no-reuse GL images differ");}
        auto overlap=plan;overlap.passes[2].accesses.push_back({a,{},ResourceAccessType::Sample});
        {auto map=pool->Realize(overlap,overlap.Compile());Require(map->TextureStorage(a)!=map->TextureStorage(b),"same-pass read/write lifetimes were aliased");}
        auto exported=plan;exported.passes[1].accesses[0].type=ResourceAccessType::Export;
        const auto exportCompiled=exported.Compile();
        Require(exportCompiled.lifetimes[a.index].exported&&exportCompiled.lifetimes[a.index].last==exported.passes.size(),"exported target lifetime ended at its export node");
        {auto map=pool->Realize(exported,exportCompiled);Require(map->TextureStorage(a)!=map->TextureStorage(b),"exported target was aliased by a later temporary");}
        auto incompatible=plan;incompatible.resources[b.index].descriptor->texture.minFilter=baregl::types::ETextureFilteringMode::NEAREST;
        {auto map=pool->Realize(incompatible,incompatible.Compile());Require(map->TextureStorage(a)!=map->TextureStorage(b),"incompatible sampler state was reused");}
        for(unsigned difference=0;difference<4;++difference) {
            auto changed=plan;auto& desc=*changed.resources[b.index].descriptor;
            if(difference==0)desc.texture.internalFormat=baregl::types::EInternalFormat::RGBA16F;
            if(difference==1)desc.texture.width=32;
            if(difference==2)desc.texture.horizontalWrap=baregl::types::ETextureWrapMode::CLAMP_TO_EDGE;
            if(difference==3){desc.texture.useMipMaps=true;desc.mips=7;}
            auto map=pool->Realize(changed,changed.Compile());Require(map->TextureStorage(a)!=map->TextureStorage(b),"incompatible format/extent/wrap/mips was reused");
        }
        for(int fault=0;fault<5;++fault) {
            auto map=pool->Realize(plan,compiled);RenderCommandList list;list.KeepUntilRetired(map);map.reset();
            if(fault==0){list.AppendDeferred([]()->RenderCommandList::Operation {throw std::runtime_error("encode failed");});bool failed=false;try{list.Seal();}catch(...){failed=true;}Require(failed&&pool->GetStatistics().leasedTargets==0,"record failure stranded pool lease");continue;}
            list.Append([fault](auto&){if(fault==2)throw std::runtime_error("execute failed after GPU prefix");});list.Seal();RenderFrame failed;failed.Append(std::move(list));failed.Seal();
            if(fault==1){failed.Cancel();Require(pool->GetStatistics().leasedTargets==0,"cancelled frame stranded pool lease");continue;}
            PoolSyncHooks::failFence=fault==3;failed.Submit(driver);PoolSyncHooks::failWait=fault==4;
            if(fault!=3)Require(pool->GetStatistics().leasedTargets==1,"failed frame returned storage before drain");
            failed.Wait();PoolSyncHooks::failFence=PoolSyncHooks::failWait=false;
            Require(!failed.GetCompletion().Succeeded()&&pool->GetStatistics().leasedTargets==0,"GPU failure did not drain/release lease");
        }
        // Allocation failure must roll back all previously acquired targets.
        auto broken=plan;broken.resources[b.index].descriptor->texture.width=100000;
        bool rolledBack=false;try{pool->Realize(broken,compiled);}catch(...){rolledBack=true;}
        Require(rolledBack&&pool->GetStatistics().leasedTargets==0,"partial allocation failure stranded a reservation");
        pool->SetIdleBudget(0);Require(pool->GetStatistics().idleTargets==0,"idle budget did not evict retired targets");
        {auto map=pool->Realize(plan,compiled);const auto id=map->TextureStorage(a).GetID();
            std::thread release([map=std::move(map)]{});release.join();
            Require(pool->GetStatistics().leasedTargets==0&&domain->GetPendingCount()>0,"Worker release bypassed GL owner deletion queue");
            domain->Drain();Require(!glIsTexture(id),"owner did not drain evicted Worker captures");}
        auto pinned=pool->Realize(plan,compiled);const auto pinID=pinned->TextureStorage(a).GetID();pool->Close();
        Require(glIsTexture(pinID)&&pool->GetStatistics().leasedTargets==1,"pool close invalidated active frame storage");pinned.reset();Require(!glIsTexture(pinID),"closed pool failed to release final lease");
        bool closed=false;try{pool->Realize(plan,compiled);}catch(...){closed=true;}Require(closed,"closed pool reused another context");
        {
            unsigned dispatches=0;
            baregl::ResourceExecutor owner([&](auto operation){
                ++dispatches;
                // The real GL owner has no producer dispatcher. Model its
                // nested native calls without counting them as producer waits.
                baregl::ResourceExecutor native([](auto operation){operation();});operation();
            });
            OvRendering::Core::RenderTargetPool bounded;bounded.SetFrameReserve(2);
            {auto first=bounded.Realize(plan,compiled);Require(bounded.GetStatistics().allocations==2&&dispatches==1,"bounded pool did not batch two frame sets on its owner");}
            auto first=bounded.Realize(plan,compiled),second=bounded.Realize(plan,compiled);
            Require(first->TextureStorage(a)!=second->TextureStorage(a)&&dispatches==1,"warm in-flight pool performed an owner wait or reused active storage");
            bool rejected=false;
            {baregl::ResourceExecutor foreignOwner([&](auto operation){++dispatches;operation();});try{bounded.Realize(plan,compiled);}catch(const std::logic_error&){rejected=true;}}
            Require(rejected&&dispatches==1,"warm pool accepted a foreign producer binding");
        }
        auto oldDomainPool=std::make_shared<OvRendering::Core::RenderTargetPool>();auto old=oldDomainPool->Realize(plan,compiled);const auto oldStorage=old->TextureStorage(a);old.reset();
        domain->Close();domain.reset();Require(oldStorage.GetID()==0,"closed domain did not invalidate pooled storage");
        domain=std::make_unique<baregl::DeletionQueue>();bool foreign=false;try{oldDomainPool->Realize(plan,compiled);}catch(...){foreign=true;}Require(foreign,"pool reused a previous context domain");oldDomainPool.reset();
        {OvRendering::Core::RenderTargetPool fresh;auto map=fresh.Realize(plan,compiled);Require(map->TextureStorage(a).GetID()!=0,"new resource domain could not allocate");}
        Require(glGetError()==GL_NO_ERROR,"pool GL validation failed");
        std::cout<<"PASS: real transient aliasing, overlapping/sampler separation, warm reuse, no-reuse, delayed GPU lease, immediate retirement, cancellation/encode/execute/fence/wait failure, rollback, idle eviction, close and domain rebuild\n";
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
        Require(compiled.finalVersions==std::vector<uint32_t>({1,1}),"content versions did not advance once per writing pass");
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
        reject([](auto& p){p.passes[1].accesses[0].expectedVersion=0;},"stale content version accepted");
        {auto current=plan;current.passes[1].accesses[0].expectedVersion=1;current.Compile();}
        reject([](auto& p){p.passes[0].accesses.push_back(p.passes[0].accesses[0]);},"overlapping writes accepted");
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
        RenderPlan aspects;const auto stencil=aspects.CreateTransient("depth/stencil",depth);
        aspects.passes.push_back(Node("clear-depth",{{stencil,{ResourceAspect::Depth},ResourceAccessType::Attachment,AttachmentLoad::Clear}}));
        aspects.passes.push_back(Node("load-stencil",{{stencil,{ResourceAspect::Stencil},ResourceAccessType::Attachment,AttachmentLoad::Load}}));
        bool missingStencil=false;try{aspects.Compile();}catch(const std::invalid_argument&){missingStencil=true;}Require(missingStencil,"clearing depth initialized independent stencil content");
        aspects.passes[0].accesses[0].range.aspect=ResourceAspect::DepthStencil;aspects.Compile();
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
    void ProductPlanValidation(const std::filesystem::path& output)
    {
        OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;
        OvWindowing::Context::Device device(ds);OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=96;ws.height=64;ws.samples=1;
        OvWindowing::Window window(device,ws);window.MakeCurrentContext();OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
        OvCore::ResourceManagement::ShaderManager shaders;const auto assets=std::filesystem::current_path()/"Data/Engine";
        shaders.ProvideAssetPaths(assets,assets);ServiceLocator::Provide(shaders);
        std::array<std::unique_ptr<baregl::Framebuffer>,4> targets;
        std::array<std::unique_ptr<OvCore::Rendering::PostProcess::PostProcessPipeline>,4> pipelines;
        std::array<std::vector<uint8_t>,4> expected,actual;
        std::array<baregl::Framebuffer::Snapshot,4> outputs;
        std::array<ResourceHandle,4> scratch;
        OvCore::Rendering::PostProcess::PostProcessSnapshot settings;settings.exposure.progressive=false;settings.bloom.passes=6;
        RenderPlan product;RenderCommandList commands;size_t independentTargets=0;
        for(size_t i=0;i<4;++i) {
            const uint16_t width=i==3?48:96,height=i==3?32:64;
            targets[i]=std::make_unique<baregl::Framebuffer>();OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*targets[i],width,height,false,false,false);
            pipelines[i]=std::make_unique<OvCore::Rendering::PostProcess::PostProcessPipeline>();
            const float color[]={.2f+float(i)*.1f,.8f,1.5f,1};glClearNamedFramebufferfv(targets[i]->GetID(),GL_COLOR,0,color);
            auto oracle=pipelines[i]->Record(settings,*targets[i],driver.CreatePipelineState(),false);oracle.Execute(driver);
            expected[i].resize(size_t(width)*height*4);targets[i]->RecordTarget().ReadRGBA8(expected[i]);
            glClearNamedFramebufferfv(targets[i]->GetID(),GL_COLOR,0,color);
            RenderPlan fragment;const auto texture=targets[i]->RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR);
            fragment.Import("Viewport.Color",texture->RetainStorage(),ResourceOrigin::Imported,{{}});
            RenderCommandList history;pipelines[i]->Build(fragment,history,settings,*targets[i],driver.CreatePipelineState());
            scratch[i]=fragment.Handle(1);const auto compiled=fragment.Compile();
            for(const auto& resource:fragment.resources)if(resource.origin==ResourceOrigin::Transient)++independentTargets;
            outputs[i]=targets[i]->RecordTarget();product.Append(std::move(fragment),0,0);commands.AppendRecording(std::move(history));
        }
        RenderPlanPass consumer=Node("UI.FinalViewportConsumer",{});
        for(size_t i=0;i<4;++i) {
            const auto texture=targets[i]->RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR);
            consumer.accesses.push_back({*product.Find(texture->RetainStorage()),{},ResourceAccessType::Sample});actual[i].resize(expected[i].size());
        }
        consumer.operation=[outputs,&actual] {return [outputs,&actual](auto&) {for(size_t i=0;i<4;++i)outputs[i].ReadRGBA8(actual[i]);};};
        product.passes.push_back(std::move(consumer));const auto compiled=product.Compile();
        OvRendering::Core::RenderTargetPool pool;auto mapping=pool.Realize(product,compiled);
        Require(mapping->PhysicalID(scratch[0])==mapping->PhysicalID(scratch[1])&&mapping->PhysicalID(scratch[1])==mapping->PhysicalID(scratch[2]),"product did not alias completed viewport scratch");
        Require(pool.GetStatistics().allocations<independentTargets&&pool.GetStatistics().aliases>0,"product kept every private viewport temporary");
        auto forged=scratch[0];++forged.generation;bool stale=false;try{mapping->PhysicalID(forged);}catch(const std::invalid_argument&){stale=true;}Require(stale,"fragment generation validation was lost during composition");
        std::ofstream(output/"product-plan.txt")<<compiled.Dump()<<mapping->Dump();
        // Frozen sampler owners and outputs survive changes before Worker encoding.
        for(auto& target:targets){target->Resize(8,8);target.reset();}
        product.RecordOperations(commands,{},mapping);mapping.reset();
        OvTools::Jobs::JobSystem jobs(2);commands.Seal(&jobs);RenderFrame frame;frame.Append(std::move(commands));frame.Seal();frame.Submit(driver);frame.Wait();frame.GetCompletion().RethrowIfFailed();
        for(size_t i=0;i<4;++i)Require(actual[i]==expected[i],"product composition changed a viewport's exact output");
        Require(pool.GetStatistics().leasedTargets==0&&glGetError()==GL_NO_ERROR,"product did not retire shared scratch");
        // A later fragment cannot declare a missing earlier producer initialized.
        RenderPlan before;const auto outputHandle=before.ImportBackbuffer(96,64,false);before.passes.push_back(Node("before",{}));
        RenderPlan after;const auto later=after.ImportBackbuffer(96,64,true);after.passes.push_back(Node("present",{{later,{},ResourceAccessType::Present}}));
        RenderPlan missing;missing.Append(std::move(before),0,0);missing.Append(std::move(after),0,0);
        bool rejected=false;try{missing.Compile();}catch(const std::invalid_argument&){rejected=true;}Require(rejected,"later product import promoted uninitialized content");
        (void)outputHandle;
        RenderPlan initial;const auto first=initial.ImportBackbuffer(96,64,false);
        initial.passes.push_back(Node("first-clear",{{first,{},ResourceAccessType::Attachment,AttachmentLoad::Clear}}));
        RenderPlan laterVersion;const auto second=laterVersion.ImportBackbuffer(96,64,true);
        ResourceAccess load{second,{},ResourceAccessType::Attachment,AttachmentLoad::Load};load.expectedVersion=0;
        laterVersion.passes.push_back(Node("later-write",{load}));
        ResourceAccess present{second,{},ResourceAccessType::Present};present.expectedVersion=1;
        laterVersion.passes.push_back(Node("later-present",{present}));
        RenderPlan versioned;versioned.Append(std::move(initial),0,0);versioned.Append(std::move(laterVersion),0,0);
        Require(versioned.Compile().finalVersions[0]==2,"fragment content versions did not account for earlier product writes");
        std::cout<<"PASS: four viewport chains share physical scratch in one ordered plan; exact no-reuse images, final UI reads, frozen resize/destruction, fragment generations, GPU retirement and import promotion rejection\n";
    }
    void PostProcessValidation(const std::filesystem::path& output)
    {
        OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;
        OvWindowing::Context::Device device(ds);OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=96;ws.height=64;ws.samples=1;
        OvWindowing::Window window(device,ws);window.MakeCurrentContext();OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
        OvCore::ResourceManagement::ShaderManager shaders;const auto assets=std::filesystem::current_path()/"Data/Engine";
        shaders.ProvideAssetPaths(assets,assets);ServiceLocator::Provide(shaders);
        OvCore::Rendering::PostProcess::PostProcessPipeline pipeline;
        auto postPool=std::make_shared<OvRendering::Core::RenderTargetPool>();
        pipeline.SetResourcePool(postPool);
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
            if(!mask) {
                Require(plan.passes.size()==2&&plan.resources.size()==2,"disabled effects still emit nodes/resources");
                Require(plan.resources[1].origin==ResourceOrigin::Transient,"postprocess still owns a ping-pong cache");
            }
            for(const auto& resource:plan.resources)if(resource.origin!=ResourceOrigin::Imported&&resource.origin!=ResourceOrigin::History)
                Require(resource.origin==ResourceOrigin::Transient&&!resource.storage.GetID(),"effect scratch storage was allocated during Build");
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
            auto mapping=postPool->Realize(plan,compiled);plan.RecordOperations(commands,{},std::move(mapping));commands.Seal();RenderFrame frame;frame.Append(std::move(commands));frame.Seal();frame.Submit(driver);frame.Wait();frame.GetCompletion().RethrowIfFailed();
            std::vector<float> pixels(size_t(size.first)*size.second*4);const auto texture=target.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR);
            glGetTextureImage(texture->GetID(),0,GL_RGBA,GL_FLOAT,static_cast<GLsizei>(pixels.size()*sizeof(float)),pixels.data());
            Require(std::all_of(pixels.begin(),pixels.end(),[](float value){return std::isfinite(value);})&&glGetError()==GL_NO_ERROR,"postprocess boundary produced invalid pixels/GL state");
            // Disable both interval aliasing and inter-frame reuse on the same algorithms.
            glClearNamedFramebufferfv(target.GetID(),GL_COLOR,0,color);
            auto diagnostic=pipeline.Record(settings,target,driver.CreatePipelineState(),false);diagnostic.Execute(driver);
            std::vector<float> reference(pixels.size());
            glGetTextureImage(texture->GetID(),0,GL_RGBA,GL_FLOAT,static_cast<GLsizei>(reference.size()*sizeof(float)),reference.data());
            Require(std::memcmp(pixels.data(),reference.data(),pixels.size()*sizeof(float))==0,"pooled postprocess differs from no-reuse output");
            const auto allocations=postPool->GetStatistics().allocations;
            glClearNamedFramebufferfv(target.GetID(),GL_COLOR,0,color);
            auto warm=pipeline.Record(settings,target,driver.CreatePipelineState());warm.Execute(driver);
            Require(postPool->GetStatistics().allocations==allocations&&postPool->GetStatistics().leasedTargets==0,"warm effect chain allocated or retained a retired lease");
        }
        // A viewport resize/destruction must not mutate a submitted effect chain.
        OvCore::Rendering::PostProcess::PostProcessSnapshot settings;
        settings.exposure.enabled=false;settings.bloom.enabled=true;settings.tonemapping.enabled=true;settings.fxaa.enabled=true;
        const float color[]={.4f,.8f,1.5f,1};
        glClearNamedFramebufferfv(target.GetID(),GL_COLOR,0,color);
        auto referenceCommands=pipeline.Record(settings,target,driver.CreatePipelineState(),false);referenceCommands.Execute(driver);
        std::vector<uint8_t> expected(96*64*4);target.RecordTarget().ReadRGBA8(expected);
        glClearNamedFramebufferfv(target.GetID(),GL_COLOR,0,color);
        const auto frozenOutput=target.RecordTarget();
        auto old=pipeline.Record(settings,target,driver.CreatePipelineState());
        RenderFrame inFlight;inFlight.Append(std::move(old));inFlight.Seal();inFlight.Submit(driver);
        const auto pinned=postPool->GetStatistics();Require(pinned.leasedTargets>0,"submitted effect targets escaped their GPU lease");
        target.Resize(48,32);glClearNamedFramebufferfv(target.GetID(),GL_COLOR,0,color);
        const auto before=postPool->GetStatistics().allocations;
        auto canceled=pipeline.Record(settings,target,driver.CreatePipelineState());
        Require(postPool->GetStatistics().allocations>before,"resized effect chain reused incompatible/in-flight targets");
        canceled.Cancel();Require(postPool->GetStatistics().leasedTargets==pinned.leasedTargets,"cancel released another frame's effect lease");
        inFlight.Wait();inFlight.GetCompletion().RethrowIfFailed();
        std::vector<uint8_t> actual(expected.size());frozenOutput.ReadRGBA8(actual);
        Require(actual==expected&&postPool->GetStatistics().leasedTargets==0,"resized viewport corrupted frozen output or retained retired effects");
        std::cout<<"PASS: effect output survives in-flight viewport resize; cancellation preserves older GPU leases\n";
        std::cout<<"PASS: 16 postprocess effect combinations x 1x1/thin/odd/normal extents, Bloom pass bounds/Load blending, explicit luminance mip/history nodes, exact no-reuse pixels and warm zero allocation\n";
    }
}
int main(int argc,char** argv) try
{
    Require(argc==2,"expected evidence directory");ContractValidation(argv[1]);OperationValidation();PoolValidation(argv[1]);ProductPlanValidation(argv[1]);PostProcessValidation(argv[1]);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
