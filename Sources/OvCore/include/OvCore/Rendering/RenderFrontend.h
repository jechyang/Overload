#pragma once
#include <atomic>
#include <thread>
#include <OvCore/Rendering/SceneRenderExtractor.h>
#include <OvCore/Rendering/SceneFrameExtractor.h>
#include <OvRendering/Core/MeshCommandRecorder.h>
#include <OvTools/Jobs/JobSystem.h>
#include <OvCore/UI/RenderResources.h>
#include <OvRendering/Core/RenderTargetPool.h>
#include <OvRendering/Data/FrameGraphDiagnostics.h>
#include <OvRendering/Data/FrameGraphPreview.h>
#include <OvRendering/Data/RenderFrame.h>

namespace OvCore::Rendering
{
    // Application entry: capture on the owner, then prepare and encode owned views.
    class RenderFrontend final
    {
    public:
        struct PreviewTicket
        {
            uint64_t sequence=0;
            std::shared_ptr<const OvRendering::Data::FrameGraphPreviewImage> image;
            std::string error;
            OvRendering::Data::RenderFrame::Completion completion;
        };
        RenderFrontend() = default;
        void SetResourcePool(std::shared_ptr<OvRendering::Core::RenderTargetPool> pool) {RequireOwner();if(!pool)throw std::invalid_argument("Missing resource pool");m_pool=std::move(pool);}
        std::shared_ptr<OvRendering::Core::RenderTargetPool> GetResourcePool() const {return m_pool;}
        std::unique_ptr<SceneFrame> ExtractFrame(const SceneFrameRequest& request) const;
        // Compose an owned, fixed plan from already extracted views. No tasks start here.
        std::unique_ptr<SceneFrame> ComposeFrame(
            std::vector<std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot>> views,
            OvRendering::Data::RenderPlan plan) const;
        std::unique_ptr<SceneFrame> ComposeFrames(std::vector<std::unique_ptr<SceneFrame>>) const;
        void SetResourceReuseEnabled(bool value) { RequireOwner();m_reuse=value; }
        void SetDiagnosticsCaptureEnabled(bool);
        std::shared_ptr<const OvRendering::Data::FrameGraphDiagnostics> GetFrameGraphDiagnostics() const;
        void RequestFrameGraphPreview(const OvRendering::Data::FrameGraphPreviewRequest&,OvRendering::Data::RenderFrame::Completion);
        std::shared_ptr<const PreviewTicket> GetFrameGraphPreview() const;
        void ClearFrameGraphPreview();
        std::unique_ptr<SceneFrame> PrepareFrame(std::unique_ptr<SceneFrame> frame,
            OvRendering::Data::PipelineState state, OvTools::Jobs::JobSystem* jobs = nullptr, const std::atomic_bool* cancel = nullptr) const;
        OvRendering::Data::RenderCommandList RecordFrame(std::unique_ptr<SceneFrame> frame,
            OvTools::Jobs::JobSystem* jobs = nullptr, const std::atomic_bool* cancel = nullptr,bool deferEncoding=false) const;
        std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> ExtractScene(const SceneRenderRequest& p_request) const;
        std::shared_ptr<const OvRendering::Data::PreparedFrameData> Prepare(
            std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> p_scene,
            OvRendering::Data::PipelineState p_baseState,
            OvTools::Jobs::JobSystem* p_jobs = nullptr, const std::atomic_bool* p_cancel = nullptr) const;
        OvRendering::Data::RenderCommandList Record(
            std::shared_ptr<const OvRendering::Data::PreparedFrameData> p_frame,
            std::optional<baregl::Framebuffer::Snapshot> p_target = std::nullopt,
            OvTools::Jobs::JobSystem* p_jobs = nullptr, const std::atomic_bool* p_cancel = nullptr) const;
    private:
        void RequireOwner() const;
        bool m_reuse=true;
        bool m_captureDiagnostics=false;
        mutable uint64_t m_diagnosticsSequence=0;
        mutable std::shared_ptr<const OvRendering::Data::FrameGraphDiagnostics> m_diagnostics;
        mutable std::optional<OvRendering::Data::FrameGraphPreviewRequest> m_previewRequest;
        mutable std::shared_ptr<PreviewTicket> m_preview;
        uint64_t m_previewSequence=0;
        std::thread::id m_owner = std::this_thread::get_id();
        OvRendering::Core::MeshCommandRecorder m_recorder;
        mutable OvRendering::Core::MeshCommandRecorder::ProgramCache m_programs;
        mutable UI::RenderResources m_uiResources;
        std::shared_ptr<OvRendering::Core::RenderTargetPool> m_pool=std::make_shared<OvRendering::Core::RenderTargetPool>();
    };
}
