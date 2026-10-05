#pragma once
#include <OvCore/Rendering/RenderFrontend.h>

// A small C++ pipeline configuration: an opaque inspection view followed by a
// normal forward view. This module declares all resources and stages on Main;
// the existing coordinator prepares/records both views with exclusive outputs.
namespace RenderingExamples
{
    struct InspectionPipeline
    {
        bool includeTransparent = true;
        bool includeSceneUI = true;

        std::unique_ptr<OvCore::Rendering::SceneFrame> Extract(
            OvCore::Rendering::RenderFrontend& frontend,
            OvCore::Rendering::SceneRenderRequest main,
            const OvRendering::Entities::Camera& inspectionCamera,
            baregl::Framebuffer::Snapshot inspectionTarget,
            baregl::Framebuffer::Snapshot mainTarget) const
        {
            using namespace OvRendering::Data;
            using enum RenderStage;
            const auto [width,height] = inspectionTarget.GetSize();
            OvCore::Rendering::SceneRenderRequest inspection{{main.scene,inspectionCamera,width,height,
                main.viewId+1,main.sceneGeneration,main.elapsedTime}};
            inspection.pipeline.stages={Opaque};
            inspection.fallbackMaterial=main.fallbackMaterial;
            main.pipeline.stages={Opaque};
            if (includeTransparent) main.pipeline.stages.push_back(Transparent);
            if (includeSceneUI) main.pipeline.stages.push_back(SceneUI);
            auto frame=frontend.ComposeFrames({});
            // AddView declares every color/depth/stencil attachment and actual
            // material sampler; preparation compiles the complete graph once.
            frame->AddView(frontend.ExtractScene(inspection),std::move(inspectionTarget),"Opaque inspection");
            frame->AddView(frontend.ExtractScene(main),std::move(mainTarget),"Forward preview");
            return frame;
        }
    };
}
