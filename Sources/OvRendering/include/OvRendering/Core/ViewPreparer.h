#pragma once
#include <OvRendering/Core/MeshFeatureProcessor.h>
namespace OvRendering::Core
{
    class ViewPreparer final
    {
    public:
        static Data::RenderView BuildView(const Data::RenderViewInput& input);
        static void SortStage(std::vector<VisibleStaticMesh>& list, std::span<const Data::MeshInput> meshes, Data::RenderStage stage);
        // Serial schedule of exactly the same range and merge functions.
        static std::shared_ptr<const Data::PreparedFrameData> Prepare(
            std::shared_ptr<const Data::RenderSceneSnapshot> scene,
            Data::PipelineState baseState, const std::atomic_bool* cancel = nullptr);
    private:
        friend class RenderTaskCoordinator;
        static std::unique_ptr<Data::PreparedFrameData> BuildDrawLists(
            std::shared_ptr<const Data::RenderSceneSnapshot> scene, Data::RenderView view,
            std::vector<MeshRangeResult> ranges, Data::PipelineState baseState, const std::atomic_bool* cancel);
    };
}
