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
    return SceneRenderExtractor::ExtractScene(p_request, m_recorder.GetFallback2D(), m_recorder.GetFallbackCube(), &m_programs);
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

std::unique_ptr<SceneFrame> RenderFrontend::ExtractFrame(const SceneFrameRequest& request) const
{
    RequireOwner();
    return SceneFrameExtractor::Extract(request,m_recorder.GetFallback2D(),m_recorder.GetFallbackCube(), &m_programs, &m_uiResources);
}
std::unique_ptr<SceneFrame> RenderFrontend::PrepareFrame(std::unique_ptr<SceneFrame> frame,
    OvRendering::Data::PipelineState state, OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel) const
{
    RequireOwner();
    if (!frame || !frame->m_prepared.empty()) throw std::invalid_argument("Frame missing or already prepared");
    frame->m_compiled = frame->m_plan.Compile(frame->m_views);
    frame->m_mapping=m_pool->Realize(frame->m_plan,*frame->m_compiled);
    frame->m_prepared = OvRendering::Core::RenderTaskCoordinator::PrepareViews(frame->m_views,state,jobs,cancel);
    for(const auto& prepared:frame->m_prepared)for(const auto& draw:prepared->GetDraws())
    {
        const auto& mesh=prepared->GetScene().GetMeshes()[draw.sourceIndex];
        frame->m_info.Add(mesh.geometry->GetVertexCount(),mesh.geometry->GetIndexCount(),mesh.instances);
    }
    return frame;
}
OvRendering::Data::RenderCommandList RenderFrontend::RecordFrame(std::unique_ptr<SceneFrame> frame,
    OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel) const
{
    RequireOwner();
    if (!frame || frame->m_prepared.size() != frame->m_views.size()) throw std::invalid_argument("Frame not prepared");
    if (!frame->m_compiled) throw std::invalid_argument("Frame plan was not compiled before recording");
    std::vector<std::optional<baregl::Framebuffer::Snapshot>> targets(frame->m_views.size());
    for (const auto& pass : frame->m_plan.passes) if(pass.view!=OvRendering::Data::RenderPlanPass::NoView) targets[pass.view] = pass.target;
    auto blocks = OvRendering::Core::RenderTaskCoordinator::RecordViews(frame->m_prepared,targets,m_recorder,jobs,cancel);
    frame->m_plan.RecordOperations(frame->m_commands,blocks,frame->m_mapping);
    frame->m_commands.Seal();
    return std::move(frame->m_commands);
}

std::unique_ptr<SceneFrame> RenderFrontend::ComposeFrame(
    std::vector<std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot>> views,
    OvRendering::Data::RenderPlan plan) const
{
    RequireOwner();
    plan.Validate(views);
    for (const auto& resource : plan.resources)
        if (resource.origin == OvRendering::Data::ResourceOrigin::History)
            throw std::invalid_argument("ComposeFrame: temporal resources require the SceneFrame extraction owner");
    auto frame = std::unique_ptr<SceneFrame>(new SceneFrame);
    frame->m_views = std::move(views);
    frame->m_plan = std::move(plan);
    return frame;
}
