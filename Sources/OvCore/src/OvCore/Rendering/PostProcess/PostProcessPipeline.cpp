#include <OvCore/Rendering/PostProcess/PostProcessPipeline.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/ResourceManagement/ShaderManager.h>
using namespace OvCore::Rendering::PostProcess;
PostProcessPipeline::PostProcessPipeline() : m_buffers("PostProcessBlit")
{
    for (auto& buffer : m_buffers.GetFramebuffers()) OvCore::Rendering::FramebufferUtil::SetupFramebuffer(buffer,1,1,false,false,false);
    m_blit.SetShader(OVSERVICE(OvCore::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\Blit.ovfx"]);
}
OvRendering::Data::RenderCommandList PostProcessPipeline::Record(const PostProcessSnapshot& settings,
    baregl::Framebuffer& target, OvRendering::Data::PipelineState state)
{
    OvRendering::Data::RenderCommandList commands;
    OvRendering::Core::FullscreenCommandRecorder recorder(commands,m_fullscreenResources);
    recorder.Blit(state,target,m_buffers[0],m_blit);
    const auto apply=[&](AEffect& effect,const EffectSettings& input) {
        if (effect.IsApplicable(input)) { effect.Draw(recorder,state,m_buffers[0],m_buffers[1],input); ++m_buffers; }
    };
    apply(m_exposure,settings.exposure); apply(m_bloom,settings.bloom);
    apply(m_tonemapping,settings.tonemapping); apply(m_fxaa,settings.fxaa);
    recorder.Blit(state,m_buffers[0],target,m_blit);
    commands.Seal();
    m_info=recorder.GetFrameInfo();
    return commands;
}
