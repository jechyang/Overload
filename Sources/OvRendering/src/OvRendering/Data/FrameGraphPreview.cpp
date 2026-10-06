#include <OvRendering/Data/FrameGraphPreview.h>
#include <OvRendering/Core/RenderTargetPool.h>
#include <baregl/ResourceExecutor.h>
#include <baregl/ShaderProgram.h>
#include <baregl/detail/glad/glad.h>
#include <algorithm>
#include <cmath>

using namespace OvRendering::Data;
namespace
{
    const std::string computeSource=R"glsl(#version 430 core
layout(local_size_x=8,local_size_y=8) in;
layout(binding=0) uniform sampler2D source2D;
layout(binding=1) uniform samplerCube sourceCube;
layout(rgba8,binding=0) writeonly uniform image2D outputImage;
uniform int cube,face,mip,channel,toneMap;
uniform float exposure,rangeMin,rangeMax;
vec3 direction(vec2 p) {
    if(face==0)return vec3(1,-p.y,-p.x);
    if(face==1)return vec3(-1,-p.y,p.x);
    if(face==2)return vec3(p.x,1,p.y);
    if(face==3)return vec3(p.x,-1,-p.y);
    if(face==4)return vec3(p.x,-p.y,1);
    return vec3(-p.x,-p.y,-1);
}
void main() {
    ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(outputImage);
    if(any(greaterThanEqual(p,size)))return;
    vec2 uv=(vec2(p)+.5)/vec2(size);
    vec4 c=cube!=0?textureLod(sourceCube,direction(uv*2-1),float(mip)):
        texelFetch(source2D,min(ivec2(uv*vec2(textureSize(source2D,mip))),textureSize(source2D,mip)-1),mip);
    if(channel==6)c=vec4(vec3((c.r-rangeMin)/(rangeMax-rangeMin)),1);
    else if(channel==5)c=vec4(vec3(c.a),1);
    else {
        if(channel>=2&&channel<=4)c=vec4(vec3(c[channel-2]),1);
        c.rgb=max(c.rgb*exp2(exposure),vec3(0));
        if(toneMap!=0)c.rgb=c.rgb/(vec3(1)+c.rgb);
    }
    if(any(isnan(c))||any(isinf(c)))c=vec4(1,0,1,1);
    imageStore(outputImage,p,vec4(clamp(c.rgb,0,1),1));
})glsl";

    // Compute touches only these bindings. Raster state, FBOs, viewport and
    // pixel pack/unpack state are preserved without invalidating driver caches.
    struct ComputeBindings
    {
        GLint program=0,active=0,texture2D[2]{},textureCube[2]{},sampler[2]{},image[6]{};
        GLuint previewSampler=0;
        ComputeBindings() {
            glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
            for(GLuint i=0;i<2;++i) {
                glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture2D[i]);glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&textureCube[i]);
                glGetIntegeri_v(GL_SAMPLER_BINDING,i,&sampler[i]);
            }
            const GLenum fields[]={GL_IMAGE_BINDING_NAME,GL_IMAGE_BINDING_LEVEL,GL_IMAGE_BINDING_LAYERED,GL_IMAGE_BINDING_LAYER,GL_IMAGE_BINDING_ACCESS,GL_IMAGE_BINDING_FORMAT};
            for(size_t i=0;i<6;++i)glGetIntegeri_v(fields[i],0,&image[i]);
            glCreateSamplers(1,&previewSampler);glSamplerParameteri(previewSampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);
            glSamplerParameteri(previewSampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glSamplerParameteri(previewSampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glSamplerParameteri(previewSampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glSamplerParameteri(previewSampler,GL_TEXTURE_COMPARE_MODE,GL_NONE);
        }
        ~ComputeBindings() {
            glUseProgram(program);
            for(GLuint i=0;i<2;++i) {
                glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,texture2D[i]);glBindTexture(GL_TEXTURE_CUBE_MAP,textureCube[i]);glBindSampler(i,sampler[i]);
            }
            glActiveTexture(active);glBindImageTexture(0,image[0],image[1],image[2]!=0,image[3],image[4],image[5]);glDeleteSamplers(1,&previewSampler);
        }
    };
    struct ResolveTexture
    {
        GLuint id=0;
        baregl::ResourceLifetime lifetime{baregl::ResourceKind::TEXTURE};
    };
}

uint64_t OvRendering::Data::FrameGraphPreviewResourceKey(const FrameGraphDiagnostics& graph,size_t resource)
{
    if(resource>=graph.resources.size())throw std::invalid_argument("Preview resource no longer exists");
    uint64_t hash=14695981039346656037ull;
    const auto number=[&](uint64_t value){for(unsigned i=0;i<8;++i){hash^=uint8_t(value>>(8*i));hash*=1099511628211ull;}};
    const auto name=[&](const std::string& value){number(value.size());for(unsigned char c:value){hash^=c;hash*=1099511628211ull;}};
    const auto& r=graph.resources[resource];number(resource);name(r.name);number(uint64_t(r.origin));number(uint64_t(r.kind));number(uint64_t(r.format));
    number(r.width);number(r.height);number(r.mips);number(r.layers);number(r.samples);number(uint64_t(r.aspect));
    // Stable across frame scopes/physical slots. View/effect/order changes reject
    // a stale UI selection; adding the preview image to ImGui does not change it.
    number(graph.passes.size());
    for(size_t p=0;p<graph.passes.size();++p) {
        const auto& pass=graph.passes[p];name(pass.name);number(pass.viewId);
        for(const auto& a:pass.accesses)if(a.resource.index==resource) {
            number(p);number(uint64_t(a.type));number(uint64_t(a.load));number(uint64_t(a.store));number(a.fullCoverage);
            number(uint64_t(a.range.aspect));number(a.range.firstMip);number(a.range.mipCount);number(a.range.firstLayer);number(a.range.layerCount);
        }
    }
    return hash;
}

PreparedFrameGraphPreview OvRendering::Data::PrepareFrameGraphPreview(const RenderPlan& plan,const CompiledRenderPlan& compiled,
    const OvRendering::Core::RenderResourceMapping& mapping,std::span<const std::shared_ptr<const RenderSceneSnapshot>> views,
    const FrameGraphDiagnostics& graph,const FrameGraphPreviewRequest& request)
{
    if(graph.scope!=plan.Scope()||compiled.scope!=plan.Scope()||request.resourceKey!=FrameGraphPreviewResourceKey(graph,request.resource))
        throw std::invalid_argument("The plan changed. Select the resource again and capture a new preview.");
    const auto& r=graph.resources[request.resource];
    if(r.kind==PlanResourceKind::Backbuffer)throw std::invalid_argument("Backbuffer preview is not supported; select a viewport output texture.");
    if(r.samples!=1||request.mip>=r.mips||request.layer>=r.layers||request.version>r.finalVersion||r.lifetime.first==size_t(-1))
        throw std::invalid_argument("Selected content version, mip or layer is unavailable.");
    if(!std::isfinite(request.exposure)||std::abs(request.exposure)>16||!std::isfinite(request.rangeMin)||!std::isfinite(request.rangeMax)||request.rangeMax<=request.rangeMin)
        throw std::invalid_argument("Invalid preview exposure or depth range.");
    size_t before=r.lifetime.first;
    if(request.version) {
        bool found=false;
        for(size_t p=0;p<graph.passes.size();++p)for(size_t a=0;a<graph.passes[p].accesses.size();++a)
            if(graph.passes[p].accesses[a].resource.index==request.resource&&graph.passes[p].contentVersions[a].write==request.version) {before=p+1;found=true;}
        if(!found)throw std::invalid_argument("Selected version has no producer.");
    }
    ResourceAccess read{plan.Handle(request.resource),{r.aspect==ResourceAspect::DepthStencil?ResourceAspect::Depth:r.aspect,request.mip,1,request.layer,1},ResourceAccessType::Readback};
    read.expectedVersion=request.version;
    // Use the real compiler to reject uninitialized/discarded subresources at
    // the requested state. The temporary validation plan is never realized.
    auto validation=plan;RenderPlanPass probe;probe.name="FrameGraph.Preview";probe.view=RenderPlanPass::NoView;
    probe.accesses.push_back(read);probe.operation=[] {return [](auto&){};};
    for(auto& pass:validation.passes)for(auto& previous:pass.after)if(previous>=before)++previous;
    validation.passes.insert(validation.passes.begin()+before,std::move(probe));validation.Compile(views);

    auto image=std::make_shared<FrameGraphPreviewImage>();image->resourceName=r.name;image->scope=plan.Scope();image->version=request.version;
    image->mip=request.mip;image->layer=request.layer;image->beforePass=before;
    image->sourceWidth=std::max(1u,r.width>>request.mip);image->sourceHeight=std::max(1u,r.height>>request.mip);
    const float scale=std::min(1.f,512.f/std::max(image->sourceWidth,image->sourceHeight));
    image->width=std::max(1u,uint32_t(image->sourceWidth*scale));image->height=std::max(1u,uint32_t(image->sourceHeight*scale));
    auto channel=request.channel;
    if(channel==FrameGraphPreviewChannel::Auto) {
        if(uint8_t(r.aspect)&uint8_t(ResourceAspect::Depth))channel=FrameGraphPreviewChannel::Depth;
        else channel=(r.format==baregl::types::EInternalFormat::R32F||r.format==baregl::types::EInternalFormat::R16F||r.format==baregl::types::EInternalFormat::R8)?FrameGraphPreviewChannel::Red:FrameGraphPreviewChannel::RGB;
    }
    if(channel==FrameGraphPreviewChannel::Depth&&!(uint8_t(r.aspect)&uint8_t(ResourceAspect::Depth)))throw std::invalid_argument("Select a depth resource for depth preview.");
    using enum baregl::types::EInternalFormat;
    if((r.format>=R8I&&r.format<=RGBA32UI)||r.format==RGB10_A2UI)throw std::invalid_argument("Integer texture preview is not supported.");
    baregl::Texture::Snapshot source;std::shared_ptr<const baregl::Renderbuffer> renderbuffer;std::shared_ptr<ResolveTexture> resolved;
    if(r.kind==PlanResourceKind::Renderbuffer) {
        renderbuffer=plan.resources[request.resource].renderbuffer;
        if(!renderbuffer) {
            const auto attachments=mapping.Target(read.resource).GetRenderbufferAttachments();
            if(attachments.empty())throw std::invalid_argument("No renderbuffer binding for this resource.");
            renderbuffer=attachments.front().storage;
        }
    } else {
        source=mapping.TextureStorage(read.resource);
        if(source.GetType()!=baregl::types::ETextureType::TEXTURE_2D&&source.GetType()!=baregl::types::ETextureType::TEXTURE_CUBE)
            throw std::invalid_argument("Only 2D textures and cube faces support preview.");
    }
    std::shared_ptr<baregl::ShaderProgram> program;
    baregl::ResourceExecutor::Invoke([&] {
        baregl::data::TextureDesc desc;desc.width=image->width;desc.height=image->height;desc.internalFormat=baregl::types::EInternalFormat::RGBA8;
        desc.useMipMaps=false;desc.minFilter=baregl::types::ETextureFilteringMode::LINEAR;desc.horizontalWrap=desc.verticalWrap=baregl::types::ETextureWrapMode::CLAMP_TO_EDGE;
        image->texture=std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D,"FrameGraph.Preview");image->texture->Allocate(desc);
        if(renderbuffer) {
            GLint format=0;glGetNamedRenderbufferParameteriv(renderbuffer->GetID(),GL_RENDERBUFFER_INTERNAL_FORMAT,&format);
            // Drivers may report the requested unsized depth format. Immutable
            // texture storage requires its actual sized depth/stencil format.
            if(format==GL_DEPTH_COMPONENT||format==GL_DEPTH_STENCIL) {
                GLint bits=0;glGetNamedRenderbufferParameteriv(renderbuffer->GetID(),GL_RENDERBUFFER_DEPTH_SIZE,&bits);
                format=format==GL_DEPTH_STENCIL?(bits>24?GL_DEPTH32F_STENCIL8:GL_DEPTH24_STENCIL8):bits<=16?GL_DEPTH_COMPONENT16:bits<=24?GL_DEPTH_COMPONENT24:GL_DEPTH_COMPONENT32;
            }
            resolved=std::make_shared<ResolveTexture>();glCreateTextures(GL_TEXTURE_2D,1,&resolved->id);resolved->lifetime.Adopt(resolved->id);
            glTextureStorage2D(resolved->id,1,format,image->sourceWidth,image->sourceHeight);
            glTextureParameteri(resolved->id,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTextureParameteri(resolved->id,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        }
        program=std::make_shared<baregl::ShaderProgram>();baregl::ShaderStage shader(baregl::types::EShaderType::COMPUTE);shader.Upload(computeSource);
        const auto compiledShader=shader.Compile();if(!compiledShader.success)throw std::runtime_error("Preview shader: "+compiledShader.message);
        program->Attach(shader);const auto linked=program->Link();program->DetachAll();if(!linked.success)throw std::runtime_error("Preview program: "+linked.message);
    });
    const bool cube=r.kind==PlanResourceKind::TextureCube;
    RenderPlanInspection inspection;inspection.beforePass=before;
    inspection.operation=[image,source,renderbuffer,resolved,program,request,channel,cube] {
        return [image,source,renderbuffer,resolved,program,request,channel,cube](auto&) {
            ComputeBindings bindings;
            // texelFetch on a mutable single-level 2D texture must not inherit
            // mip-chain completeness requirements. Cube textureLod needs them.
            glSamplerParameteri(bindings.previewSampler,GL_TEXTURE_MIN_FILTER,cube?GL_NEAREST_MIPMAP_NEAREST:GL_NEAREST);
            if(renderbuffer)glCopyImageSubData(renderbuffer->GetID(),GL_RENDERBUFFER,0,0,0,0,resolved->id,GL_TEXTURE_2D,0,0,0,0,image->sourceWidth,image->sourceHeight,1);
            glUseProgram(program->GetID());glBindTextureUnit(cube?1:0,resolved?resolved->id:source.GetID());glBindSampler(cube?1:0,bindings.previewSampler);
            glBindImageTexture(0,image->texture->GetID(),0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA8);
            program->SetUniform("cube",int(cube));program->SetUniform("face",int(request.layer));program->SetUniform("mip",int(request.mip));
            program->SetUniform("channel",int(channel));program->SetUniform("toneMap",int(request.toneMap));
            program->SetUniform("exposure",request.exposure);program->SetUniform("rangeMin",request.rangeMin);program->SetUniform("rangeMax",request.rangeMax);
            glDispatchCompute((image->width+7)/8,(image->height+7)/8,1);glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT|GL_TEXTURE_UPDATE_BARRIER_BIT);
        };
    };
    return {std::move(image),std::move(inspection)};
}
