#include <OvCore/Rendering/RenderFrontend.h>
#include <OvRendering/Core/RenderTaskCoordinator.h>
#include <stdexcept>

using namespace OvCore::Rendering;

void RenderFrontend::RequireOwner() const
{
    if (std::this_thread::get_id() != m_owner)
        throw std::logic_error("RenderFrontend requires its constructing thread");
}

std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> RenderFrontend::ExtractScene(const SceneRenderRequest& p_request) const
{
    RequireOwner();
    return SceneRenderExtractor::ExtractScene(p_request, m_recorder.GetFallback2D(), m_recorder.GetFallbackCube());
}

std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> RenderFrontend::Extract(const StaticOpaqueRequest& p_request) const
{
    RequireOwner();
    return SceneRenderExtractor::Extract(p_request, m_recorder.GetFallback2D(), m_recorder.GetFallbackCube());
}

std::shared_ptr<const OvRendering::Data::PreparedFrameData> RenderFrontend::Prepare(
    std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> p_scene,
    OvRendering::Data::PipelineState p_baseState, OvTools::Jobs::JobSystem* p_jobs, const std::atomic_bool* p_cancel) const
{
    RequireOwner();
    return OvRendering::Core::RenderTaskCoordinator::Prepare(std::move(p_scene), p_baseState, p_jobs, p_cancel);
}

OvRendering::Data::RenderCommandList RenderFrontend::Record(
    std::shared_ptr<const OvRendering::Data::PreparedFrameData> p_frame,
    std::optional<baregl::Framebuffer::Snapshot> p_target, OvTools::Jobs::JobSystem* p_jobs, const std::atomic_bool* p_cancel) const
{
    RequireOwner();
    return m_recorder.Record(std::move(p_frame), std::move(p_target), p_jobs, p_cancel);
}
