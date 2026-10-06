// Compare the same frozen product operations and exposure history inputs.
// Only the luminance descriptor differs; wall-clock adaptation parameters do not.
void LuminanceValidation(const std::filesystem::path& output)
{
    using namespace baregl::types;
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);OvWindowing::Settings::WindowSettings ws;
    ws.visible=false;ws.focused=false;ws.width=96;ws.height=64;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();
    OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
    OvCore::ResourceManagement::ShaderManager shaders;const auto assets=std::filesystem::current_path()/"Data/Engine";
    shaders.ProvideAssetPaths(assets,assets);ServiceLocator::Provide(shaders);
    OvRendering::Core::RenderTargetPool pool;OvTools::Jobs::JobSystem jobs(2);
    std::ofstream report(output/"luminance-formats.csv");
    report<<"width,height,pattern,frame,progressive,r32Bytes,rgba32Bytes,mips,exactLuminance,exactHistory,exactOutput\n";
    struct Capture {std::vector<float> luminance,history,pixels;uint64_t bytes=0;};
    const auto read=[](GLuint id,unsigned level,GLenum format,unsigned channels) {
        GLint width=0,height=0;glGetTextureLevelParameteriv(id,level,GL_TEXTURE_WIDTH,&width);
        glGetTextureLevelParameteriv(id,level,GL_TEXTURE_HEIGHT,&height);
        std::vector<float> values(size_t(width)*height*channels);
        glGetTextureImage(id,level,format,GL_FLOAT,static_cast<GLsizei>(values.size()*sizeof(float)),values.data());return values;
    };
    const auto equal=[](const auto& a,const auto& b){return a.size()==b.size()&&std::memcmp(a.data(),b.data(),a.size()*sizeof(float))==0;};
    size_t cases=0;
    for(const auto size:{std::pair<uint16_t,uint16_t>{1,1},{1,32},{63,17},{96,64}})for(unsigned pattern=0;pattern<5;++pattern) {
        baregl::Framebuffer target;OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,size.first,size.second,false,false,false);
        const auto texture=target.RetainTextureAttachment(EFramebufferAttachment::COLOR);
        OvCore::Rendering::PostProcess::PostProcessPipeline pipeline;
        OvCore::Rendering::PostProcess::PostProcessSnapshot settings;
        settings.bloom.enabled=settings.tonemapping.enabled=settings.fxaa.enabled=false;
        settings.exposure.centerWeightBias=float(pattern%3)*.5f;
        settings.exposure.exposureCompensationEV=pattern==4?2.f:0.f;
        settings.exposure.speedUp=3.f;settings.exposure.speedDown=.5f;
        for(unsigned frame=0;frame<4;++frame) {
            settings.exposure.progressive=frame>0;
            std::vector<float> input(size_t(size.first)*size.second*4);
            for(size_t pixel=0;pixel<input.size()/4;++pixel) {
                const float gradient=float((pixel*73+frame*17)%257)/256.f;
                const float brightness=frame==2?.001f:frame==3?32.f:1.f;
                const float value=pattern==0?0.f:pattern==1?1e-6f:pattern==2?gradient:pattern==3?gradient*10000.f:.25f;
                input[pixel*4]=value*brightness;
                input[pixel*4+1]=value*brightness*(pattern==4?4.f:.5f);
                input[pixel*4+2]=value*brightness*(pattern==4?.125f:2.f);input[pixel*4+3]=1.f;
            }
            RenderPlan plan;plan.Import("input",texture->RetainStorage(),ResourceOrigin::Imported,{{}});
            RenderCommandList history;pipeline.Build(plan,history,settings,target,driver.CreatePipelineState());
            const auto found=std::find_if(plan.resources.begin(),plan.resources.end(),[](const auto& r){return r.name=="Exposure.Luminance";});
            Require(found!=plan.resources.end(),"exposure omitted luminance resource");
            const auto index=static_cast<size_t>(found-plan.resources.begin());const auto handle=plan.Handle(index);
            const auto& desc=*found->descriptor;
            Require(desc.texture.internalFormat==EInternalFormat::R32F&&desc.texture.mutableDesc->format==EFormat::RED&&desc.mips==11&&desc.texture.width==1024&&desc.texture.height==1024,"production luminance lost its R32F precision/extent/mips");
            auto legacy=plan;legacy.resources[index].descriptor->texture.internalFormat=EInternalFormat::RGBA32F;
            legacy.resources[index].descriptor->texture.mutableDesc->format=EFormat::RGBA;
            Require(legacy.resources[index].descriptor->EstimatedBytes()==desc.EstimatedBytes()*4,"luminance descriptor did not reduce storage by 75 percent");
            const auto execute=[&](const RenderPlan& variant,RenderCommandList commands,bool redOnly) {
                texture->Upload(input.data(),EFormat::RGBA,EPixelDataType::FLOAT);
                const auto compiled=variant.Compile();auto mapping=pool.Realize(variant,compiled);
                variant.RecordOperations(commands,{},mapping);commands.Seal(&jobs);
                RenderFrame submitted;submitted.Append(std::move(commands));submitted.Seal();
                Require(submitted.Submit(driver),"luminance comparison submission failed");submitted.Wait();submitted.GetCompletion().RethrowIfFailed();
                Capture result;const auto id=mapping->TextureStorage(handle).GetID();
                for(unsigned level=0;level<desc.mips;++level) {
                    auto values=read(id,level,GL_RED,1);result.luminance.insert(result.luminance.end(),values.begin(),values.end());
                    GLint width=0,height=0,red=0,green=0,blue=0,alpha=0,format=0;
                    glGetTextureLevelParameteriv(id,level,GL_TEXTURE_WIDTH,&width);glGetTextureLevelParameteriv(id,level,GL_TEXTURE_HEIGHT,&height);
                    glGetTextureLevelParameteriv(id,level,GL_TEXTURE_RED_SIZE,&red);glGetTextureLevelParameteriv(id,level,GL_TEXTURE_GREEN_SIZE,&green);
                    glGetTextureLevelParameteriv(id,level,GL_TEXTURE_BLUE_SIZE,&blue);glGetTextureLevelParameteriv(id,level,GL_TEXTURE_ALPHA_SIZE,&alpha);
                    glGetTextureLevelParameteriv(id,level,GL_TEXTURE_INTERNAL_FORMAT,&format);
                    Require(red==32&&green==(redOnly?0:32)&&blue==(redOnly?0:32)&&alpha==(redOnly?0:32)&&format==(redOnly?GL_R32F:GL_RGBA32F),"GL luminance allocation differs from the declared format");
                    result.bytes+=uint64_t(width)*height*(red+green+blue+alpha)/8;
                }
                for(size_t i=0;i<variant.resources.size();++i)if(variant.resources[i].origin==ResourceOrigin::History) {
                    auto values=read(mapping->TextureStorage(variant.Handle(i)).GetID(),0,GL_RGBA,4);
                    result.history.insert(result.history.end(),values.begin(),values.end());
                }
                result.pixels=read(texture->GetID(),0,GL_RGBA,4);
                Require(std::all_of(result.pixels.begin(),result.pixels.end(),[](float value){return std::isfinite(value);}),"exposure produced nonfinite HDR pixels");
                return result;
            };
            // Both executions read the same previous ping-pong history and
            // write the same current history, with identical frozen uniforms.
            const auto reference=execute(legacy,RenderCommandList{},false);
            const auto actual=execute(plan,std::move(history),true);
            Require(equal(actual.luminance,reference.luminance),"R32F changed a luminance mip's red float bits");
            Require(equal(actual.history,reference.history),"R32F changed initial/progressive exposure history");
            Require(equal(actual.pixels,reference.pixels),"R32F changed final HDR output float bits");
            Require(actual.bytes==desc.EstimatedBytes()&&reference.bytes==actual.bytes*4&&pool.GetStatistics().leasedTargets==0,"actual GL bytes or GPU retirement differ from luminance expectations");
            report<<size.first<<','<<size.second<<','<<pattern<<','<<frame<<','<<settings.exposure.progressive<<','<<actual.bytes<<','<<reference.bytes<<','<<desc.mips<<",1,1,1\n";++cases;
        }
    }
    Require(glGetError()==GL_NO_ERROR,"luminance format validation left a GL error");
    std::printf("PASS: %zu frozen R32F/RGBA32F exposure comparisons: every mip red float bit, initial/progressive history and final HDR float bit equal; actual GL storage 5592404 vs 22369616 bytes; Worker encoding and GPU retirement\n",cases);
}
