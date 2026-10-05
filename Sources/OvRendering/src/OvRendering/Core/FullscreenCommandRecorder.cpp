#include <OvRendering/Core/FullscreenCommandRecorder.h>
#include <OvRendering/Core/MeshFeatureProcessor.h>
#include <OvRendering/Context/Driver.h>
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
Data::ResourceHandle Core::FullscreenCommandRecorder::TextureHandle(const std::shared_ptr<baregl::Texture>& texture,bool initialized)
{
    if(!texture)throw std::invalid_argument("Fullscreen: missing texture");
    if(const auto found=m_plan.Find(texture->RetainStorage()))return *found;
    const auto desc=Data::PlanResourceDesc::From(texture->RetainStorage());
    std::vector<Data::ResourceRange> ranges;
    if(initialized)ranges.push_back({desc.aspect,0,desc.mips,0,desc.layers});
    return m_plan.Import(texture->GetDebugName(),texture->RetainStorage(),initialized?Data::ResourceOrigin::Imported:Data::ResourceOrigin::CurrentFrame,std::move(ranges));
}
void Core::FullscreenCommandRecorder::RegisterHistoryTexture(const std::shared_ptr<baregl::Texture>& texture,bool initialized)
{
    const auto desc=Data::PlanResourceDesc::From(texture->RetainStorage());std::vector<Data::ResourceRange> ranges;
    if(initialized)ranges.push_back({desc.aspect,0,desc.mips,0,desc.layers});
    m_plan.Import(texture->GetDebugName(),texture->RetainStorage(),Data::ResourceOrigin::History,std::move(ranges));
}
void Core::FullscreenCommandRecorder::ClearColor(baregl::Framebuffer& target,const OvMaths::FVector4& color)
{
    const auto handle=TextureHandle(target.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR));
    Data::RenderPlanPass pass;pass.name=m_passName;pass.view=Data::RenderPlanPass::NoView;pass.target=target.RecordTarget();
    Data::ResourceAccess access{handle,{},Data::ResourceAccessType::Attachment,Data::AttachmentLoad::Clear};
    access.clearColor={color.x,color.y,color.z,color.w};pass.accesses.push_back(access);
    pass.operation=[output=*pass.target,color] { return [output,color](auto& driver) {output.Bind();driver.Clear(true,false,false,color);baregl::Framebuffer::BindDefault();};};
    m_plan.passes.push_back(std::move(pass));
}
void Core::FullscreenCommandRecorder::GenerateMipmaps(const std::shared_ptr<baregl::Texture>& texture)
{ m_plan.GenerateMipmaps(m_passName,texture); }
void Core::FullscreenCommandRecorder::Blit(Data::PipelineState state, baregl::Framebuffer& source,
    baregl::Framebuffer& target, Data::Material& material, Settings::EBlitFlags flags)
{ BlitWithRetainedTextures(state,source,target,material,flags,{}); }
void Core::FullscreenCommandRecorder::BlitWithRetainedTextures(Data::PipelineState state, baregl::Framebuffer& source,
    baregl::Framebuffer& target, Data::Material& material, Settings::EBlitFlags flags,
    std::span<const std::shared_ptr<baregl::Texture>> textures)
{
    using enum Settings::EBlitFlags;
    if (&source == &target) throw std::invalid_argument("Fullscreen source aliases its output");
    if (!Settings::IsFlagSet(UPDATE_VIEWPORT_SIZE,flags)) throw std::invalid_argument("Fullscreen requires an explicit output viewport");
    const auto [w,h] = source.GetSize();
    if (Settings::IsFlagSet(RESIZE_DST_TO_MATCH_SRC,flags)&&target.GetSize()!=std::pair{w,h})
        throw std::invalid_argument("Fullscreen output was not prepared at the source extent");
    auto owners = std::vector<std::shared_ptr<baregl::Texture>>(textures.begin(),textures.end());
    Data::Material::PropertyOverrides bindings;
    if (Settings::IsFlagSet(FILL_INPUT_TEXTURE,flags))
    {
        auto input = source.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR);
        if (!input) throw std::invalid_argument("Fullscreen input has no color texture");
        bindings.emplace("_InputTexture",input.get()); owners.push_back(std::move(input));
    }
    Data::StateMask mask;
    if (Settings::IsFlagSet(USE_MATERIAL_STATE_MASK,flags)) mask = material.GenerateStateMask();
    else { mask.depthWriting=false; mask.colorWriting=true; mask.blendable=false;
        mask.frontfaceCulling=false; mask.backfaceCulling=false; mask.depthTest=false; }
    state = MeshFeatureProcessor::BuildState(state,mask,Data::RenderStage::ReflectionCapture);
    const auto binding = material.PrepareBinding(std::nullopt,std::nullopt,m_resources.m_materialResources.GetFallback2D().get(),m_resources.m_materialResources.GetFallbackCube().get(),m_signature);
    const bool once = !m_signature || binding.signature.singleUsePropertySignature != m_signature->singleUsePropertySignature;
    auto prepared = material.PrepareSnapshot(std::nullopt,std::nullopt,m_resources.m_materialResources.GetFallback2D(),m_resources.m_materialResources.GetFallbackCube(),owners,
        m_previous,true,once,true,bindings);
    m_previous=prepared; m_signature=binding.signature;
    m_info.Add(4,6,1);
    auto output=target.RecordTarget();
    const auto size=target.GetSize();
    Data::RenderPlanPass pass;pass.name=m_passName;pass.view=Data::RenderPlanPass::NoView;pass.target=output;
    for(const auto& texture:owners) {
        const auto handle=TextureHandle(texture);const auto desc=Data::PlanResourceDesc::From(texture->RetainStorage());
        if(std::none_of(pass.accesses.begin(),pass.accesses.end(),[&](const auto& access){return access.resource==handle;}))
            pass.accesses.push_back({handle,{desc.aspect,0,desc.mips,0,desc.layers},Data::ResourceAccessType::Sample});
    }
    const auto targetHandle=TextureHandle(target.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR));
    const bool fullCoverage=!state.blending&&!state.scissorTest&&!state.stencilTest&&!state.depthTest&&
        state.rasterizationMode==baregl::types::ERasterizationMode::FILL&&state.colorWriting.mask==15;
    pass.accesses.push_back({targetHandle,{},Data::ResourceAccessType::Attachment,state.blending?Data::AttachmentLoad::Load:Data::AttachmentLoad::DontCare,Data::AttachmentStore::Store,fullCoverage});
    pass.operation=[prepared,output,size,state,quad=m_resources.m_quad] {
        auto payload=Data::Material::EncodeSnapshot(prepared);
        return [payload,output,size,state,quad](Context::Driver& driver) {
            output.Bind(); driver.SetViewport(0,0,size.first,size.second);
            payload->Execute(); driver.Draw(state,*quad,baregl::types::EPrimitiveMode::TRIANGLES,1);
            baregl::Framebuffer::BindDefault();
        };
    };
    m_plan.passes.push_back(std::move(pass));
}
