/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <imgui.h>
#include <imgui_internal.h>

#include "OvUI/Internal/Converter.h"
#include "OvUI/Internal/TextureID.h"
#include "OvUI/Styling/Style.h"
#include "OvUI/Widgets/Layout/GroupCollapsable.h"

OvUI::Widgets::Layout::GroupCollapsable::GroupCollapsable(const std::string & p_name) :
	name(p_name)
{
    m_autoExecutePlugins = false;
}

void OvUI::Widgets::Layout::GroupCollapsable::_Draw_Impl()
{
	bool previouslyOpened = opened;
    const auto label = name + "###" + m_widgetID;
    auto* storage = ImGui::GetStateStorage();
    const auto id = ImGui::GetID(label.c_str());
    const int previousExpansion = storage->GetInt(id,defaultExpanded ? 1 : 0);
    if (expandForSearch) ImGui::SetNextItemOpen(true,ImGuiCond_Always);

	// Stable explicit IDs preserve expansion across Inspector rebuilds and keep
	// equally named script/component sections independent.
	const auto& style = Styling::Style::Get();
	if (style.distinctCollapsingHeader)
	{
		ImGui::PushStyleColor(ImGuiCol_Header, Internal::Converter::ToImVec4(style.CollapsingHeader));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Internal::Converter::ToImVec4(style.CollapsingHeaderHovered));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, Internal::Converter::ToImVec4(style.CollapsingHeaderActive));
	}
	bool isOpen = ImGui::CollapsingHeader(label.c_str(), closable ? &opened : nullptr,
		defaultExpanded ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
	if (style.distinctCollapsingHeader) ImGui::PopStyleColor(3);
    if (expandForSearch) storage->SetInt(id,previousExpansion);
    const ImVec2 headerMin = ImGui::GetItemRectMin();
    const ImVec2 headerMax = ImGui::GetItemRectMax();
    ExecutePlugins(Plugins::EPluginExecutionContext::WIDGET);

    // Capture the header item before drawing actions or child property controls.
    if (contextMenu && ImGui::BeginPopupContextItem(("context" + m_widgetID).c_str()))
    {
        contextMenu(); ImGui::EndPopup();
    }

	EndDisableOverride(); // Early end disable override group so that children are not affected

	if (reorderable || !actions.empty() || contextMenu)
	{
		const float bSize = headerMax.y - headerMin.y;
		const float rightEdge = (closable ? (headerMax.x - bSize) : headerMax.x) - (contextMenu ? bSize : 0);
		const ImVec2 savedCursor = ImGui::GetCursorPos();

		// Keep original vertical padding so arrow buttons fill the header height exactly
		const float padY = ImGui::GetStyle().FramePadding.y;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{padY, padY});
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.distinctCollapsingHeader ? Internal::Converter::ToImVec4(style.CollapsingHeaderHovered) : ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.distinctCollapsingHeader ? Internal::Converter::ToImVec4(style.CollapsingHeaderActive) : ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        if (contextMenu)
        {
            ImGui::SetCursorScreenPos({rightEdge,headerMin.y});
            if (ImGui::Button(("...##menu"+m_widgetID).c_str(),{bSize,bSize})) ImGui::OpenPopup(("context"+m_widgetID).c_str());
        }

		if (reorderable)
		{
			ImGui::SetCursorScreenPos({rightEdge - bSize * 2.0f, headerMin.y});
			if (!canMoveUp) ImGui::BeginDisabled();
			if (ImGui::ArrowButton(("##mu" + m_widgetID).c_str(), ImGuiDir_Up))
				MoveUpEvent.Invoke();
			if (!canMoveUp) ImGui::EndDisabled();

			ImGui::SetCursorScreenPos({rightEdge - bSize, headerMin.y});
			if (!canMoveDown) ImGui::BeginDisabled();
			if (ImGui::ArrowButton(("##md" + m_widgetID).c_str(), ImGuiDir_Down))
				MoveDownEvent.Invoke();
			if (!canMoveDown) ImGui::EndDisabled();
		}

		const float actionsRight = rightEdge - (reorderable ? 2.0f * bSize : 0.0f);
		for (int i = static_cast<int>(actions.size()) - 1; i >= 0; --i)
		{
			ImGui::SetCursorScreenPos({actionsRight - static_cast<float>(actions.size() - i) * bSize, headerMin.y});
			const auto texID = actions[i].textureID;
			texID.RetainForCapture();
			if (ImGui::ImageButtonEx(
				ImGui::GetID(("##act" + std::to_string(i) + m_widgetID).c_str()),
				texID.id,
				{bSize - 2.0f * padY, bSize - 2.0f * padY},
				{0.f, 1.f}, {1.f, 0.f},
				{0, 0, 0, 0}, {1, 1, 1, 1}))
				actions[i].callback();
		}

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
		ImGui::SetCursorPos(savedCursor);
	}

	if (isOpen)
		Group::_Draw_Impl();

	if (opened != previouslyOpened)
	{
		if (opened)
			OpenEvent.Invoke();
		else
			CloseEvent.Invoke();
	}
}
