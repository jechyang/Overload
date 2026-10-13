/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <algorithm>

#include <imgui.h>
#include <imgui_internal.h>

#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Core/EditorResources.h>
#include <OvEditor/Core/GizmoBehaviour.h>
#include <OvEditor/Panels/SceneView.h>
#include <OvEditor/Panels/Toolbar.h>

#include <OvUI/Styling/Style.h>

namespace
{
	OvUI::Internal::TextureID LoadIcon(const std::string& p_name)
	{
		auto* texture = EDITOR_CONTEXT(editorResources)->GetTexture(p_name);
		return texture ? OvUI::Internal::TextureID{ texture->RetainTexture() } : OvUI::Internal::TextureID{};
	}

	float ButtonSize() { return 26.0f * OVUI_SCALE; }
	float IconSize() { return 16.0f * OVUI_SCALE; }
	float GroupSpacing() { return 10.0f * OVUI_SCALE; }

	bool TextButton(const char* p_label, float p_width, bool p_active, const char* p_tooltip)
	{
		if (p_active)
		{
			const auto accent = ImGui::GetStyleColorVec4(ImGuiCol_Header);
			ImGui::PushStyleColor(ImGuiCol_Button, accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
		}

		const bool clicked = ImGui::Button(p_label, { p_width, ButtonSize() });

		if (p_active) ImGui::PopStyleColor(3);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", p_tooltip);
		return clicked;
	}
}

OvEditor::Panels::Toolbar::Toolbar
(
	const std::string& p_title,
	bool p_opened,
	const OvUI::Settings::PanelWindowSettings& p_windowSettings
) :
	PanelWindow(p_title, p_opened, p_windowSettings),
	m_moveIcon(LoadIcon("Move")),
	m_rotateIcon(LoadIcon("Rotate")),
	m_scaleIcon(LoadIcon("Scale")),
	m_playIcon(LoadIcon("Play")),
	m_pauseIcon(LoadIcon("Pause")),
	m_nextIcon(LoadIcon("Next")),
	m_refreshIcon(LoadIcon("Refresh"))
{
	// Panels created before the toolbar rely on this initial mode notification.
	EDITOR_EXEC(SetEditorMode(OvEditor::Core::EditorActions::EEditorMode::EDIT));
}

bool OvEditor::Panels::Toolbar::DrawIconButton(const char* p_id, const OvUI::Internal::TextureID& p_icon, bool p_active, const char* p_tooltip)
{
	const float padding = (ButtonSize() - IconSize()) * 0.5f;
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { padding, padding });

	if (p_active)
	{
		const auto accent = ImGui::GetStyleColorVec4(ImGuiCol_Header);
		ImGui::PushStyleColor(ImGuiCol_Button, accent);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
	}

	p_icon.RetainForCapture();
	const bool clicked = ImGui::ImageButtonEx(
		ImGui::GetID(p_id), p_icon.id, { IconSize(), IconSize() },
		{ 0.f, 1.f }, { 1.f, 0.f }, { 0.f, 0.f, 0.f, 0.f }, { 1.f, 1.f, 1.f, 1.f }
	);

	if (p_active) ImGui::PopStyleColor(3);
	ImGui::PopStyleVar();

	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", p_tooltip);
	return clicked;
}

void OvEditor::Panels::Toolbar::DrawTools()
{
	using Core::EGizmoOperation;

	const auto operation = EDITOR_EXEC(GetGizmoOperation());
	const float spacing = OVUI_SCALE;

	if (DrawIconButton("##ToolMove", m_moveIcon, operation == EGizmoOperation::TRANSLATE, "Move Tool (W)"))
		EDITOR_EXEC(SetGizmoOperation(EGizmoOperation::TRANSLATE));
	ImGui::SameLine(0, spacing);
	if (DrawIconButton("##ToolRotate", m_rotateIcon, operation == EGizmoOperation::ROTATE, "Rotate Tool (E)"))
		EDITOR_EXEC(SetGizmoOperation(EGizmoOperation::ROTATE));
	ImGui::SameLine(0, spacing);
	if (DrawIconButton("##ToolScale", m_scaleIcon, operation == EGizmoOperation::SCALE, "Scale Tool (R)"))
		EDITOR_EXEC(SetGizmoOperation(EGizmoOperation::SCALE));
	ImGui::SameLine(0, GroupSpacing());

	// Tool handle settings, as in Unity's "Pivot | Center" and "Local | Global" toggles.
	auto& sceneView = EDITOR_PANEL(SceneView, "Scene View");
	const float width = 64.0f * OVUI_SCALE;

	if (TextButton(sceneView.IsCenterPivot() ? "Center##Pivot" : "Pivot##Pivot", width, false,
		"Tool handle position\nPivot: primary object origin\nCenter: center of the selection bounds"))
		sceneView.SetCenterPivot(!sceneView.IsCenterPivot());
	ImGui::SameLine(0, spacing);

	// Scaling always uses local axes (world-space scale of rotated objects would shear).
	const bool scaling = operation == EGizmoOperation::SCALE;
	ImGui::BeginDisabled(scaling);
	if (TextButton(scaling || sceneView.IsLocalSpace() ? "Local##Space" : "Global##Space", width, false,
		scaling ? "The Scale tool always uses local axes" : "Tool handle rotation\nLocal: object axes\nGlobal: world axes"))
		sceneView.SetLocalSpace(!sceneView.IsLocalSpace());
	ImGui::EndDisabled();
}

void OvEditor::Panels::Toolbar::DrawPlayControls()
{
	using EEditorMode = Core::EditorActions::EEditorMode;

	const auto mode = EDITOR_EXEC(GetCurrentEditorMode());
	const bool inPlayMode = mode != EEditorMode::EDIT;
	const float spacing = OVUI_SCALE;
	const float width = 3.0f * ButtonSize() + 2.0f * spacing;

	ImGui::SameLine();
	ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + GroupSpacing(), (ImGui::GetWindowWidth() - width) * 0.5f));

	if (DrawIconButton("##Play", m_playIcon, inPlayMode, inPlayMode ? "Stop (Ctrl+P)" : "Play (Ctrl+P)"))
		EDITOR_EXEC(TogglePlay());
	ImGui::SameLine(0, spacing);

	ImGui::BeginDisabled(!inPlayMode);
	if (DrawIconButton("##Pause", m_pauseIcon, mode == EEditorMode::PAUSE, mode == EEditorMode::PAUSE ? "Resume (Ctrl+Shift+P)" : "Pause (Ctrl+Shift+P)"))
		EDITOR_EXEC(TogglePause());
	ImGui::SameLine(0, spacing);
	if (DrawIconButton("##Step", m_nextIcon, false, "Step one frame (Ctrl+Alt+P)"))
		EDITOR_EXEC(NextFrame());
	ImGui::EndDisabled();
}

void OvEditor::Panels::Toolbar::DrawRightControls()
{
	const float arrowSpace = 16.0f * OVUI_SCALE;
	const float layoutWidth = ImGui::CalcTextSize("Layout").x + ImGui::GetStyle().FramePadding.x * 2.0f + arrowSpace;
	const float width = ButtonSize() + GroupSpacing() + layoutWidth;

	ImGui::SameLine();
	ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + GroupSpacing(), ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - width));

	if (DrawIconButton("##ReloadScripts", m_refreshIcon, false, "Reload all scripts"))
		EDITOR_EXEC(RefreshScripts());
	ImGui::SameLine(0, GroupSpacing());

	ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, { 0.0f, 0.5f });
	const bool openLayouts = ImGui::Button("Layout##Layouts", { layoutWidth, ButtonSize() });
	ImGui::PopStyleVar();

	const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
	const float arrowScale = 0.7f;
	const float arrowHeight = ImGui::GetFontSize() * arrowScale;
	ImGui::RenderArrow(ImGui::GetWindowDrawList(), { max.x - arrowSpace, min.y + (max.y - min.y - arrowHeight) * 0.5f },
		ImGui::GetColorU32(ImGuiCol_Text), ImGuiDir_Down, arrowScale);

	if (openLayouts) ImGui::OpenPopup("##LayoutMenu");
	ImGui::SetNextWindowPos({ max.x, max.y + 2.0f * OVUI_SCALE }, ImGuiCond_Always, { 1.0f, 0.0f });
	if (ImGui::BeginPopup("##LayoutMenu"))
	{
		if (ImGui::MenuItem("Default")) EDITOR_EXEC(ApplyLayout(Core::EEditorLayout::DEFAULT));
		if (ImGui::MenuItem("2 by 3")) EDITOR_EXEC(ApplyLayout(Core::EEditorLayout::TWO_BY_THREE));
		ImGui::EndPopup();
	}
}

void OvEditor::Panels::Toolbar::_Draw_Impl()
{
	if (!IsOpened()) return;

	// Clicking toolbar buttons must not take keyboard focus away from the panel the user
	// works in (W/E/R, Delete, Ctrl+D... are scoped to the focused Scene View/Hierarchy).
	// ImGui focuses a clicked window at the end of the frame, so remember the last regular
	// window that had focus and hand it back once the toolbar receives it.
	const auto windowName = name + GetPanelID();
	auto* const toolbarWindow = ImGui::FindWindowByName(windowName.c_str());
	if (auto* focused = ImGui::GetCurrentContext()->NavWindow; focused && focused != toolbarWindow && !(focused->Flags & ImGuiWindowFlags_Popup))
		m_restoreFocus = focused;

	const float verticalPadding = 5.0f * OVUI_SCALE;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8.0f * OVUI_SCALE, verticalPadding });
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
	ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_TabHovered));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));

	constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
	if (ImGui::BeginViewportSideBar(windowName.c_str(), ImGui::GetMainViewport(), ImGuiDir_Up, ButtonSize() + 2.0f * verticalPadding, flags))
	{
		const bool focused = ImGui::IsWindowFocused();

		DrawTools();
		DrawPlayControls();
		DrawRightControls();

		const ImVec2 min = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
		ImGui::GetWindowDrawList()->AddLine({ min.x, min.y + size.y - 1.0f }, { min.x + size.x, min.y + size.y - 1.0f }, ImGui::GetColorU32(ImGuiCol_Border));

		const bool menuOpen = ImGui::IsPopupOpen("##LayoutMenu");
		if (focused && !menuOpen && m_restoreFocus && m_restoreFocus->WasActive)
			ImGui::FocusWindow(m_restoreFocus);
	}
	ImGui::End();

	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar();
}
