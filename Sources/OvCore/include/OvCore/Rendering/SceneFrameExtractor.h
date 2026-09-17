#pragma once
#include <OvCore/Rendering/SceneRenderExtractor.h>
#include <OvRendering/Data/RenderPlan.h>
#include <OvCore/Rendering/PostProcess/PostProcessPipeline.h>
namespace OvCore::UI { class RenderResources; }
namespace OvCore::Rendering
{
    struct SceneFrameRequest
    {
        SceneRenderRequest main;
        std::optional<baregl::Framebuffer::Snapshot> target;
        OvRendering::Data::Material& shadowFallback;
        bool generateShadows = true, captureReflections = true;
        PostProcess::PostProcessPipeline* postProcess = nullptr;
        baregl::Framebuffer* postProcessTarget = nullptr;
        OvRendering::Data::PipelineState postProcessState;
    };
    class RenderFrontend;
    class SceneFrame final
    {
        friend class SceneFrameExtractor;
        friend class RenderFrontend;
    public:
        const OvRendering::Data::FrameInfo& GetFrameInfo() const { return m_info; }
        const OvRendering::Data::RenderPlan& GetPlan() const { return m_plan; }
        std::span<const std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot>> GetViews() const { return m_views; }
        std::span<const std::shared_ptr<const OvRendering::Data::PreparedFrameData>> GetPreparedViews() const { return m_prepared; }
        SceneFrame(const SceneFrame&) = delete;
    private:
        SceneFrame() = default;
        OvRendering::Data::FrameInfo m_info;
        OvRendering::Data::RenderPlan m_plan;
        std::vector<std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot>> m_views;
        std::vector<std::shared_ptr<const OvRendering::Data::PreparedFrameData>> m_prepared;
        // Registers histories before speculative owner state advances. Destruction,
        // cancellation, Prepare/Record/Execute failure all invalidate the same epoch.
        OvRendering::Data::RenderCommandList m_commands;
        std::optional<size_t> m_postProcessAfterView;
        std::unique_ptr<OvRendering::Data::RenderCommandList> m_postProcessCommands;
    };
    class SceneFrameExtractor final
    {
    public:
        static std::unique_ptr<SceneFrame> Extract(const SceneFrameRequest& request,
            const std::shared_ptr<baregl::Texture>& white, const std::shared_ptr<baregl::Texture>& black,
            OvRendering::Core::MeshCommandRecorder::ProgramCache* programs = nullptr,
            UI::RenderResources* uiResources = nullptr);
    };
}
