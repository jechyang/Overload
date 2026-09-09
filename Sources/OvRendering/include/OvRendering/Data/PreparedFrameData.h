#pragma once

#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <vector>
#include <OvRendering/Data/Frustum.h>
#include <OvRendering/Data/RenderPipeline.h>
#include <OvRendering/Data/Material.h>
#include <OvRendering/Data/PipelineState.h>
#include <OvRendering/Resources/IMesh.h>

namespace OvCore::Rendering { class SceneRenderExtractor; }
namespace OvRendering::Core { class ViewPreparer; class RenderTaskCoordinator; }

namespace OvRendering::Data
{

    // Values captured at the scene boundary, never a Camera/Transform alias.
    struct RenderViewInput
    {
        uint64_t id = 0, sceneGeneration = 0;
        uint16_t width = 0, height = 0;
        OvMaths::FVector3 position, forward, up;
        float fov = 60, size = 5, nearPlane = .1f, farPlane = 1000, elapsedTime = 0;
        Settings::EProjectionMode projectionMode = Settings::EProjectionMode::PERSPECTIVE;
        bool cullGeometry = true;
        std::optional<Frustum> frustumOverride;
        bool clearColor = true, clearDepth = true, clearStencil = true;
        OvMaths::FVector4 clearValue;
    };

    struct RenderView
    {
        RenderViewInput input;
        OvMaths::FMatrix4 view, projection;
        std::optional<Frustum> frustum;

    };

    struct StaticMeshInput
    {
        uint64_t actorId = 0, collectionIndex = 0, materialGroup = 0;
        int drawOrder = 0;
        OvMaths::FVector3 position;
        OvMaths::FMatrix4 model, user;
        std::optional<Geometry::BoundingSphere> worldBounds;
        RenderStage stage = RenderStage::Opaque;
        StateMask state;
        uint32_t instances = 1;
        std::shared_ptr<const Resources::IMesh> geometry;
        Material::PreparedSnapshot material;
    };

    // Only Extract can build this object. No mutable builder or container escapes.
    class RenderSceneSnapshot final
    {
        friend class OvCore::Rendering::SceneRenderExtractor;
    public:
        const RenderViewInput& GetView() const { return m_view; }
        const RenderPipeline& GetPipeline() const { return m_pipeline; }
        std::span<const StaticMeshInput> GetMeshes() const { return m_meshes; }
        RenderSceneSnapshot(const RenderSceneSnapshot&) = delete;
    private:
        RenderSceneSnapshot() = default;
        RenderViewInput m_view;
        RenderPipeline m_pipeline;
        std::vector<StaticMeshInput> m_meshes;
    };

    struct PreparedStaticDraw
    {
        size_t sourceIndex;
        PipelineState pipelineState;
        OvMaths::FMatrix4 modelUpload, userUpload;
    };

    struct PreparedStageList
    {
        uint64_t viewId;
        RenderStage stage;
        size_t firstDraw, drawCount;
    };

    // Published only after all preparation succeeds. All source resources are owned.
    class PreparedFrameData final
    {
        friend class Core::ViewPreparer;
        friend class Core::RenderTaskCoordinator;
    public:
        const RenderView& GetView() const { return m_view; }
        std::span<const PreparedStageList> GetLists() const { return m_lists; }
        std::span<const std::thread::id> GetPreparationThreads() const { return m_preparationThreads; }
        std::span<const PreparedStaticDraw> GetDraws() const { return m_draws; }
        const RenderSceneSnapshot& GetScene() const { return *m_scene; }
        std::thread::id GetPreparationThread() const { return m_preparationThread; }
        PreparedFrameData(const PreparedFrameData&) = delete;
    private:
        PreparedFrameData() = default;
        RenderView m_view;
        std::shared_ptr<const RenderSceneSnapshot> m_scene;
        std::vector<PreparedStaticDraw> m_draws;
        std::vector<PreparedStageList> m_lists;
        std::vector<std::thread::id> m_preparationThreads;
        std::thread::id m_preparationThread;
    };
}
