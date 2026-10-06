    void PreviewValidation(const std::filesystem::path& output)
    {
        OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;
        OvWindowing::Context::Device device(ds);OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=64;ws.height=32;ws.samples=1;
        OvWindowing::Window window(device,ws);window.MakeCurrentContext();OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
        baregl::DeletionQueue domain;OvRendering::Core::RenderTargetPool pool;pool.SetFrameReserve(1);
        const auto readImage=[](const auto& image) {
            std::vector<uint8_t> pixels(image->width*image->height*4);
            glGetTextureImage(image->texture->GetID(),0,GL_RGBA,GL_UNSIGNED_BYTE,static_cast<GLsizei>(pixels.size()),pixels.data());return pixels;
        };
        const auto check=[](const auto& pixels,const std::array<float,3>& expected) {
            for(size_t i=0;i<pixels.size();i+=4)for(size_t c=0;c<4;++c)
                if(std::abs(int(pixels[i+c])-int(std::lround((c==3?1:expected[c])*255)))>1)
                    throw std::runtime_error("Preview pixel "+std::to_string(i/4)+" channel "+std::to_string(c)+" actual "+std::to_string(pixels[i+c])+" expected "+std::to_string(std::lround((c==3?1:expected[c])*255)));
        };
        const auto clear=[](ResourceHandle h,AttachmentLoad load,std::array<float,4> color) {
            auto pass=Node("Write",{{h,{},ResourceAccessType::Attachment,load}});pass.operation={};
            pass.prepareOperation=[h,color](const auto& map) {
                const auto target=map.Target(h);return [target,color] {return [target,color](auto&){glClearNamedFramebufferfv(target.GetID(),GL_COLOR,0,color.data());};};
            };return pass;
        };
        RenderPlan plan;const auto a=plan.CreateTransient("A",ColorDesc(16,8)),b=plan.CreateTransient("B",ColorDesc(16,8));
        plan.passes.push_back(clear(a,AttachmentLoad::Clear,{.25f,.5f,.75f,.4f}));
        plan.passes.push_back(clear(a,AttachmentLoad::Load,{.75f,.25f,.5f,.8f}));
        plan.passes.push_back(Node("ReadA",{{a,{},ResourceAccessType::Readback}}));
        plan.passes.push_back(clear(b,AttachmentLoad::Clear,{0,1,0,1}));
        plan.passes.push_back(Node("ReadB",{{b,{},ResourceAccessType::Readback}}));
        const auto compiled=plan.Compile();auto mapping=pool.Realize(plan,compiled);
        Require(mapping->PhysicalID(a)==mapping->PhysicalID(b),"preview alias fixture did not share a physical slot");
        const auto graph=CaptureFrameGraphDiagnostics(plan,compiled,*mapping);
        std::shared_ptr<const FrameGraphPreviewImage> held;
        for(uint32_t version:{1u,2u})for(const auto channel:{FrameGraphPreviewChannel::RGB,FrameGraphPreviewChannel::Red,FrameGraphPreviewChannel::Alpha}) {
            FrameGraphPreviewRequest request;request.resource=a.index;request.resourceKey=FrameGraphPreviewResourceKey(graph,a.index);request.version=version;request.channel=channel;
            auto preview=PrepareFrameGraphPreview(plan,compiled,*mapping,{},graph,request);const auto image=preview.image;
            Require(image->beforePass==version,"preview was not inserted immediately after the requested producer");
            RenderCommandList commands;plan.RecordOperations(commands,{},mapping,std::move(preview.inspection));commands.Seal();
            RenderFrame frame;frame.Append(std::move(commands));frame.Seal();Require(!frame.GetCompletion().Succeeded(),"preview GPU completion was published before submit");
            frame.Submit(driver);frame.Wait();Require(frame.GetCompletion().Succeeded(),"preview frame failed");
            const std::array<float,3> rgb=version==1?std::array<float,3>{.25f,.5f,.75f}:std::array<float,3>{.75f,.25f,.5f};
            const auto value=channel==FrameGraphPreviewChannel::Alpha?(version==1?.4f:.8f):rgb[0];
            check(readImage(image),channel==FrameGraphPreviewChannel::RGB?rgb:std::array<float,3>{value,value,value});held=image;
            std::vector<uint8_t> final(16*8*4);mapping->Target(b).ReadRGBA8(final);check(final,{0,1,0});
        }
        const auto reject=[&](auto request,const char* reason) {
            bool rejected=false;try{PrepareFrameGraphPreview(plan,compiled,*mapping,{},graph,request);}catch(const std::invalid_argument&){rejected=true;}Require(rejected,reason);
        };
        FrameGraphPreviewRequest request;request.resource=a.index;request.resourceKey=FrameGraphPreviewResourceKey(graph,a.index);request.version=1;
        auto invalid=request;invalid.version=0;reject(invalid,"uninitialized transient v0 preview accepted");
        invalid=request;invalid.mip=1;reject(invalid,"out-of-range mip preview accepted");
        invalid=request;++invalid.resourceKey;reject(invalid,"stale plan/resource selection accepted");
        invalid=request;invalid.rangeMin=1;reject(invalid,"empty depth range accepted");
        {
            auto discarded=plan;discarded.passes.insert(discarded.passes.begin()+3,Node("DiscardA",{{a,{},ResourceAccessType::Attachment,AttachmentLoad::DontCare,AttachmentStore::Discard,true}}));
            const auto c=discarded.Compile();const auto g=CaptureFrameGraphDiagnostics(discarded,c,*mapping);
            invalid=request;invalid.resourceKey=FrameGraphPreviewResourceKey(g,a.index);invalid.version=3;bool rejected=false;
            try{PrepareFrameGraphPreview(discarded,c,*mapping,{},g,invalid);}catch(const std::invalid_argument&){rejected=true;}
            Require(rejected,"discarded content was offered as a valid preview");
        }
        // A successful image owns no mapping/source texture, even after aliasing.
        const auto sourceID=mapping->TextureStorage(a).GetID();mapping.reset();pool.TrimIdle();domain.Drain();
        Require(pool.GetStatistics().leasedTargets==0&&!glIsTexture(sourceID)&&held->texture->GetID()!=0,"captured image retained source storage or pool leases");
        check(readImage(held),{.8f,.8f,.8f});
        std::ofstream file(output/"framegraph-preview-alias.rgba",std::ios::binary);const auto aliasPixels=readImage(held);file.write(reinterpret_cast<const char*>(aliasPixels.data()),aliasPixels.size());

        std::shared_ptr<baregl::Texture> sentinelCube;
        const auto capture=[&](RenderPlan& sourcePlan,FrameGraphPreviewRequest selection) {
            const auto c=sourcePlan.Compile();auto map=pool.Realize(sourcePlan,c);const auto values=CaptureFrameGraphDiagnostics(sourcePlan,c,*map);
            selection.resourceKey=FrameGraphPreviewResourceKey(values,selection.resource);
            auto prepared=PrepareFrameGraphPreview(sourcePlan,c,*map,{},values,selection);const auto image=prepared.image;
            GLuint sampler=0,pack=0;glCreateSamplers(1,&sampler);glCreateBuffers(1,&pack);glNamedBufferData(pack,64,nullptr,GL_STATIC_READ);
            for(GLuint unit=0;unit<2;++unit){glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,held->texture->GetID());glBindTexture(GL_TEXTURE_CUBE_MAP,sentinelCube->GetID());glBindSampler(unit,sampler);}
            glBindImageTexture(0,held->texture->GetID(),0,GL_FALSE,0,GL_READ_WRITE,GL_RGBA8);glUseProgram(0);glActiveTexture(GL_TEXTURE7);
            glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);glPixelStorei(GL_PACK_ALIGNMENT,8);glPixelStorei(GL_PACK_ROW_LENGTH,13);glPixelStorei(GL_PACK_SKIP_ROWS,2);glPixelStorei(GL_PACK_SKIP_PIXELS,1);
            const auto state=[] {
                std::vector<GLint> result;const auto get=[&](GLenum field){GLint value=0;glGetIntegerv(field,&value);result.push_back(value);};
                get(GL_CURRENT_PROGRAM);get(GL_ACTIVE_TEXTURE);const auto active=result.back();
                for(GLuint unit=0;unit<2;++unit){glActiveTexture(GL_TEXTURE0+unit);get(GL_TEXTURE_BINDING_2D);get(GL_TEXTURE_BINDING_CUBE_MAP);GLint value=0;glGetIntegeri_v(GL_SAMPLER_BINDING,unit,&value);result.push_back(value);}
                glActiveTexture(active);
                for(const auto field:{GL_IMAGE_BINDING_NAME,GL_IMAGE_BINDING_LEVEL,GL_IMAGE_BINDING_LAYERED,GL_IMAGE_BINDING_LAYER,GL_IMAGE_BINDING_ACCESS,GL_IMAGE_BINDING_FORMAT}){GLint value=0;glGetIntegeri_v(field,0,&value);result.push_back(value);}
                for(const auto field:{GL_PIXEL_PACK_BUFFER_BINDING,GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,GL_PIXEL_UNPACK_BUFFER_BINDING})get(field);
                return result;
            };
            const auto previous=state();
            RenderCommandList list;sourcePlan.RecordOperations(list,{},map,std::move(prepared.inspection));map.reset();list.Seal();
            RenderFrame frame;frame.Append(std::move(list));frame.Seal();frame.Submit(driver);frame.Wait();Require(frame.GetCompletion().Succeeded(),"subresource preview submission failed");
            Require(state()==previous,"preview polluted program, 2D/cube/sampler/image bindings, active unit or pixel pack state");
            glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
            glBindImageTexture(0,0,0,GL_FALSE,0,GL_READ_ONLY,GL_R8);for(GLuint unit=0;unit<2;++unit)glBindSampler(unit,0);glDeleteSamplers(1,&sampler);glDeleteBuffers(1,&pack);return image;
        };
        auto cube=std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_CUBE,"Preview.Cube");
        sentinelCube=cube;
        auto desc=ColorDesc(8,8).texture;desc.mutableDesc.reset();desc.useMipMaps=true;cube->Allocate(desc);
        for(int mip=0;mip<4;++mip)for(int face=0;face<6;++face) {
            const float color[]={float(face+1)/10,float(mip)/5,.5f,1};
            glClearTexSubImage(cube->GetID(),mip,0,0,face,8>>mip,8>>mip,1,GL_RGBA,GL_FLOAT,color);
        }
        RenderPlan cubePlan;const auto cubeHandle=cubePlan.Import("Cube",cube->RetainStorage(),ResourceOrigin::Imported,{{ResourceAspect::Color,0,4,0,6}});
        cubePlan.passes.push_back(Node("ReadCube",{{cubeHandle,{ResourceAspect::Color,0,4,0,6},ResourceAccessType::Sample}}));
        request={};request.resource=cubeHandle.index;
        for(uint16_t mip=0;mip<4;++mip)for(uint16_t face=0;face<6;++face) {
            request.mip=mip;request.layer=face;const auto image=capture(cubePlan,request);
            Require(image->width==unsigned(8>>mip)&&image->height==unsigned(8>>mip),"mip image dimensions incorrect");
            check(readImage(image),{float(face+1)/10,float(mip)/5,.5f});
        }
        auto depth=std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D,"Preview.Depth");
        desc.width=16;desc.height=8;desc.useMipMaps=false;desc.minFilter=baregl::types::ETextureFilteringMode::NEAREST;
        desc.internalFormat=baregl::types::EInternalFormat::DEPTH_COMPONENT;desc.mutableDesc=baregl::data::MutableTextureDesc{baregl::types::EFormat::DEPTH_COMPONENT,baregl::types::EPixelDataType::FLOAT,nullptr};
        depth->Allocate(desc);const float depthValue=.25f;glClearTexImage(depth->GetID(),0,GL_DEPTH_COMPONENT,GL_FLOAT,&depthValue);
        glTextureParameteri(depth->GetID(),GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);
        RenderPlan depthPlan;const auto depthHandle=depthPlan.Import("Depth",depth->RetainStorage(),ResourceOrigin::Imported,{{ResourceAspect::Depth}});
        depthPlan.passes.push_back(Node("ReadDepth",{{depthHandle,{ResourceAspect::Depth},ResourceAccessType::Sample}}));
        request={};request.resource=depthHandle.index;request.rangeMin=.2f;request.rangeMax=.3f;check(readImage(capture(depthPlan,request)),{.5f,.5f,.5f});
        GLint mode=0;glGetTextureParameteriv(depth->GetID(),GL_TEXTURE_COMPARE_MODE,&mode);Require(mode==GL_COMPARE_REF_TO_TEXTURE,"preview changed source depth comparison mode");
        RenderPlan depthBuffer;auto bufferDesc=ColorDesc(16,8);bufferDesc.kind=PlanResourceKind::Renderbuffer;bufferDesc.aspect=ResourceAspect::DepthStencil;
        bufferDesc.texture.internalFormat=baregl::types::EInternalFormat::DEPTH_STENCIL;bufferDesc.sampleable=false;
        const auto buffer=depthBuffer.CreateTransient("DepthBuffer",bufferDesc);
        auto clearDepth=Node("ClearDepth",{{buffer,{ResourceAspect::DepthStencil},ResourceAccessType::Attachment,AttachmentLoad::Clear}});clearDepth.operation={};
        clearDepth.prepareOperation=[buffer](const auto& map){const auto target=map.Target(buffer);return [target]{return [target](auto&){glClearNamedFramebufferfi(target.GetID(),GL_DEPTH_STENCIL,0,.625f,3);};};};
        depthBuffer.passes.push_back(std::move(clearDepth));request={};request.resource=buffer.index;request.version=1;
        check(readImage(capture(depthBuffer,request)),{.625f,.625f,.625f});
        auto red=std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D,"Preview.Red");
        desc.width=1024;desc.height=256;desc.mutableDesc.reset();desc.internalFormat=baregl::types::EInternalFormat::R32F;red->Allocate(desc);
        const float redValue=.5f;glClearTexImage(red->GetID(),0,GL_RED,GL_FLOAT,&redValue);
        RenderPlan redPlan;const auto redHandle=redPlan.Import("Red",red->RetainStorage(),ResourceOrigin::Imported,{{}});redPlan.passes.push_back(Node("ReadRed",{{redHandle,{},ResourceAccessType::Sample}}));
        request={};request.resource=redHandle.index;const auto small=capture(redPlan,request);
        Require(small->width==512&&small->height==128&&small->sourceWidth==1024,"preview did not preserve aspect ratio within its memory cap");
        check(readImage(small),{.5f,.5f,.5f});
        request.exposure=1;request.toneMap=true;check(readImage(capture(redPlan,request)),{.5f,.5f,.5f});
        Require(glGetError()==GL_NO_ERROR,"preview GL state/subresource validation failed");
        std::cout<<"PASS: one-shot version images before alias overwrite, RGB/Red/Alpha, v0/Discard/stale/range rejection, no source lease retention, all 24 cube face/mips, raw depth comparison override, depth renderbuffer resolve, R32F grayscale, aspect/memory cap and HDR controls\n";
    }
