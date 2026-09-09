#include <OvCore/Rendering/RenderFrontend.h>
#include <OvRendering/Core/ViewPreparer.h>
#include <array>
#include <exception>
#include <stdexcept>

using namespace OvCore::Rendering;

void RenderFrontend::RequireOwner() const
{
    if (std::this_thread::get_id() != m_owner)
        throw std::logic_error("RenderFrontend requires its constructing thread");
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
    using OvRendering::Core::ViewPreparer;
    if (!p_jobs) return ViewPreparer::Prepare(std::move(p_scene), p_baseState, p_cancel);
    if (!p_jobs->IsMainThread()) throw std::logic_error("Prepare coordinator must own the JobSystem");
    // One directly submitted Any job: no draw-count threshold or nested encoder.
    // Its explicit result distinguishes success from JobSystem terminal/failed state.
    struct Result
    {
        std::shared_ptr<const OvRendering::Data::PreparedFrameData> value;
        std::exception_ptr error;
    } result;
    const std::array handles{p_jobs->Submit([scene = std::move(p_scene), p_baseState, p_cancel, &result] {
        try { result.value = ViewPreparer::Prepare(scene, p_baseState, p_cancel); }
        catch (...) { result.error = std::current_exception(); }
    })};
    // Any affinity, no dependencies, pool owner: passive wait cannot run scene
    // mutation callbacks. Everything captured remains alive until terminal acquire.
    p_jobs->WaitAllPassive(handles);
    if (result.error) std::rethrow_exception(result.error);
    if (!result.value) throw std::runtime_error("Prepare task ended without successful output");
    if (p_cancel && p_cancel->load(std::memory_order_acquire)) throw std::runtime_error("View preparation cancelled");
    return std::move(result.value);
}

OvRendering::Data::RenderCommandList RenderFrontend::Record(
    std::shared_ptr<const OvRendering::Data::PreparedFrameData> p_frame,
    std::optional<baregl::Framebuffer::Snapshot> p_target) const
{
    RequireOwner();
    return m_recorder.Record(std::move(p_frame), std::move(p_target));
}
