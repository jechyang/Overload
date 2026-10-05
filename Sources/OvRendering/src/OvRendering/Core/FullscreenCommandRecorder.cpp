#include <OvRendering/Core/FullscreenCommandRecorder.h>
#include <OvRendering/Core/MeshFeatureProcessor.h>
#include <OvRendering/Context/Driver.h>
#include <OvRendering/Core/RenderTargetPool.h>
#include <array>
#include <algorithm>
#include <stdexcept>
using namespace OvRendering;
Core::FullscreenCommandResources::FullscreenCommandResources()
{
    constexpr auto vertices = std::to_array<Geometry::Vertex>({
        {{-1,-1,0},{0,0}}, {{1,-1,0},{1,0}}, {{1,1,0},{1,1}}, {{-1,1,0},{0,1}}});
    constexpr auto indices = std::to_array<uint32_t>({0,1,2,0,2,3});
    Resources::Mesh quad(vertices, indices);
    m_quad = quad.RetainGeometry();
}
Core::FullscreenCommandRecorder::FullscreenCommandRecorder(Data::RenderPlan& plan,Data::RenderCommandList& commands, FullscreenCommandResources& resources)
    : m_commands(commands), m_plan(plan), m_resources(resources) {}
Core::FullscreenCommandRecorder::PlanGpuScope::PlanGpuScope(Data::RenderPlan& plan,Data::RenderCommandList& commands,const tracy::SourceLocationData* source)
    :m_plan(plan),m_commands(commands),m_source(source),m_first(plan.passes.size()),m_exceptions(std::uncaught_exceptions()) {}
Core::FullscreenCommandRecorder::PlanGpuScope::~PlanGpuScope()
{
    if(m_commands.GetState()!=Data::RenderCommandList::State::RECORDING)return;
    if(std::uncaught_exceptions()>m_exceptions){m_commands.FailRecording();return;}
    if(m_first==m_plan.passes.size())return;
    try{m_plan.passes[m_first].gpuScopesBegin.push_back(m_source);++m_plan.passes.back().gpuScopesEnd;}
    catch(...){m_commands.FailRecording();}
}
Data::ResourceHandle Core::FullscreenCommandRecorder::ImportTarget(baregl::Framebuffer& target,Data::ResourceOrigin origin,bool initialized)
{
    auto texture=target.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR);
    if(!texture)throw std::invalid_argument("Fullscreen target has no color texture");
    const auto storage=texture->RetainStorage();
    const auto descriptor=Data::PlanResourceDesc::From(storage);
    auto handle=m_plan.Find(storage);
    if(!handle) {
        std::vector<Data::ResourceRange> ranges;
        if(initialized)ranges.push_back({descriptor.aspect,0,descriptor.mips,0,descriptor.layers});
        handle=m_plan.Import(texture->GetDebugName(),storage,origin,std::move(ranges));
    }
    auto& resource=m_plan.resources[handle->index];
    if(!resource.descriptor)resource.descriptor=descriptor;
    resource.textureOwner=std::move(texture);resource.importedTarget=target.RecordTarget();
    return *handle;
}
Data::ResourceHandle Core::FullscreenCommandRecorder::CreateTransient(std::string name,const baregl::data::TextureDesc& texture)
{
    Data::PlanResourceDesc descriptor;descriptor.texture=texture;
    if(texture.useMipMaps)for(auto extent=std::max(texture.width,texture.height);extent>1;extent/=2)++descriptor.mips;
    return m_plan.CreateTransient(std::move(name),descriptor);
}
Data::ResourceHandle Core::FullscreenCommandRecorder::CreateTransient(std::string name,uint16_t width,uint16_t height,bool mipmaps)
{
    using namespace baregl::types;
    baregl::data::TextureDesc texture{
        .width=width,.height=height,
        .minFilter=mipmaps?ETextureFilteringMode::LINEAR_MIPMAP_LINEAR:ETextureFilteringMode::LINEAR,
        .magFilter=ETextureFilteringMode::LINEAR,
        .horizontalWrap=ETextureWrapMode::CLAMP_TO_BORDER,.verticalWrap=ETextureWrapMode::CLAMP_TO_BORDER,
        .internalFormat=EInternalFormat::RGBA32F,.useMipMaps=mipmaps,
        .mutableDesc=baregl::data::MutableTextureDesc{.format=EFormat::RGBA,.type=EPixelDataType::FLOAT}};
    return CreateTransient(std::move(name),texture);
}
std::pair<uint16_t,uint16_t> Core::FullscreenCommandRecorder::GetSize(Data::ResourceHandle handle) const
{
    const auto& resource=m_plan.Resolve(handle);return {resource.width,resource.height};
}
void Core::FullscreenCommandRecorder::ClearColor(Data::ResourceHandle handle,const OvMaths::FVector4& color)
{
    (void)m_plan.Resolve(handle);
    Data::RenderPlanPass pass;pass.name=m_passName;pass.view=Data::RenderPlanPass::NoView;
    Data::ResourceAccess access{handle,{},Data::ResourceAccessType::Attachment,Data::AttachmentLoad::Clear};
    access.clearColor={color.x,color.y,color.z,color.w};pass.accesses.push_back(access);
    pass.prepareOperation=[handle,color](const Core::RenderResourceMapping& mapping) -> Data::RenderCommandList::OperationFactory {
        return [output=mapping.Target(handle),color] {return [output,color](auto& driver) {
            output.Bind();driver.Clear(true,false,false,color);baregl::Framebuffer::BindDefault();};};
    };
    m_plan.passes.push_back(std::move(pass));
}
void Core::FullscreenCommandRecorder::GenerateMipmaps(Data::ResourceHandle handle)
{
    const auto& desc=*m_plan.Resolve(handle).descriptor;
    if(desc.mips==1)return;
    Data::RenderPlanPass pass;pass.name=m_passName;pass.view=Data::RenderPlanPass::NoView;
    pass.accesses.push_back({handle,{desc.aspect,0,1,0,desc.layers},Data::ResourceAccessType::MipmapSource});
    pass.accesses.push_back({handle,{desc.aspect,1,uint16_t(desc.mips-1),0,desc.layers},Data::ResourceAccessType::MipmapDestination,
        Data::AttachmentLoad::DontCare,Data::AttachmentStore::Store,true});
    pass.prepareOperation=[handle](const Core::RenderResourceMapping& mapping) -> Data::RenderCommandList::OperationFactory {
        return [texture=mapping.Texture(handle)] {return [texture](auto&) {texture->GenerateMipmaps();};};
    };
    m_plan.passes.push_back(std::move(pass));
}
void Core::FullscreenCommandRecorder::Blit(Data::PipelineState state,Data::ResourceHandle source,
    Data::ResourceHandle target,Data::Material& material,Settings::EBlitFlags flags,std::span<const SamplerBinding> textures)
{
    using enum Settings::EBlitFlags;
    if(source==target)throw std::invalid_argument("Fullscreen source aliases its output");
    if(!Settings::IsFlagSet(UPDATE_VIEWPORT_SIZE,flags))throw std::invalid_argument("Fullscreen requires an explicit output viewport");
    const auto size=GetSize(target);
    if(Settings::IsFlagSet(RESIZE_DST_TO_MATCH_SRC,flags)&&GetSize(source)!=size)
        throw std::invalid_argument("Fullscreen output was not prepared at the source extent");
    auto logical=std::vector<SamplerBinding>(textures.begin(),textures.end());
    Data::Material::PropertyOverrides bindings;
    const auto& fallback=m_resources.m_materialResources.GetFallback2D();
    for(const auto& binding:logical) {
        (void)m_plan.Resolve(binding.resource);
        material.SetProperty(binding.name,fallback.get(),binding.singleUse);
    }
    if(Settings::IsFlagSet(FILL_INPUT_TEXTURE,flags)) {
        bindings.emplace("_InputTexture",fallback.get());logical.push_back({"_InputTexture",source});
    }
    Data::StateMask mask;
    if(Settings::IsFlagSet(USE_MATERIAL_STATE_MASK,flags))mask=material.GenerateStateMask();
    else {mask.depthWriting=false;mask.colorWriting=true;mask.blendable=false;
        mask.frontfaceCulling=false;mask.backfaceCulling=false;mask.depthTest=false;}
    state=MeshFeatureProcessor::BuildState(state,mask,Data::RenderStage::ReflectionCapture);
    const auto binding=material.PrepareBinding(std::nullopt,std::nullopt,fallback.get(),m_resources.m_materialResources.GetFallbackCube().get(),m_signature);
    const bool once=!m_signature||binding.signature.singleUsePropertySignature!=m_signature->singleUsePropertySignature;
    auto prepared=material.PrepareSnapshot(std::nullopt,std::nullopt,fallback,m_resources.m_materialResources.GetFallbackCube(),{},
        m_previous,true,once,true,bindings);
    m_previous=prepared;m_signature=binding.signature;m_info.Add(4,6,1);
    Data::RenderPlanPass pass;pass.name=m_passName;pass.view=Data::RenderPlanPass::NoView;
    for(const auto& sampler:logical) {
        const auto& desc=*m_plan.Resolve(sampler.resource).descriptor;
        if(std::none_of(pass.accesses.begin(),pass.accesses.end(),[&](const auto& access){return access.resource==sampler.resource;}))
            pass.accesses.push_back({sampler.resource,{desc.aspect,0,desc.mips,0,desc.layers},Data::ResourceAccessType::Sample});
    }
    const bool fullCoverage=!state.blending&&!state.scissorTest&&!state.stencilTest&&!state.depthTest&&
        state.rasterizationMode==baregl::types::ERasterizationMode::FILL&&state.colorWriting.mask==15;
    pass.accesses.push_back({target,{},Data::ResourceAccessType::Attachment,state.blending?Data::AttachmentLoad::Load:Data::AttachmentLoad::DontCare,Data::AttachmentStore::Store,fullCoverage});
    pass.prepareOperation=[prepared,target,size,state,logical=std::move(logical),quad=m_resources.m_quad](const Core::RenderResourceMapping& mapping) -> Data::RenderCommandList::OperationFactory {
        std::unordered_map<std::string,std::shared_ptr<baregl::Texture>> samplers;
        for(const auto& binding:logical)if(!samplers.emplace(binding.name,mapping.Texture(binding.resource)).second)
            throw std::invalid_argument("Duplicate logical shader sampler");
        return [prepared=Data::Material::RebindPreparedTextures(prepared,samplers),output=mapping.Target(target),size,state,quad] {
            auto payload=Data::Material::EncodeSnapshot(prepared);
            return [payload,output,size,state,quad](Context::Driver& driver) {
                output.Bind();driver.SetViewport(0,0,size.first,size.second);
                payload->Execute();driver.Draw(state,*quad,baregl::types::EPrimitiveMode::TRIANGLES,1);
                baregl::Framebuffer::BindDefault();
            };
        };
    };
    m_plan.passes.push_back(std::move(pass));
}
