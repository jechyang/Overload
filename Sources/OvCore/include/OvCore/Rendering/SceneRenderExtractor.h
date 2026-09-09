#pragma once
#include <OvCore/SceneSystem/Scene.h>
#include <OvRendering/Data/PreparedFrameData.h>
#include <OvRendering/Entities/Camera.h>

namespace OvCore::Rendering
{
    // A1 is an exclusive static-opaque view request. No implicit fallback or split
    // ownership: unsupported visible categories/features reject the whole request.
    // Existing Game and editor view callers keep SceneRenderer until migrated.
    struct StaticOpaqueRequest
    {
        SceneSystem::Scene& scene;
        const OvRendering::Entities::Camera& camera;
        uint16_t width, height;
        uint64_t viewId = 0, sceneGeneration = 0;
        float elapsedTime = 0;
        std::optional<OvRendering::Data::Frustum> frustumOverride;
    };

    class SceneRenderExtractor final
    {
    public:
        // Call in the main-thread stable scene-read interval. No alias to source
        // scene, camera, transform, material or asset escapes this boundary.
        static std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> Extract(
            const StaticOpaqueRequest& p_request,
            const std::shared_ptr<baregl::Texture>& p_white,
            const std::shared_ptr<baregl::Texture>& p_black);
    };
}
