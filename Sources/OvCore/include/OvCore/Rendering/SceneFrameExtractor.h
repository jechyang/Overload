#pragma once
#include <OvCore/Rendering/SceneRenderExtractor.h>
#include <OvRendering/Data/RenderPlan.h>
#include <OvRendering/Data/FrameGraphDiagnostics.h>
#include <OvCore/Rendering/PostProcess/PostProcessPipeline.h>
#include <optional>
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
        OvRendering::Data::RenderPlan& ComposePlan() { if(m_compiled)throw std::logic_error("Frame plan already published");return m_plan; }
        OvRendering::Data::RenderCommandList::Completion GetCompletion() const { return m_commands.GetCompletion(); }
        void AddView(std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot>,std::optional<baregl::Framebuffer::Snapshot>,std::string);
        SceneFrame(const SceneFrame&) = delete;
    private:
        SceneFrame() = default;
        OvRendering::Data::FrameInfo m_info;
        OvRendering::Data::RenderPlan m_plan;
        std::optional<OvRendering::Data::CompiledRenderPlan> m_compiled;
        std::shared_ptr<const OvRendering::Core::RenderResourceMapping> m_mapping;
        std::optional<OvRendering::Data::RenderPlanInspection> m_inspection;
        // Kept private until recording succeeds; published diagnostics never mutate.
        std::shared_ptr<OvRendering::Data::FrameGraphDiagnostics> m_diagnostics;
        std::vector<std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot>> m_views;
        std::vector<std::shared_ptr<const OvRendering::Data::PreparedFrameData>> m_prepared;
        // Registers histories before speculative owner state advances. Destruction,
        // cancellation, Prepare/Record/Execute failure all invalidate the same epoch.
        OvRendering::Data::RenderCommandList m_commands;
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
