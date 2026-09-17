#pragma once
#include <atomic>
#include <thread>
#include <OvCore/Rendering/SceneRenderExtractor.h>
#include <OvCore/Rendering/SceneFrameExtractor.h>
#include <OvRendering/Core/MeshCommandRecorder.h>
#include <OvTools/Jobs/JobSystem.h>
#include <OvCore/UI/RenderResources.h>

namespace OvCore::Rendering
{
    // Application entry: capture on the owner, then prepare and encode owned views.
    class RenderFrontend final
    {
    public:
        RenderFrontend() = default;
        std::unique_ptr<SceneFrame> ExtractFrame(const SceneFrameRequest& request) const;
        // Compose an owned, fixed plan from already extracted views. No tasks start here.
        std::unique_ptr<SceneFrame> ComposeFrame(
            std::vector<std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot>> views,
            OvRendering::Data::RenderPlan plan) const;
        std::unique_ptr<SceneFrame> PrepareFrame(std::unique_ptr<SceneFrame> frame,
            OvRendering::Data::PipelineState state, OvTools::Jobs::JobSystem* jobs = nullptr, const std::atomic_bool* cancel = nullptr) const;
        OvRendering::Data::RenderCommandList RecordFrame(std::unique_ptr<SceneFrame> frame,
            OvTools::Jobs::JobSystem* jobs = nullptr, const std::atomic_bool* cancel = nullptr) const;
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
        std::thread::id m_owner = std::this_thread::get_id();
        OvRendering::Core::MeshCommandRecorder m_recorder;
        mutable OvRendering::Core::MeshCommandRecorder::ProgramCache m_programs;
        mutable UI::RenderResources m_uiResources;
    };
}
