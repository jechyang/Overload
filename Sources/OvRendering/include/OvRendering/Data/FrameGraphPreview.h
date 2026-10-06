#pragma once
#include <OvRendering/Data/FrameGraphDiagnostics.h>

namespace OvRendering::Data
{
    enum class FrameGraphPreviewChannel { Auto,RGB,Red,Green,Blue,Alpha,Depth };
    struct FrameGraphPreviewRequest
    {
        size_t resource=0;
        uint64_t resourceKey=0;
        uint32_t version=0;
        uint16_t mip=0,layer=0;
        FrameGraphPreviewChannel channel=FrameGraphPreviewChannel::Auto;
        float exposure=0,rangeMin=0,rangeMax=1;
        bool toneMap=false;
    };
    // Separate from value-only plan diagnostics. Only this bounded, explicitly
    // requested RGBA8 image is retained, never a source texture or pool lease.
    struct FrameGraphPreviewImage
    {
        std::string resourceName;
        uint64_t scope=0;
        uint32_t version=0,sourceWidth=0,sourceHeight=0,width=0,height=0;
        uint16_t mip=0,layer=0;
        size_t beforePass=0;
        std::shared_ptr<baregl::Texture> texture;
    };
    struct PreparedFrameGraphPreview
    {
        std::shared_ptr<const FrameGraphPreviewImage> image;
        RenderPlanInspection inspection;
    };
    uint64_t FrameGraphPreviewResourceKey(const FrameGraphDiagnostics&,size_t resource);
    PreparedFrameGraphPreview PrepareFrameGraphPreview(const RenderPlan&,const CompiledRenderPlan&,
        const Core::RenderResourceMapping&,std::span<const std::shared_ptr<const RenderSceneSnapshot>>,
        const FrameGraphDiagnostics&,const FrameGraphPreviewRequest&);
}
