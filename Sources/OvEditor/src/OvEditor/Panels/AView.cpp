#include <OvRendering/Context/Driver.h>
/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <tracy/Tracy.hpp>
#include <algorithm>

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
    const auto colorIndex=result.frame->FindResource(m_surface.GetColor()->RetainStorage());
    m_overlayRequest=CreateOverlayRequest();
    std::vector<OvRendering::Data::ResourceUse> overlayWrites;
    if(colorIndex) overlayWrites.push_back({*colorIndex,1});
    if(m_overlayRequest.grid||m_overlayRequest.debugObjects)
        result.frame->DeclareTrailingComposition("EditorOverlay",{},std::move(overlayWrites));
    if(m_overlayRequest.picking)
        result.frame->DeclareTrailingComposition("Picking",{},{});
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
    // A trailing OverlayUI pass has screen-space matrices, not the viewport
    // camera. Editor grids and gizmos must always use the requested camera.
    const auto preparedViews=view.frame->GetPreparedViews();
    const auto mainView=std::find_if(preparedViews.begin(),preparedViews.end(),
        [&](const auto& prepared) { return prepared->GetView().input.id == view.request->viewId; });
    if(mainView==preparedViews.end())throw std::logic_error("Viewport main camera view is missing");
    const auto overlayView=(*mainView)->GetView();
    m_frameInfo=view.frame->GetFrameInfo();
    commands.Append(m_frontend.RecordFrame(std::move(view.frame),m_jobs));
    if(m_overlayRequest.grid||m_overlayRequest.debugObjects)
    {
        if(!m_overlays)m_overlays=std::make_unique<Rendering::EditorOverlayCollector>();
        commands.AppendRecording(m_overlays->Extract(view.request->scene,m_overlayRequest,overlayView,view.state,m_surface.GetTarget()));
        m_frameInfo+=m_overlays->GetFrameInfo();
    }
    if(m_overlayRequest.picking)
    {
        if(!m_picking)m_picking=std::make_unique<Rendering::PickingCollector>();
        commands.AppendRecording(m_picking->Extract(*view.request,m_overlayRequest,view.state,m_jobs));
        m_frameInfo+=m_picking->GetFrameInfo();
    }
    OnViewRecorded();
    commands.Append([](auto& driver){driver.OnFrameCompleted();});
    FrameMarkEnd(name.c_str());
    if(!deferEncoding)commands.Seal(m_jobs);
    return commands;
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
