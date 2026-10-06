#include <OvCore/Rendering/RenderFrontend.h>
#include <OvRendering/Core/RenderTaskCoordinator.h>
#include <stdexcept>

using namespace OvCore::Rendering;

void RenderFrontend::RequireOwner() const
{
    if (std::this_thread::get_id() != m_owner)
        throw std::logic_error("RenderFrontend requires its constructing thread");
}

void RenderFrontend::SetDiagnosticsCaptureEnabled(bool enabled)
{
    RequireOwner();m_captureDiagnostics=enabled;
    if(!enabled){m_diagnostics.reset();m_previewRequest.reset();m_preview.reset();SetGpuTimingCaptureEnabled(false);}
}
std::shared_ptr<const OvRendering::Data::FrameGraphDiagnostics> RenderFrontend::GetFrameGraphDiagnostics() const
{
    RequireOwner();return m_diagnostics;
}
void RenderFrontend::RequestFrameGraphPreview(const OvRendering::Data::FrameGraphPreviewRequest& request,OvRendering::Data::RenderFrame::Completion completion)
{
    RequireOwner();
    if(completion.GetState()!=OvRendering::Data::RenderFrame::State::RECORDING)throw std::invalid_argument("Preview requires the recording frame's completion");
    m_previewRequest=request;m_preview=std::make_shared<PreviewTicket>();
    m_preview->sequence=++m_previewSequence;m_preview->completion=std::move(completion);
}
std::shared_ptr<const RenderFrontend::PreviewTicket> RenderFrontend::GetFrameGraphPreview() const {RequireOwner();return m_preview;}
void RenderFrontend::ClearFrameGraphPreview() {RequireOwner();m_previewRequest.reset();m_preview.reset();}
void RenderFrontend::SetGpuTimingCaptureEnabled(bool enabled)
{
    RequireOwner();
    if(enabled&&!m_captureDiagnostics)throw std::logic_error("GPU timings require diagnostic capture");
    if(enabled){if(!m_gpuTimer)m_gpuTimer=std::make_unique<OvRendering::Data::FrameGraphGpuTimer>();}
    else {m_gpuPending.clear();m_gpuTimer.reset();m_gpuTimings.reset();m_gpuSampleCounter=0;}
}
std::shared_ptr<const OvRendering::Data::FrameGraphGpuTimings> RenderFrontend::GetFrameGraphGpuTimings() const
{
    RequireOwner();
    for(auto it=m_gpuPending.begin();it!=m_gpuPending.end();) {
        if(!(*it)->IsRetired()){++it;continue;}
        if(auto result=(*it)->GetResult();result&&(!m_gpuTimings||result->graph->captureIndex>m_gpuTimings->graph->captureIndex))m_gpuTimings=std::move(result);
        it=m_gpuPending.erase(it);
    }
    return m_gpuTimings;
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
    if (!frame || frame->m_compiled || !frame->m_prepared.empty()) throw std::invalid_argument("Frame missing or already prepared");
    frame->m_compiled = frame->m_plan.Compile(frame->m_views);
    frame->m_mapping=m_pool->Realize(frame->m_plan,*frame->m_compiled,m_reuse);
    frame->m_prepared = OvRendering::Core::RenderTaskCoordinator::PrepareViews(frame->m_views,state,jobs,cancel);
    for(const auto& prepared:frame->m_prepared)for(const auto& draw:prepared->GetDraws())
    {
        const auto& mesh=prepared->GetScene().GetMeshes()[draw.sourceIndex];
        frame->m_info.Add(mesh.geometry->GetVertexCount(),mesh.geometry->GetIndexCount(),mesh.instances);
    }
    if(m_captureDiagnostics) {
        auto diagnostics=OvRendering::Data::CaptureFrameGraphDiagnostics(frame->m_plan,*frame->m_compiled,*frame->m_mapping,frame->m_views);
        diagnostics.captureIndex=++m_diagnosticsSequence;
        m_diagnostics=std::make_shared<const OvRendering::Data::FrameGraphDiagnostics>(std::move(diagnostics));
        if(m_previewRequest) {
            const auto request=*m_previewRequest;m_previewRequest.reset();
            try {
                auto preview=OvRendering::Data::PrepareFrameGraphPreview(frame->m_plan,*frame->m_compiled,*frame->m_mapping,frame->m_views,*m_diagnostics,request);
                m_preview->image=std::move(preview.image);frame->m_inspection=std::move(preview.inspection);
            }catch(const std::exception& error){m_preview->error=error.what();}
        }
    }
    return frame;
}
OvRendering::Data::RenderCommandList RenderFrontend::RecordFrame(std::unique_ptr<SceneFrame> frame,
    OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel,bool deferEncoding,OvRendering::Data::RenderFrame* timingFrame) const
{
    RequireOwner();
    if (!frame || frame->m_prepared.size() != frame->m_views.size()) throw std::invalid_argument("Frame not prepared");
    if (!frame->m_compiled) throw std::invalid_argument("Frame plan was not compiled before recording");
    std::vector<std::optional<baregl::Framebuffer::Snapshot>> targets(frame->m_views.size());
    for (const auto& pass : frame->m_plan.passes) if(pass.view!=OvRendering::Data::RenderPlanPass::NoView) targets[pass.view] = pass.target;
    auto blocks = OvRendering::Core::RenderTaskCoordinator::RecordViews(frame->m_prepared,targets,m_recorder,jobs,cancel);
    std::shared_ptr<OvRendering::Data::FrameGraphGpuTimingCapture> timings;
    if(m_gpuTimer) {
        if(!timingFrame||!m_diagnostics||m_diagnostics->scope!=frame->m_plan.Scope())throw std::invalid_argument("GPU timings require the recording frame and matching diagnostic graph");
        GetFrameGraphGpuTimings();
        if(m_gpuSampleCounter++%4==0&&m_gpuPending.size()<OvRendering::Data::FrameGraphGpuTimer::MaxPending) {
            timings=m_gpuTimer->Capture(m_diagnostics,*timingFrame);
            if(timings)m_gpuPending.push_back(timings);
        }
    }
    frame->m_plan.RecordOperations(frame->m_commands,blocks,frame->m_mapping,std::move(frame->m_inspection),std::move(timings));
    if(!deferEncoding)frame->m_commands.Seal(jobs);
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

std::unique_ptr<SceneFrame> RenderFrontend::ComposeFrames(std::vector<std::unique_ptr<SceneFrame>> fragments) const
{
    RequireOwner();auto frame=std::unique_ptr<SceneFrame>(new SceneFrame);
    for(auto& fragment:fragments) {
        if(!fragment||fragment->m_compiled||!fragment->m_prepared.empty())throw std::invalid_argument("Product composition needs unprepared owned fragments");
        frame->m_plan.Append(std::move(fragment->m_plan),frame->m_views.size(),fragment->m_views.size());
        for(auto& view:fragment->m_views)frame->m_views.push_back(std::move(view));
        frame->m_info+=fragment->m_info;frame->m_commands.AppendRecording(std::move(fragment->m_commands));
    }
    return frame;
}
