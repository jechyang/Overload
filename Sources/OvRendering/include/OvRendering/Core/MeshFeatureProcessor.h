#pragma once
#include <atomic>
#include <OvRendering/Data/PreparedFrameData.h>
namespace OvRendering::Core
{
    struct VisibleStaticMesh { size_t sourceIndex; float distance; };
    struct MeshRangeResult
    {
        std::vector<VisibleStaticMesh> visible;
        std::thread::id thread;
    };
    class MeshFeatureProcessor final
    {
    public:
        // No scene services, GL, shared output or source material writes.
        static MeshRangeResult PrepareRange(const Data::RenderSceneSnapshot& scene,
            const Data::RenderView& view, size_t begin, size_t end, const std::atomic_bool* cancel);
        static Data::PipelineState BuildState(Data::PipelineState base, const Data::StateMask& material);
    };
}
