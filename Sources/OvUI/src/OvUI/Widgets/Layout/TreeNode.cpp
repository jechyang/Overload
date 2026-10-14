/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <algorithm>

#include <imgui.h>

#include <OvUI/Internal/Converter.h>
#include <OvUI/Widgets/Layout/TreeNode.h>

OvUI::Widgets::Layout::TreeNode::TreeNode(const std::string & p_name, bool p_arrowClickToOpen) :
	DataWidget(name),
	name(p_name),
	m_arrowClickToOpen(p_arrowClickToOpen)
{
	m_autoExecutePlugins = false;
}

void OvUI::Widgets::Layout::TreeNode::Open()
{
	m_shouldOpen = true;
	m_shouldClose = false;
}

void OvUI::Widgets::Layout::TreeNode::Close()
{
	m_shouldClose = true;
	m_shouldOpen = false;
}

void OvUI::Widgets::Layout::TreeNode::BeginRename()
{
	m_renaming = true;
	m_focusRename = true;
	m_renameBuffer = name;
	m_renameBuffer.resize(std::max<size_t>(name.size() + 64, 256), '\0');
	m_scrollIntoView = true;
}

void OvUI::Widgets::Layout::TreeNode::DrawRenameField(float p_textX)
{
	// Overlay the field on the row: same line as the node, starting where the label starts.
	const ImVec2 rowMin = ImGui::GetItemRectMin(), rowMax = ImGui::GetItemRectMax();
	ImGui::PushID("##Rename");
	ImGui::SameLine();
	ImGui::SetCursorScreenPos({ p_textX - ImGui::GetStyle().FramePadding.x, rowMin.y });
	ImGui::SetNextItemWidth(std::max(40.0f, rowMax.x - p_textX));
	if (m_focusRename)
	{
		ImGui::SetKeyboardFocusHere();
		m_focusRename = false;
	}

	// Same height as the row so the rows below do not move.
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { ImGui::GetStyle().FramePadding.x, std::max(0.0f, (rowMax.y - rowMin.y - ImGui::GetFontSize()) * 0.5f) });
	ImGui::InputText("##Field", m_renameBuffer.data(), m_renameBuffer.size(), ImGuiInputTextFlags_AutoSelectAll);
	ImGui::PopStyleVar();
	const bool deactivated = ImGui::IsItemDeactivated();
	const bool lostFocus = !ImGui::IsItemActive() && !ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered();
	ImGui::PopID();

	if (deactivated || lostFocus)
	{
		m_renaming = false;
		const std::string edited = m_renameBuffer.c_str();
		if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !edited.empty() && edited != name)
			RenamedEvent.Invoke(edited);
	}
}

bool OvUI::Widgets::Layout::TreeNode::IsOpened() const
{
	return m_shouldOpen || (m_opened && !m_shouldClose);
}

void OvUI::Widgets::Layout::TreeNode::_Draw_Impl()
{
	bool prevOpened = m_opened;

	if (m_shouldOpen)
	{
		ImGui::SetNextItemOpen(true);
		m_shouldOpen = false;
	}
	else if (m_shouldClose)
	{
		ImGui::SetNextItemOpen(false);
		m_shouldClose = false;
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
	if (m_renaming)			ImGui::SetNextItemAllowOverlap();
	if (m_arrowClickToOpen) flags |= ImGuiTreeNodeFlags_OpenOnArrow;
	if (selected)			flags |= ImGuiTreeNodeFlags_Selected;
	if (leaf)				flags |= ImGuiTreeNodeFlags_Leaf;

	ImGui::PushStyleColor(ImGuiCol_Text, Internal::Converter::ToImVec4(labelColor.Resolve()));

	iconTextureID.RetainForCapture();
	const bool hasIcon = iconTextureID.id != 0;
	bool opened;
	float labelX = 0.0f;
	if (hasIcon)
	{
		flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
		opened = ImGui::TreeNodeEx(m_widgetID.c_str(), flags);

		// Draw icon and text via the draw list so they don't create new ImGui
		// items (the TreeNodeEx must remain the "last item" for plugins like
		// ContextualMenu that call BeginPopupContextItem).
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float startX = ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing();
		const float centerY = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;

		const ImU32 tintCol = ImGui::ColorConvertFloat4ToU32(Internal::Converter::ToImVec4(labelColor.Resolve()));
		const ImVec2 iconMin(startX, centerY - iconSize * 0.5f);
		const ImVec2 iconMax(startX + iconSize, centerY + iconSize * 0.5f);
		drawList->AddImage(iconTextureID.id, iconMin, iconMax, ImVec2(0.f, 1.f), ImVec2(1.f, 0.f), tintCol);

		labelX = startX + iconSize + ImGui::GetStyle().ItemSpacing.x;
		if (!m_renaming)
			drawList->AddText(ImVec2(labelX, ImGui::GetItemRectMin().y), tintCol, name.c_str());
	}
	else
	{
		opened = ImGui::TreeNodeEx(((m_renaming ? std::string{} : name) + "###" + m_widgetID).c_str(), flags);
		labelX = ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing();
	}

	if (m_scrollIntoView)
	{
		if (!ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
		m_scrollIntoView = false;
	}

	const bool inLabelArea = (ImGui::GetMousePos().x - ImGui::GetItemRectMin().x) > ImGui::GetTreeNodeToLabelSpacing();

	// DoubleClickedEvent fires on the down event so it is detected reliably.
	if (ImGui::IsItemHovered() && inLabelArea && ImGui::IsMouseDoubleClicked(0))
	{
		DoubleClickedEvent.Invoke();
	}

	// ClickedEvent fires on mouse release so that initiating a drag-and-drop does not
	// immediately select the actor (which would hide the drop target in the inspector).
	if (ImGui::IsItemHovered() && inLabelArea && ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragging(0))
	{
		ClickedEvent.Invoke();
	}

	ImGui::PopStyleColor();

	// While renaming, the field is the last item; plugins (menus, drag and drop) wait.
	const bool renaming = m_renaming;
	if (renaming)
		DrawRenameField(labelX);

	if (opened)
	{
		if (!prevOpened)
			OpenedEvent.Invoke();

		m_opened = true;

		if (!renaming) ExecutePlugins(Plugins::EPluginExecutionContext::WIDGET); // Manually execute plugins to make plugins considering the TreeNode and no childs

		DrawWidgets();

		ImGui::TreePop();
	}
	else
	{
		if (prevOpened)
			ClosedEvent.Invoke();

		m_opened = false;

		if (!renaming) ExecutePlugins(Plugins::EPluginExecutionContext::WIDGET); // Manually execute plugins to make plugins considering the TreeNode and no childs
	}
}
