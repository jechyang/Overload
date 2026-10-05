#pragma once
#include <OvCore/Rendering/PostProcess/PostProcessSnapshot.h>
#include <OvRendering/Core/RenderTargetPool.h>
namespace OvCore::Rendering::PostProcess
{
    // One owner per viewport. Settings are values; histories/resources live here.
    class PostProcessPipeline final
    {
    public:
        PostProcessPipeline();
        const OvRendering::Data::FrameInfo& GetFrameInfo() const { return m_info; }
        OvRendering::Data::RenderCommandList Record(const PostProcessSnapshot& settings,
            baregl::Framebuffer& target, OvRendering::Data::PipelineState state, bool reuse=true);
        void Build(OvRendering::Data::RenderPlan&,OvRendering::Data::RenderCommandList& history,
            const PostProcessSnapshot&,baregl::Framebuffer&,OvRendering::Data::PipelineState);
        // Standalone Record uses this pool; Build uses its RenderFrontend's pool.
        void SetResourcePool(std::shared_ptr<OvRendering::Core::RenderTargetPool> pool) { m_recordPool=std::move(pool); }
    private:
        OvRendering::Core::FullscreenCommandResources m_fullscreenResources;
        OvRendering::Data::FrameInfo m_info;
        AutoExposureEffect m_exposure;
        BloomEffect m_bloom;
        TonemappingEffect m_tonemapping;
        FXAAEffect m_fxaa;
        OvRendering::Data::Material m_blit;
        std::shared_ptr<OvRendering::Core::RenderTargetPool> m_recordPool;
    };
}
