#pragma once
#include <OvCore/Rendering/PostProcess/PostProcessSnapshot.h>
namespace OvCore::Rendering::PostProcess
{
    // One owner per viewport. Settings are values; histories/resources live here.
    class PostProcessPipeline final
    {
    public:
        PostProcessPipeline();
        const OvRendering::Data::FrameInfo& GetFrameInfo() const { return m_info; }
        OvRendering::Data::RenderCommandList Record(const PostProcessSnapshot& settings,
            baregl::Framebuffer& target, OvRendering::Data::PipelineState state);
    private:
        OvRendering::Core::FullscreenCommandResources m_fullscreenResources;
        OvRendering::Data::FrameInfo m_info;
        AutoExposureEffect m_exposure;
        BloomEffect m_bloom;
        TonemappingEffect m_tonemapping;
        FXAAEffect m_fxaa;
        OvRendering::Data::Material m_blit;
        PingPongFramebuffer m_buffers;
    };
}
