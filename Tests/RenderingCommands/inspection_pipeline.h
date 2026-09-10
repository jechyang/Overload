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
            std::vector<std::shared_ptr<const RenderSceneSnapshot>> views;
            views.push_back(frontend.ExtractScene(inspection));
            views.push_back(frontend.ExtractScene(main));
            RenderPlan plan;
            const auto addPass = [&](std::string name,size_t view,baregl::Framebuffer::Snapshot target) {
                RenderPlanPass pass;
                pass.name=std::move(name);pass.view=view;pass.viewId=views[view]->GetView().id;
                pass.stages=views[view]->GetPipeline().stages;pass.target=std::move(target);
                // Color and depth are separate declared outputs; no one-color assumption.
                for (const auto& attachment : pass.target->GetTextureAttachments()) if (attachment.writable)
                {
                    PlanResource resource;
                    resource.id=plan.resources.size()+1;resource.sceneGeneration=main.sceneGeneration;
                    resource.storage=attachment.storage;resource.width=views[view]->GetView().width;
                    resource.height=views[view]->GetView().height;
                    pass.writes.push_back({plan.resources.size(),1});plan.resources.push_back(std::move(resource));
                }
                plan.passes.push_back(std::move(pass));
            };
            addPass("Opaque inspection",0,std::move(inspectionTarget));
            addPass("Forward preview",1,std::move(mainTarget));
            return frontend.ComposeFrame(std::move(views),std::move(plan));
        }
    };
}
