#include <OvRendering/Core/FullscreenCommandRecorder.h>
#include <OvRendering/Core/MeshFeatureProcessor.h>
#include <OvRendering/Context/Driver.h>
#include <array>
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
Core::FullscreenCommandRecorder::FullscreenCommandRecorder(Data::RenderCommandList& commands, FullscreenCommandResources& resources)
    : m_commands(commands), m_resources(resources) {}
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
    if (Settings::IsFlagSet(RESIZE_DST_TO_MATCH_SRC,flags)) target.Resize(w,h);
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
    m_commands.AppendDeferred([prepared,output,size,state,quad=m_resources.m_quad] {
        auto payload=Data::Material::EncodeSnapshot(prepared);
        return [payload,output,size,state,quad](Context::Driver& driver) {
            output.Bind(); driver.SetViewport(0,0,size.first,size.second);
            payload->Execute(); driver.Draw(state,*quad,baregl::types::EPrimitiveMode::TRIANGLES,1);
            baregl::Framebuffer::BindDefault();
        };
    });
}
