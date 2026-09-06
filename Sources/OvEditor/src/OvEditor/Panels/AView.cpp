/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <tracy/Tracy.hpp>

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
	m_framebuffer(p_title)
{
	OvCore::Rendering::FramebufferUtil::SetupFramebuffer(
		m_framebuffer,
		static_cast<uint32_t>(GetSize().x),
		static_cast<uint32_t>(GetSize().y),
		true, true, false
	);

	const auto tex = m_framebuffer.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR);
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

void OvEditor::Panels::AView::InitFrame()
{
	m_renderer->AddDescriptor<OvCore::Rendering::SceneRenderer::SceneDescriptor>(
		CreateSceneDescriptor()
	);
}

void OvEditor::Panels::AView::Render()
{
	auto [winWidth, winHeight] = GetSafeSize();
	m_image->size = OvMaths::FVector2(static_cast<float>(winWidth), static_cast<float>(winHeight));

	auto camera = GetCamera();
	auto scene = GetScene();

	if (winWidth > 0 && winHeight > 0 && camera && scene)
	{
		FrameMarkStart(name.c_str());

		m_framebuffer.Resize(winWidth, winHeight);
		m_image->textureID = m_framebuffer.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR);

		InitFrame();

		OvRendering::Data::FrameDescriptor frameDescriptor;
		frameDescriptor.renderWidth = winWidth;
		frameDescriptor.renderHeight = winHeight;
		frameDescriptor.camera = camera;
		frameDescriptor.outputBuffer = m_framebuffer;

		m_renderer->BeginFrame(frameDescriptor);
		DrawFrame();
		if (!m_renderer->IsRecordingCommands()) CompleteRender();
		m_renderer->EndFrame();
		FrameMarkEnd(name.c_str());
		m_renderer->SubmitRenderOperation([](auto& driver) { driver.OnFrameCompleted(); });
	}
}

OvRendering::Data::RenderCommandList OvEditor::Panels::AView::RecordRenderCommands()
{
    m_renderer->SetDrawRecordingEnabled(true);
    m_renderer->BeginCommandRecording();
    try
    {
        Render();
        return m_renderer->FinishCommandRecording();
    }
    catch (...)
    {
        const auto error = std::current_exception();
        try { if (m_renderer->IsDrawing()) m_renderer->EndFrame(); } catch (...) {}
        m_renderer->CancelCommandRecording();
        std::rethrow_exception(error);
    }
}

void OvEditor::Panels::AView::DrawFrame()
{
	m_renderer->DrawFrame();
}

std::pair<uint16_t, uint16_t> OvEditor::Panels::AView::GetSafeSize() const
{
	const float kTitleBarHeight = ImGui::GetFrameHeight(); // accounts for the imgui window title bar
	const auto& size = GetSize();
	return {
		static_cast<uint16_t>(size.x),
		static_cast<uint16_t>(std::max(0.0f, size.y - kTitleBarHeight))
	}; 
}

const OvCore::Rendering::SceneRenderer& OvEditor::Panels::AView::GetRenderer() const
{
	return *m_renderer.get();
}

OvCore::Rendering::SceneRenderer::SceneDescriptor OvEditor::Panels::AView::CreateSceneDescriptor()
{
	auto scene = GetScene();

	OVASSERT(scene, "No scene assigned to this view!");

	return {
		*scene
	};
}
