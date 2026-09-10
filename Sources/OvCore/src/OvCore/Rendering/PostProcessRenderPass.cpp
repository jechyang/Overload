#include <OvCore/Rendering/PostProcessRenderPass.h>
#include <OvCore/ECS/Components/CPostProcessStack.h>
#include <OvCore/Rendering/SceneRenderer.h>
using namespace OvCore::Rendering;
PostProcessRenderPass::PostProcessRenderPass(OvRendering::Core::CompositeRenderer& renderer) : ARenderPass(renderer) {}
void PostProcessRenderPass::Draw(OvRendering::Data::PipelineState state)
{
    const auto& scene=m_renderer.GetDescriptor<SceneRenderer::SceneDescriptor>().scene;
    for (const auto* component : scene.GetFastAccessComponents().postProcessStacks)
        if (component && component->owner.IsActive())
        {
            const auto snapshot=PostProcess::PostProcessSnapshot::Capture(component->GetStack());
            m_renderer.SubmitCommandList(m_pipeline.Record(snapshot,m_renderer.GetFrameDescriptor().outputBuffer.value(),state));
            break;
        }
}
