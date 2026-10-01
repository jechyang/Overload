#pragma once
#include <OvCore/SceneSystem/Scene.h>
#include <OvRendering/Core/MeshCommandRecorder.h>
#include <OvCore/Rendering/ScenePipeline.h>
#include <OvRendering/Data/PreparedFrameData.h>
#include <OvRendering/Entities/Camera.h>
#include <map>
#include <utility>

namespace OvCore::Rendering
{
    // Main-thread scene-read input; the snapshot owns all values after extraction.
    struct SceneViewRequest
    {
        SceneSystem::Scene& scene;
        const OvRendering::Entities::Camera& camera;
        uint16_t width, height;
        uint64_t viewId = 0, sceneGeneration = 0;
        float elapsedTime = 0;
        std::optional<OvRendering::Data::Frustum> frustumOverride;
    };

    struct SceneRenderRequest : SceneViewRequest
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
    private:
        friend class SceneFrameExtractor;
        struct MaterialCache
        {
            std::optional<OvRendering::Data::MaterialSignatureSet> signature;
            OvRendering::Data::Material::PreparedSnapshot snapshot;
            // Frame-shared palettes keyed by (skinned component, pose version).
            std::map<std::pair<const void*, uint64_t>, std::shared_ptr<const std::vector<OvMaths::FMatrix4>>> palettes;
        };
        static OvRendering::Data::RenderViewInput CaptureView(const SceneViewRequest& p_request);
        static std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> ExtractImpl(
            const SceneRenderRequest& p_request,
            const std::shared_ptr<baregl::Texture>& p_white, const std::shared_ptr<baregl::Texture>& p_black, bool p_planned = false,
            std::span<const OvRendering::Data::ReflectionProbeInput> p_probes = {}, MaterialCache* p_cache = nullptr,
            const OvRendering::Data::RenderViewInput* p_lightView = nullptr,
            OvRendering::Core::MeshCommandRecorder::ProgramCache* programs = nullptr);
    };
}
