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
    OvRendering::Data::RenderPlan plan;
    const auto texture=target.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR);
    const auto desc=OvRendering::Data::PlanResourceDesc::From(texture->RetainStorage());
    plan.Import("PostProcess.Input",texture->RetainStorage(),OvRendering::Data::ResourceOrigin::Imported,{{desc.aspect,0,desc.mips,0,desc.layers}});
    Build(plan,commands,settings,target,state);
    plan.Validate({});
    plan.RecordOperations(commands);
    commands.Seal();
    return commands;
}
void PostProcessPipeline::Build(OvRendering::Data::RenderPlan& plan,OvRendering::Data::RenderCommandList& commands,
    const PostProcessSnapshot& settings,baregl::Framebuffer& target,OvRendering::Data::PipelineState state)
{
    const auto [width,height]=target.GetSize();
    m_buffers[0].Resize(width,height);
    if(m_exposure.IsApplicable(settings.exposure)||m_bloom.IsApplicable(settings.bloom)||m_tonemapping.IsApplicable(settings.tonemapping)||m_fxaa.IsApplicable(settings.fxaa))m_buffers[1].Resize(width,height);
    if(m_bloom.IsApplicable(settings.bloom))m_bloom.Prepare(width,height,settings.bloom);
    OvRendering::Core::FullscreenCommandRecorder recorder(plan,commands,m_fullscreenResources);
    recorder.SetPassName("PostProcess.InputBlit");
    recorder.Blit(state,target,m_buffers[0],m_blit);
    const auto apply=[&](AEffect& effect,const EffectSettings& input) {
        if (effect.IsApplicable(input)) { effect.Draw(recorder,state,m_buffers[0],m_buffers[1],input); ++m_buffers; }
    };
    apply(m_exposure,settings.exposure); apply(m_bloom,settings.bloom);
    apply(m_tonemapping,settings.tonemapping); apply(m_fxaa,settings.fxaa);
    recorder.SetPassName("PostProcess.OutputBlit");recorder.Blit(state,m_buffers[0],target,m_blit);
    m_info=recorder.GetFrameInfo();
}
