#include <OvRendering/Context/Driver.h>
/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <tracy/Tracy.hpp>
#include <algorithm>
#include <OvRendering/Core/ViewPreparer.h>

#include <OvCore/Rendering/FramebufferUtil.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Panels/AView.h>
#include <OvEditor/Settings/EditorSettings.h>
#include <OvRendering/Utils/Profiling.h>

OvEditor::Panels::AView::AView
(
	const std::string& p_title,
	bool p_opened,
	const OvUI::Settings::PanelWindowSettings& p_windowSettings
) : PanelWindow(p_title, p_opened, p_windowSettings),
	m_surface(p_title,static_cast<uint32_t>(GetSize().x),static_cast<uint32_t>(GetSize().y))
{

	m_shadowFallback.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\ShadowFallback.ovfx"]);
	const auto tex = m_surface.GetColor();
	m_image = &CreateWidget<OvUI::Widgets::Visual::Image>(tex, OvMaths::FVector2{0.f, 0.f});
	scrollable = false;
}

void OvEditor::Panels::AView::Update(float p_deltaTime)
{
}

void OvEditor::Panels::AView::_Draw_Impl()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	OvUI::Panels::PanelWindow::_Draw_Impl();
	ImGui::PopStyleVar();
}

void OvEditor::Panels::AView::InitFrame() {}
void OvEditor::Panels::AView::Render()
{
    auto commands=RecordRenderCommands();commands.Execute(*EDITOR_CONTEXT(driver));CompleteRender();
}
OvEditor::Panels::AView::ViewFrame OvEditor::Panels::AView::ExtractView()
{
    ViewFrame result;
    result.state=EDITOR_CONTEXT(driver)->CreatePipelineState();
    const auto [width,height]=GetSafeSize();m_image->size={float(width),float(height)};
    if(!(width&&height&&GetCamera()&&GetScene())) return result;
    m_surface.Resize(width,height);
    m_outputSize={width,height};
    m_image->textureID=m_surface.GetColor();
    InitFrame();
    result.request=std::make_unique<OvCore::Rendering::SceneRenderRequest>(CreateSceneRequest());
    GetCamera()->CacheMatrices(width,height);
    OvCore::Rendering::SceneFrameRequest frameRequest{*result.request,m_surface.GetTarget(),m_shadowFallback};
    frameRequest.postProcess=&m_postProcess;frameRequest.postProcessTarget=&m_surface.GetFramebuffer();frameRequest.postProcessState=result.state;
    result.frame=m_frontend.ExtractFrame(frameRequest);
    m_overlayRequest=CreateOverlayRequest();
    const auto snapshots=result.frame->GetViews();
    const auto main=std::find_if(snapshots.begin(),snapshots.end(),[&](const auto& input){return input->GetView().id==result.request->viewId;});
    if(main==snapshots.end())throw std::logic_error("Viewport main camera view is missing");
    const auto overlayView=OvRendering::Core::ViewPreparer::BuildView((*main)->GetView());
    result.extractedInfo=result.frame->GetFrameInfo();
    if(m_overlayRequest.grid||m_overlayRequest.debugObjects) {
        if(!m_overlays)m_overlays=std::make_unique<Rendering::EditorOverlayCollector>();
        auto tools=m_overlays->Extract(result.request->scene,m_overlayRequest,overlayView,result.state,m_surface.GetTarget(),&result.frame->ComposePlan());
        result.frame->ComposePlan().AddCommands("Editor.Tools.Completion",std::move(tools));result.extractedInfo+=m_overlays->GetFrameInfo();
    }
    if(m_overlayRequest.picking) {
        if(!m_picking)m_picking=std::make_unique<Rendering::PickingCollector>();
        m_picking->Build(*result.frame,*result.request,m_overlayRequest,result.state);result.extractedInfo+=m_picking->GetFrameInfo();
    }
    OvRendering::Data::RenderCommandList end;
    m_completion=end.GetCompletion();end.Append([](auto& driver){driver.OnFrameCompleted();});
    result.frame->ComposePlan().AddCommands("Viewport.Complete",std::move(end));
    result.viewCount=result.frame->GetViews().size();
    result.active=true;
    return result;
}
void OvEditor::Panels::AView::PrepareView(ViewFrame& view)
{
    if(!view.active||!view.frame) return;
    view.frame=m_frontend.PrepareFrame(std::move(view.frame),view.state,m_jobs);
}
OvRendering::Data::RenderCommandList OvEditor::Panels::AView::RecordView(ViewFrame& view, bool deferEncoding)
{
    OvRendering::Data::RenderCommandList commands;
    m_completion=commands.GetCompletion();m_frameInfo={};
    if(!view.active||!view.frame)
    {
        if(!deferEncoding)commands.Seal(m_jobs);
        return commands;
    }
    FrameMarkStart(name.c_str());
    PublishProductView(view,view.frame->GetPreparedViews());
    commands.AppendRecording(m_frontend.RecordFrame(std::move(view.frame),m_jobs,nullptr,true));
    CaptureProductInteraction();FrameMarkEnd(name.c_str());
    if(!deferEncoding)commands.Seal(m_jobs);
    return commands;
}
void OvEditor::Panels::AView::PublishProductView(const ViewFrame& view,
    std::span<const std::shared_ptr<const OvRendering::Data::PreparedFrameData>> prepared)
{
    m_frameInfo=view.extractedInfo;
    for(const auto& frame:prepared)for(const auto& draw:frame->GetDraws()) {
        const auto& mesh=frame->GetScene().GetMeshes()[draw.sourceIndex];
        m_frameInfo.Add(mesh.geometry->GetVertexCount(),mesh.geometry->GetIndexCount(),mesh.instances);
    }
}
OvRendering::Data::RenderCommandList OvEditor::Panels::AView::RecordRenderCommands(bool deferEncoding)
{
    auto view=ExtractView();
    PrepareView(view);
    return RecordView(view,deferEncoding);
}
std::pair<uint16_t,uint16_t> OvEditor::Panels::AView::GetSafeSize() const
{
    const float title=ImGui::GetFrameHeight();const auto& size=GetSize();
    return {static_cast<uint16_t>(std::max(0.0f,size.x)),static_cast<uint16_t>(std::max(0.0f,size.y-title))};
}
OvCore::Rendering::SceneRenderRequest OvEditor::Panels::AView::CreateSceneRequest()
{
    const auto [width,height]=GetSafeSize();
    return {{*GetScene(),*GetCamera(),width,height,0,0,std::chrono::duration<float>(std::chrono::high_resolution_clock::now()-m_startTime).count()}};
}
OvEditor::Rendering::EditorOverlayRequest OvEditor::Panels::AView::CreateOverlayRequest()
{
    Rendering::EditorOverlayRequest request;request.scene=GetScene()->GetLifetimeToken();return request;
}
