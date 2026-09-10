#pragma once
#include <OvCore/SceneSystem/Scene.h>
#include <OvRendering/Core/MeshCommandRecorder.h>
#include <OvCore/Rendering/ScenePipeline.h>
#include <OvRendering/Data/PreparedFrameData.h>
#include <OvRendering/Entities/Camera.h>

namespace OvCore::Rendering
{
    // A1 is an exclusive static-opaque view request. No implicit fallback or split
    // ownership: unsupported visible categories/features reject the whole request.
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
        bool preserveOutput = false;
        OvRendering::Data::ShadowReceiverBindings shadow;
        OvRendering::Data::ReflectionReceiverBindings reflection;
        OvRendering::Data::Material* shadowFallback = nullptr;
        OvRendering::Data::Material* pickingFallback = nullptr;
    };

    class SceneFrameExtractor;
    class SceneRenderExtractor final
    {
    public:
        static std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> ExtractScene(
            const SceneRenderRequest& p_request,
            const std::shared_ptr<baregl::Texture>& p_white,
            const std::shared_ptr<baregl::Texture>& p_black,
            OvRendering::Core::MeshCommandRecorder::ProgramCache* programs = nullptr);
        // Call in the main-thread stable scene-read interval. No alias to source
        // scene, camera, transform, material or asset escapes this boundary.
        static std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> Extract(
            const StaticOpaqueRequest& p_request,
            const std::shared_ptr<baregl::Texture>& p_white,
            const std::shared_ptr<baregl::Texture>& p_black,
            OvRendering::Core::MeshCommandRecorder::ProgramCache* programs = nullptr);
    private:
        friend class SceneFrameExtractor;
        struct MaterialCache
        {
            std::optional<OvRendering::Data::MaterialSignatureSet> signature;
            OvRendering::Data::Material::PreparedSnapshot snapshot;
        };
        static OvRendering::Data::RenderViewInput CaptureView(const StaticOpaqueRequest& p_request);
        static std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> ExtractImpl(
            const SceneRenderRequest& p_request, bool p_opaqueOnly,
            const std::shared_ptr<baregl::Texture>& p_white, const std::shared_ptr<baregl::Texture>& p_black, bool p_planned = false,
            std::span<const OvRendering::Data::ReflectionProbeInput> p_probes = {}, MaterialCache* p_cache = nullptr,
            const OvRendering::Data::RenderViewInput* p_lightView = nullptr,
            OvRendering::Core::MeshCommandRecorder::ProgramCache* programs = nullptr);
    };
}
