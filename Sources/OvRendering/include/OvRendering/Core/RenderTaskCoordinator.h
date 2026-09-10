#pragma once
#include <OvRendering/Core/ViewPreparer.h>
#include <OvRendering/Core/MeshCommandRecorder.h>
#include <OvTools/Jobs/JobSystem.h>
namespace OvRendering::Core
{
    class RenderTaskCoordinator final
    {
    public:
        static std::vector<std::shared_ptr<const Data::PreparedFrameData>> PrepareViews(
            std::span<const std::shared_ptr<const Data::RenderSceneSnapshot>> scenes, Data::PipelineState baseState,
            OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel);
        static std::vector<Data::RenderCommandList> RecordViews(
            std::span<const std::shared_ptr<const Data::PreparedFrameData>> frames,
            std::span<const std::optional<baregl::Framebuffer::Snapshot>> targets,
            const MeshCommandRecorder& recorder, OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel);
        static Data::RenderCommandList Record(std::shared_ptr<const Data::PreparedFrameData> frame,
            const MeshCommandRecorder& recorder, std::optional<baregl::Framebuffer::Snapshot> target,
            OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel);
        static std::shared_ptr<const Data::PreparedFrameData> Prepare(
            std::shared_ptr<const Data::RenderSceneSnapshot> scene, Data::PipelineState baseState,
            OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel = nullptr);
    };
}
