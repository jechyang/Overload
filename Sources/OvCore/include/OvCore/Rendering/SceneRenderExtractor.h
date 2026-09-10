#pragma once
#include <OvCore/SceneSystem/Scene.h>
#include <OvCore/Rendering/ScenePipeline.h>
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

    struct SceneRenderRequest : StaticOpaqueRequest
    {
        OvRendering::Data::RenderPipeline pipeline = ScenePipeline::MainViewMeshes();
        OvRendering::Data::Material* overrideMaterial = nullptr;
        OvRendering::Data::Material* fallbackMaterial = nullptr;
        bool includeOpaque = true, includeTransparent = true, includeUI = true;
        OvRendering::Data::ShadowReceiverBindings shadow;
        OvRendering::Data::ReflectionReceiverBindings reflection;
    };

    class SceneRenderExtractor final
    {
    public:
        static std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> ExtractScene(
            const SceneRenderRequest& p_request,
            const std::shared_ptr<baregl::Texture>& p_white,
            const std::shared_ptr<baregl::Texture>& p_black);
        // Call in the main-thread stable scene-read interval. No alias to source
        // scene, camera, transform, material or asset escapes this boundary.
        static std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> Extract(
            const StaticOpaqueRequest& p_request,
            const std::shared_ptr<baregl::Texture>& p_white,
            const std::shared_ptr<baregl::Texture>& p_black);
    private:
        static std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> ExtractImpl(
            const SceneRenderRequest& p_request, bool p_opaqueOnly,
            const std::shared_ptr<baregl::Texture>& p_white, const std::shared_ptr<baregl::Texture>& p_black);
    };
}
