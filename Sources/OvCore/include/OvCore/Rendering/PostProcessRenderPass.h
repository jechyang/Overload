#pragma once
#include <OvRendering/Core/ARenderPass.h>
#include <OvCore/Rendering/PostProcess/PostProcessPipeline.h>
namespace OvCore::Rendering
{
    // Temporary input adapter for SceneRenderer; removed with its last caller in A6.
    class PostProcessRenderPass : public OvRendering::Core::ARenderPass
    {
    public:
        bool SupportsCommandRecording() const override { return typeid(*this) == typeid(PostProcessRenderPass); }
        explicit PostProcessRenderPass(OvRendering::Core::CompositeRenderer& renderer);
    protected:
        void Draw(OvRendering::Data::PipelineState state) override;
    private:
        PostProcess::PostProcessPipeline m_pipeline;
    };
}
