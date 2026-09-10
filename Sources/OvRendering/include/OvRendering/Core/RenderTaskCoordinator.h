#pragma once
#include <OvRendering/Core/ViewPreparer.h>
#include <OvRendering/Core/MeshCommandRecorder.h>
#include <OvTools/Jobs/JobSystem.h>
namespace OvRendering::Core
{
    class RenderTaskCoordinator final
    {
    public:
        static Data::RenderCommandList Record(std::shared_ptr<const Data::PreparedFrameData> frame,
            const MeshCommandRecorder& recorder, std::optional<baregl::Framebuffer::Snapshot> target,
            OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel);
        static std::shared_ptr<const Data::PreparedFrameData> Prepare(
            std::shared_ptr<const Data::RenderSceneSnapshot> scene, Data::PipelineState baseState,
            OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel = nullptr);
    };
}
