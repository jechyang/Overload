// Measure actual GL texture levels and Windows private bytes independently.
// No scene copies or fixture meshes are needed to isolate viewport/effect storage.
#include <iostream>
#define main UnusedSkinningValidationMain
#include "../Skinning/serial_validation.cpp"
#undef main
#include <OvCore/Rendering/PostProcess/PostProcessPipeline.h>
#include <OvCore/Rendering/PostProcess/PostProcessSnapshot.h>
#include <OvRendering/Utils/ResourceTracking.h>
#include <OvRendering/Data/RenderFrame.h>
#define NOMINMAX
#undef APIENTRY
#include <Windows.h>
#include <psapi.h>
#include <iomanip>
#include <unordered_set>

int main(int argc, char** argv)
{
    try {
        Require(argc==2,"expected evidence directory");
        const std::filesystem::path output=argv[1]; std::filesystem::create_directories(output);
        const auto enabled=[](const char* name){char value[2]{};return GetEnvironmentVariableA(name,value,sizeof(value))==1&&value[0]=='1';};
        OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion=4; ds.contextMinorVersion=5;
        OvWindowing::Context::Device device(ds);
        OvWindowing::Settings::WindowSettings ws; ws.visible=false; ws.focused=false; ws.width=64; ws.height=64; ws.samples=1;
        OvWindowing::Window window(device,ws); window.MakeCurrentContext();
        OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
        OvRendering::Utils::ResourceTracking::TrackResources();
        OvCore::ResourceManagement::ShaderManager shaders;
        const auto assets=std::filesystem::current_path()/"Data/Engine";
        shaders.ProvideAssetPaths(assets,assets); ServiceLocator::Provide(shaders);
        std::ofstream report(output/"memory.csv"), inventory(output/"textures.csv");
        report<<"phase,privateBytes,workingSet,textureCount,allocatedTextureBytes\n";
        inventory<<"phase,name,id,width,height,levels,bytes\n";
        const size_t pipelineCount=enabled("OV_FRAMEGRAPH_MEMORY_SINGLE")?1:3;
        const auto label=[&](std::string phase){if(pipelineCount==1&&phase.starts_with("three-"))phase.replace(0,6,"one-");return phase;};
        const auto sample=[&](const char* phase) {
            const auto phaseName=label(phase);phase=phaseName.c_str();
            glFinish(); PROCESS_MEMORY_COUNTERS_EX process{}; process.cb=sizeof(process);
            Require(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process),sizeof(process))!=0,"process memory query failed");
            uint64_t total=0; size_t count=0;
            std::unordered_set<uint32_t> measured;
            for(const auto& texture:OvRendering::Utils::ResourceTracking::GetTextures()) {
                if(!texture.allocated||!measured.insert(texture.id).second) continue;
                uint64_t bytes=0; unsigned levels=0;
                for(unsigned level=0;level<32;++level) {
                    GLint width=0,height=0,red=0,green=0,blue=0,alpha=0,depth=0;
                    glGetTextureLevelParameteriv(texture.id,level,GL_TEXTURE_WIDTH,&width);
                    glGetTextureLevelParameteriv(texture.id,level,GL_TEXTURE_HEIGHT,&height);
                    if(!width||!height) break;
                    glGetTextureLevelParameteriv(texture.id,level,GL_TEXTURE_RED_SIZE,&red);
                    glGetTextureLevelParameteriv(texture.id,level,GL_TEXTURE_GREEN_SIZE,&green);
                    glGetTextureLevelParameteriv(texture.id,level,GL_TEXTURE_BLUE_SIZE,&blue);
                    glGetTextureLevelParameteriv(texture.id,level,GL_TEXTURE_ALPHA_SIZE,&alpha);
                    glGetTextureLevelParameteriv(texture.id,level,GL_TEXTURE_DEPTH_SIZE,&depth);
                    bytes+=uint64_t(width)*height*(red+green+blue+alpha+depth)/8*(texture.type==baregl::types::ETextureType::TEXTURE_CUBE?6:1); ++levels;
                }
                total+=bytes; ++count;
                inventory<<phase<<','<<std::quoted(texture.name)<<','<<texture.id<<','<<texture.width<<','<<texture.height<<','<<levels<<','<<bytes<<'\n';
            }
            report<<phase<<','<<process.PrivateUsage<<','<<process.WorkingSetSize<<','<<count<<','<<total<<'\n'; report.flush(); inventory.flush();
            std::cout<<"MEMORY "<<phase<<" private="<<process.PrivateUsage<<" textures="<<total<<" count="<<count<<'\n';
        };
        sample("context");
        {
            const bool productMode=enabled("OV_FRAMEGRAPH_MEMORY_PRODUCT");
            const bool sharedMode=productMode||enabled("OV_FRAMEGRAPH_MEMORY_SHARED64");
            const uint64_t budget=sharedMode?64ull*1024*1024:256ull*1024*1024;
            std::vector<std::shared_ptr<OvRendering::Core::RenderTargetPool>> pools;
            const auto sharedPool=sharedMode?std::make_shared<OvRendering::Core::RenderTargetPool>(budget):nullptr;
            if(productMode)sharedPool->SetIdleBudget(256ull*1024*1024);
            const bool twoSets=productMode&&enabled("OV_FRAMEGRAPH_MEMORY_RESERVE2");
            const bool distinctOutputs=productMode&&enabled("OV_FRAMEGRAPH_MEMORY_DISTINCT_OUTPUTS");
            if(twoSets)sharedPool->SetFrameReserve(2);
            std::ofstream(output/"pool-mode.txt")<<(productMode?"one product graph and pool, default 256 MiB idle budget":sharedMode?"shared pool, 64 MiB idle budget":"per-viewport pools, default 256 MiB idle budget each")<<"; frame reserve="<<(twoSets?2:1)<<"; pipeline count="<<pipelineCount<<"; distinct outputs="<<distinctOutputs<<'\n';
            std::ofstream poolReport(output/"pool.csv");poolReport<<"phase,allocations,hits,aliases,evictions,idleBytes,leasedBytes,peakLeasedBytes\n";
            const auto samplePool=[&](const char* phase) {
                const auto phaseName=label(phase);phase=phaseName.c_str();
                OvRendering::Core::RenderTargetPool::Statistics total;
                for(const auto& pool:pools) {
                    const auto s=pool->GetStatistics();total.allocations+=s.allocations;total.hits+=s.hits;total.aliases+=s.aliases;
                    total.evictions+=s.evictions;total.idleBytes+=s.idleBytes;total.leasedBytes+=s.leasedBytes;total.peakLeasedBytes+=s.peakLeasedBytes;
                    Require(s.idleBytes<=(productMode?256ull*1024*1024:budget),"effects exceed pool idle budget");
                }
                poolReport<<phase<<','<<total.allocations<<','<<total.hits<<','<<total.aliases<<','<<total.evictions<<','<<total.idleBytes<<','<<total.leasedBytes<<','<<total.peakLeasedBytes<<'\n';poolReport.flush();
            };
            std::vector<std::unique_ptr<OvCore::Rendering::PostProcess::PostProcessPipeline>> pipelines(pipelineCount);
            for(auto& pipeline:pipelines) {
                auto pool=sharedMode?sharedPool:std::make_shared<OvRendering::Core::RenderTargetPool>(budget);
                if(!sharedMode||pools.empty())pools.push_back(pool);
                pipeline=std::make_unique<OvCore::Rendering::PostProcess::PostProcessPipeline>();pipeline->SetResourcePool(pool);
            }
            sample("three-cold-pipelines");samplePool("three-cold-pipelines");
            baregl::Framebuffer target("MemoryTarget"); OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,1920,1080,false,false,false);
            const float initialColor[]={.4f,.8f,1.5f,1};glClearNamedFramebufferfv(target.GetID(),GL_COLOR,0,initialColor);
            std::vector<std::unique_ptr<baregl::Framebuffer>> outputOwners;
            std::vector<baregl::Framebuffer*> outputTargets(pipelineCount,&target);
            if(distinctOutputs)for(size_t i=1;i<pipelineCount;++i) {
                auto extra=std::make_unique<baregl::Framebuffer>("MemoryViewport"+std::to_string(i));
                OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*extra,1920,1080,false,false,false);
                glClearNamedFramebufferfv(extra->GetID(),GL_COLOR,0,initialColor);
                outputTargets[i]=extra.get();outputOwners.push_back(std::move(extra));
            }
            const auto resize=[&](uint32_t width,uint32_t height) {
                OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,width,height,false,false,false);
                glClearNamedFramebufferfv(target.GetID(),GL_COLOR,0,initialColor);
                for(auto& extra:outputOwners){OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*extra,width,height,false,false,false);glClearNamedFramebufferfv(extra->GetID(),GL_COLOR,0,initialColor);}
            };
            OvCore::Rendering::PostProcess::PostProcessSnapshot settings;
            std::ofstream timings(output/"product-timings.csv");timings<<"phase,sample,compileUs,realizeUs,encodeExecuteUs,logicalTransientBytes,estimatedPeakBytes,physicalAllocations\n";
            const auto render=[&](const char* phase) {
                if(!productMode) {for(size_t i=0;i<pipelines.size();++i){auto commands=pipelines[i]->Record(settings,*outputTargets[i],driver.CreatePipelineState());commands.Execute(driver);}return;}
                for(unsigned iteration=0;iteration<6;++iteration) {
                    OvRendering::Data::RenderPlan product;OvRendering::Data::RenderCommandList commands;
                    for(size_t i=0;i<pipelines.size();++i) {
                        auto& pipeline=pipelines[i];auto& outputTarget=*outputTargets[i];
                        OvRendering::Data::RenderPlan fragment;
                        fragment.Import("Viewport.Output",outputTarget.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR)->RetainStorage(),OvRendering::Data::ResourceOrigin::Imported,{{}});
                        pipeline->Build(fragment,commands,settings,outputTarget,driver.CreatePipelineState());product.Append(std::move(fragment),0,0);
                    }
                    const auto start=std::chrono::steady_clock::now();const auto compiled=product.Compile();const auto compiledAt=std::chrono::steady_clock::now();
                    const auto before=sharedPool->GetStatistics().allocations;auto mapping=sharedPool->Realize(product,compiled);const auto realizedAt=std::chrono::steady_clock::now();
                    product.RecordOperations(commands,{},std::move(mapping));commands.Seal();commands.Execute(driver);const auto finished=std::chrono::steady_clock::now();
                    uint64_t logical=0;for(size_t i=0;i<product.resources.size();++i)if(product.resources[i].origin==OvRendering::Data::ResourceOrigin::Transient)logical+=compiled.lifetimes[i].estimatedBytes;
                    const auto us=[](auto a,auto b){return std::chrono::duration<double,std::micro>(b-a).count();};
                    timings<<phase<<','<<iteration<<','<<us(start,compiledAt)<<','<<us(compiledAt,realizedAt)<<','<<us(realizedAt,finished)<<','<<logical<<','<<compiled.estimatedPeakBytes<<','<<sharedPool->GetStatistics().allocations-before<<'\n';
                    if(iteration)Require(sharedPool->GetStatistics().allocations==before,"warm unified product allocated another transient");
                }
            };
            settings.exposure.enabled=settings.bloom.enabled=settings.tonemapping.enabled=settings.fxaa.enabled=false;
            render("disabled");
            sample("three-disabled-1080p");samplePool("three-disabled-1080p");
            settings.bloom.enabled=true;
            render("bloom");
            sample("three-bloom-1080p");samplePool("three-bloom-1080p");
            settings.exposure.enabled=true; settings.exposure.progressive=false;
            render("exposure-bloom");
            sample("three-exposure-bloom-1080p");samplePool("three-exposure-bloom-1080p");
            if(productMode) {
                settings.tonemapping.enabled=settings.fxaa.enabled=true;
                render("all-effects");sample("three-all-effects-1080p");samplePool("three-all-effects-1080p");
            }
            settings.exposure.enabled=settings.bloom.enabled=settings.tonemapping.enabled=settings.fxaa.enabled=false;
            render("disabled-after-enabled");
            sample("disabled-after-enabled");samplePool("disabled-after-enabled");
            if(productMode) {
                settings.exposure.enabled=settings.bloom.enabled=settings.tonemapping.enabled=settings.fxaa.enabled=true;
                const auto build=[&] {
                    OvRendering::Data::RenderPlan product;
                    OvRendering::Data::RenderCommandList commands;
                    for(size_t i=0;i<pipelines.size();++i) {
                        auto& pipeline=pipelines[i];auto& outputTarget=*outputTargets[i];
                        OvRendering::Data::RenderPlan fragment;
                        fragment.Import("Viewport.Output",outputTarget.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR)->RetainStorage(),OvRendering::Data::ResourceOrigin::Imported,{{}});
                        pipeline->Build(fragment,commands,settings,outputTarget,driver.CreatePipelineState());product.Append(std::move(fragment),0,0);
                    }
                    const auto compiled=product.Compile();auto mapping=sharedPool->Realize(product,compiled);
                    product.RecordOperations(commands,{},std::move(mapping));commands.Seal();return commands;
                };
                OvRendering::Data::RenderFrame first,second;
                first.Append(build());first.Seal();Require(first.Submit(driver),"memory first product submission failed");
                second.Append(build());second.Seal();Require(second.Submit(driver),"memory second product submission failed");
                sample("two-submitted-1080p");samplePool("two-submitted-1080p");
                Require(sharedPool->GetStatistics().leasedBytes>0,"in-flight memory sample lost pool leases");
                first.Wait();second.Wait();
                Require(sharedPool->GetStatistics().leasedBytes==0,"two retired memory frames retained scratch");
                OvRendering::Data::RenderFrame oldSize,newSize;
                oldSize.Append(build());oldSize.Seal();Require(oldSize.Submit(driver),"old resize product submission failed");
                resize(1280,720);
                newSize.Append(build());newSize.Seal();Require(newSize.Submit(driver),"new resize product submission failed");
                sample("resize-two-submitted");samplePool("resize-two-submitted");
                oldSize.Wait();newSize.Wait();
                render("all-effects-720p");sample("three-all-effects-720p");samplePool("three-all-effects-720p");
                resize(1920,1080);
                render("all-effects-restored-1080p");sample("three-all-effects-restored-1080p");samplePool("three-all-effects-restored-1080p");
                Require(sharedPool->GetStatistics().leasedBytes==0,"retired memory frame retained scratch");
            }
            for(const auto& pool:pools)pool->SetIdleBudget(0);sample("idle-cache-trimmed");samplePool("idle-cache-trimmed");
        }
        sample("released");
        Require(glGetError()==GL_NO_ERROR,"memory measurement issued invalid GL operations");
        std::cout<<"PASS: isolated viewport texture/process memory measurement\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
