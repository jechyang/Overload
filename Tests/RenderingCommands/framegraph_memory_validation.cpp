// Measure actual GL texture levels and Windows private bytes independently.
// No scene copies or fixture meshes are needed to isolate viewport/effect storage.
#include <iostream>
#define main UnusedSkinningValidationMain
#include "../Skinning/serial_validation.cpp"
#undef main
#include <OvCore/Rendering/PostProcess/PostProcessPipeline.h>
#include <OvCore/Rendering/PostProcess/PostProcessSnapshot.h>
#include <OvRendering/Utils/ResourceTracking.h>
#define NOMINMAX
#undef APIENTRY
#include <Windows.h>
#include <psapi.h>
#include <iomanip>

int main(int argc, char** argv)
{
    try {
        Require(argc==2,"expected evidence directory");
        const std::filesystem::path output=argv[1]; std::filesystem::create_directories(output);
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
        const auto sample=[&](const char* phase) {
            glFinish(); PROCESS_MEMORY_COUNTERS_EX process{}; process.cb=sizeof(process);
            Require(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process),sizeof(process))!=0,"process memory query failed");
            uint64_t total=0; size_t count=0;
            for(const auto& texture:OvRendering::Utils::ResourceTracking::GetTextures()) {
                if(!texture.allocated) continue;
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
            const bool sharedMode=GetEnvironmentVariableA("OV_FRAMEGRAPH_MEMORY_SHARED64",nullptr,0)>0;
            const uint64_t budget=sharedMode?64ull*1024*1024:256ull*1024*1024;
            std::vector<std::shared_ptr<OvRendering::Core::RenderTargetPool>> pools;
            const auto sharedPool=sharedMode?std::make_shared<OvRendering::Core::RenderTargetPool>(budget):nullptr;
            std::ofstream(output/"pool-mode.txt")<<(sharedMode?"shared pool, 64 MiB idle budget":"per-viewport pools, default 256 MiB idle budget each")<<'\n';
            std::ofstream poolReport(output/"pool.csv");poolReport<<"phase,allocations,hits,aliases,evictions,idleBytes,leasedBytes,peakLeasedBytes\n";
            const auto samplePool=[&](const char* phase) {
                OvRendering::Core::RenderTargetPool::Statistics total;
                for(const auto& pool:pools) {
                    const auto s=pool->GetStatistics();total.allocations+=s.allocations;total.hits+=s.hits;total.aliases+=s.aliases;
                    total.evictions+=s.evictions;total.idleBytes+=s.idleBytes;total.leasedBytes+=s.leasedBytes;total.peakLeasedBytes+=s.peakLeasedBytes;
                    Require(s.leasedBytes==0&&s.idleBytes<=budget,"retired effects exceed pool idle budget");
                }
                poolReport<<phase<<','<<total.allocations<<','<<total.hits<<','<<total.aliases<<','<<total.evictions<<','<<total.idleBytes<<','<<total.leasedBytes<<','<<total.peakLeasedBytes<<'\n';poolReport.flush();
            };
            std::array<std::unique_ptr<OvCore::Rendering::PostProcess::PostProcessPipeline>,3> pipelines;
            for(auto& pipeline:pipelines) {
                auto pool=sharedMode?sharedPool:std::make_shared<OvRendering::Core::RenderTargetPool>(budget);
                if(!sharedMode||pools.empty())pools.push_back(pool);
                pipeline=std::make_unique<OvCore::Rendering::PostProcess::PostProcessPipeline>();pipeline->SetResourcePool(pool);
            }
            sample("three-cold-pipelines");samplePool("three-cold-pipelines");
            baregl::Framebuffer target("MemoryTarget"); OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,1920,1080,false,false,false);
            const float initialColor[]={.4f,.8f,1.5f,1};glClearNamedFramebufferfv(target.GetID(),GL_COLOR,0,initialColor);
            OvCore::Rendering::PostProcess::PostProcessSnapshot settings;
            settings.exposure.enabled=settings.bloom.enabled=settings.tonemapping.enabled=settings.fxaa.enabled=false;
            for(auto& pipeline:pipelines) { auto commands=pipeline->Record(settings,target,driver.CreatePipelineState()); commands.Execute(driver); }
            sample("three-disabled-1080p");samplePool("three-disabled-1080p");
            settings.bloom.enabled=true;
            for(auto& pipeline:pipelines) { auto commands=pipeline->Record(settings,target,driver.CreatePipelineState()); commands.Execute(driver); }
            sample("three-bloom-1080p");samplePool("three-bloom-1080p");
            settings.exposure.enabled=true; settings.exposure.progressive=false;
            for(auto& pipeline:pipelines) { auto commands=pipeline->Record(settings,target,driver.CreatePipelineState()); commands.Execute(driver); }
            sample("three-exposure-bloom-1080p");samplePool("three-exposure-bloom-1080p");
            settings.exposure.enabled=settings.bloom.enabled=false;
            for(auto& pipeline:pipelines) { auto commands=pipeline->Record(settings,target,driver.CreatePipelineState()); commands.Execute(driver); }
            sample("disabled-after-enabled");samplePool("disabled-after-enabled");
            for(const auto& pool:pools)pool->SetIdleBudget(0);sample("idle-cache-trimmed");samplePool("idle-cache-trimmed");
        }
        sample("released");
        Require(glGetError()==GL_NO_ERROR,"memory measurement issued invalid GL operations");
        std::cout<<"PASS: isolated viewport texture/process memory measurement\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
