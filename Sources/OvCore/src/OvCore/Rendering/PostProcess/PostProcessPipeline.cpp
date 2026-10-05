#include <OvCore/Rendering/PostProcess/PostProcessPipeline.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/ResourceManagement/ShaderManager.h>
using namespace OvCore::Rendering::PostProcess;
PostProcessPipeline::PostProcessPipeline()
{
    m_blit.SetShader(OVSERVICE(OvCore::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\Blit.ovfx"]);
}
OvRendering::Data::RenderCommandList PostProcessPipeline::Record(const PostProcessSnapshot& settings,
    baregl::Framebuffer& target,OvRendering::Data::PipelineState state,bool reuse)
{
    OvRendering::Data::RenderCommandList commands;
    OvRendering::Data::RenderPlan plan;
    const auto texture=target.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR);
    const auto desc=OvRendering::Data::PlanResourceDesc::From(texture->RetainStorage());
    plan.Import("PostProcess.Input",texture->RetainStorage(),OvRendering::Data::ResourceOrigin::Imported,{{desc.aspect,0,desc.mips,0,desc.layers}});
    Build(plan,commands,settings,target,state);
    const auto compiled=plan.Compile();
    if(!m_recordPool)m_recordPool=std::make_shared<OvRendering::Core::RenderTargetPool>();
    auto mapping=m_recordPool->Realize(plan,compiled,reuse);
    plan.RecordOperations(commands,{},std::move(mapping));
    commands.Seal();
    return commands;
}
void PostProcessPipeline::Build(OvRendering::Data::RenderPlan& plan,OvRendering::Data::RenderCommandList& commands,
    const PostProcessSnapshot& settings,baregl::Framebuffer& target,OvRendering::Data::PipelineState state)
{
    const auto [width,height]=target.GetSize();
    OvRendering::Core::FullscreenCommandRecorder recorder(plan,commands,m_fullscreenResources);
    const auto output=recorder.ImportTarget(target);
    auto current=recorder.CreateTransient("PostProcess.Input",width,height);
    recorder.SetPassName("PostProcess.InputBlit");
    recorder.Blit(state,output,current,m_blit);
    const auto apply=[&](AEffect& effect,const EffectSettings& input,const char* name) {
        if(effect.IsApplicable(input)) {
            const auto next=recorder.CreateTransient(name,width,height);
            effect.Draw(recorder,state,current,next,input);current=next;
        }
    };
    apply(m_exposure,settings.exposure,"PostProcess.Exposure");apply(m_bloom,settings.bloom,"PostProcess.Bloom");
    apply(m_tonemapping,settings.tonemapping,"PostProcess.Tonemapping");apply(m_fxaa,settings.fxaa,"PostProcess.FXAA");
    recorder.SetPassName("PostProcess.OutputBlit");recorder.Blit(state,current,output,m_blit);
    m_info=recorder.GetFrameInfo();
}
