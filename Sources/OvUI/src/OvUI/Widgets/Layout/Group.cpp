/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <cfloat>
#include <vector>

#include <imgui.h>

#include "OvUI/Widgets/Layout/Group.h"

void OvUI::Widgets::Layout::Group::_Draw_Impl()
{
	if (!horizontal)
	{
		DrawWidgets();
		return;
	}

	CollectGarbages();

	// Disabled widgets take no column, so hidden helpers (e.g. labels) leave no gap.
	std::vector<size_t> visible;
	for (size_t index = 0; index < m_widgets.size(); ++index)
		if (m_widgets[index].first->enabled) visible.push_back(index);

	if (visible.empty())
	{
		return;
	}

	const auto& style = ImGui::GetStyle();
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{ style.ItemSpacing.x * 0.5f, style.CellPadding.y });

	if (ImGui::BeginTable(("group" + m_widgetID).c_str(), static_cast<int>(visible.size()), ImGuiTableFlags_NoSavedSettings))
	{
		for (size_t column = 0; column < visible.size(); ++column)
		{
			const auto columnFlags = static_cast<int>(visible[column]) == stretchWidget ?
				ImGuiTableColumnFlags_WidthStretch :
				ImGuiTableColumnFlags_WidthFixed;

			ImGui::TableSetupColumn(("##col_" + std::to_string(visible[column])).c_str(), columnFlags);
		}

		ImGui::TableNextRow();

		for (size_t column = 0; column < visible.size(); ++column)
		{
			const size_t index = visible[column];
			ImGui::TableSetColumnIndex(static_cast<int>(column));

			if (static_cast<int>(index) == stretchWidget)
			{
				ImGui::SetNextItemWidth(-FLT_MIN);
			}

			auto& widget = *m_widgets[index].first;
			const auto previousLineBreak = widget.lineBreak;
			widget.lineBreak = true;
			widget.Draw();
			widget.lineBreak = previousLineBreak;
		}

		ImGui::EndTable();
	}

	ImGui::PopStyleVar();
}
