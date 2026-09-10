#pragma once
#include <OvRendering/Data/RenderPipeline.h>
namespace OvCore::Rendering
{
    struct ScenePipeline final
    {
        static OvRendering::Data::RenderPipeline MainViewMeshes()
        {
            using enum OvRendering::Data::RenderStage;
            return {{Opaque, Transparent, SceneUI}};
        }
    };
}
