#pragma once
#include <atomic>
#include <OvRendering/Data/PreparedFrameData.h>
namespace OvRendering::Core
{
    struct BindingRangeResult
    {
        std::vector<std::shared_ptr<const Data::Material::Snapshot>> materials;
        std::optional<Data::ViewBindings> view;
    };
    class DrawPacketBuilder final
    {
    public:
        static BindingRangeResult PrepareRange(const Data::PreparedFrameData& frame,
            size_t begin, size_t end, bool prepareView, const std::atomic_bool* cancel);
        static Data::EngineConstants Engine(const Data::PreparedFrameData& frame, const Data::PreparedMeshDraw& draw);
    };
}
