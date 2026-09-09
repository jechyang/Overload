#pragma once
#include <atomic>
#include <OvRendering/Data/PreparedFrameData.h>

namespace OvRendering::Core
{
    class ViewPreparer final
    {
    public:
        // Pure CPU; identical function for serial and Worker execution.
        static std::shared_ptr<const Data::PreparedFrameData> Prepare(
            std::shared_ptr<const Data::RenderSceneSnapshot> p_scene,
            Data::PipelineState p_baseState,
            const std::atomic_bool* p_cancel = nullptr);
    };
}
