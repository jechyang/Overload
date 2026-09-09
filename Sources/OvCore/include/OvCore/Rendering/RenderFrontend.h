#pragma once
#include <atomic>
#include <thread>
#include <OvCore/Rendering/SceneRenderExtractor.h>
#include <OvRendering/Core/StaticMeshCommandRecorder.h>
#include <OvTools/Jobs/JobSystem.h>

namespace OvCore::Rendering
{
    // Thin application entry. A1's explicitly selected static pipeline is separate
    // from still-unmigrated SceneRenderer callers, never a fallback after failure.
    class RenderFrontend final
    {
    public:
        RenderFrontend() = default;
        std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> ExtractScene(const StaticSceneRequest& p_request) const;
        std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> Extract(const StaticOpaqueRequest& p_request) const;
        std::shared_ptr<const OvRendering::Data::PreparedFrameData> Prepare(
            std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> p_scene,
            OvRendering::Data::PipelineState p_baseState,
            OvTools::Jobs::JobSystem* p_jobs = nullptr, const std::atomic_bool* p_cancel = nullptr) const;
        OvRendering::Data::RenderCommandList Record(
            std::shared_ptr<const OvRendering::Data::PreparedFrameData> p_frame,
            std::optional<baregl::Framebuffer::Snapshot> p_target = std::nullopt) const;
    private:
        void RequireOwner() const;
        std::thread::id m_owner = std::this_thread::get_id();
        OvRendering::Core::StaticMeshCommandRecorder m_recorder;
    };
}
