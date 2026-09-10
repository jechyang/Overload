#pragma once
#include <OvRendering/Data/PreparedFrameData.h>
#include <baregl/Framebuffer.h>
#include <cstdint>
#include <string>
#include <vector>

namespace OvRendering::Data
{
    enum class ResourceOrigin { CurrentFrame, Imported, History };
    struct PlanResource
    {
        uint64_t id = 0, sceneGeneration = 0;
        baregl::Texture::Snapshot storage;
        uint16_t width = 0, height = 0;
        uint8_t subresources = 1, available = 0;
        ResourceOrigin origin = ResourceOrigin::CurrentFrame;
    };
    struct ResourceUse { size_t resource = 0; uint8_t subresources = 1; };
    struct RenderPlanPass
    {
        std::string name;
        size_t view = 0;
        uint64_t viewId = 0;
        std::vector<RenderStage> stages;
        std::optional<baregl::Framebuffer::Snapshot> target;
        std::vector<ResourceUse> reads, writes;
        // An explicit post-write operation, executed after the final cube face.
        std::shared_ptr<baregl::Texture> generateMipmaps;
        // Fixed full-screen sequence after this view, before the following SceneUI view.
        bool postProcessAfter = false;
    };
    // Fixed GPU order, independent of the order in which CPU view jobs finish.
    // Imported/history availability is supplied only at the owner planning boundary.
    struct RenderPlan
    {
        std::vector<PlanResource> resources;
        std::vector<RenderPlanPass> passes;
        void Validate(std::span<const std::shared_ptr<const RenderSceneSnapshot>> views) const;
    };
}
