/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <imgui.h>
#include <imgui_internal.h>

#include <string_view>

#include <OvEditor/Core/EditorActions.h>

namespace
{
	// Panels that every layout shows, mirroring Unity's default window set.
	constexpr const char* kCorePanels[] = { "Scene View", "Game View", "Hierarchy", "Inspector", "Asset Browser", "Console", "Toolbar" };

	// Secondary panels share a tab group with a core panel and stay closed until requested.
	constexpr const char* kViewPanels[] = { "Scene View", "Game View", "Asset View" };
	constexpr const char* kInspectorPanels[] = { "Inspector", "Material Editor", "Asset Properties", "Project Settings" };
	constexpr const char* kUtilityPanels[] = { "Console", "C# Scripts", "Frame Info", "Hardware Info", "Frame Graph", "Texture Debugger" };
}

void OvEditor::Core::EditorActions::ApplyLayout(EEditorLayout p_layout)
{
	for (const auto* id : kCorePanels)
		m_panelsManager.GetPanelAs<OvUI::Panels::PanelWindow>(id).SetOpened(true);

	m_panelsManager.GetCanvas().RequestDockspaceLayout([this, p_layout](ImGuiID p_dockspace, ImVec2 p_size)
	{
		const auto dock = [this](const char* p_panel, ImGuiID p_node)
		{
			auto& panel = m_panelsManager.GetPanelAs<OvUI::Panels::PanelWindow>(p_panel);
			ImGui::DockBuilderDockWindow((panel.name + panel.GetPanelID()).c_str(), p_node);
		};

		ImGui::DockBuilderRemoveNode(p_dockspace);
		ImGui::DockBuilderAddNode(p_dockspace, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(p_dockspace, p_size);

		ImGuiID views = p_dockspace;
		ImGuiID inspector = 0, hierarchy = 0, project = 0, utilities = 0, game = 0;

		if (p_layout == EEditorLayout::TWO_BY_THREE)
		{
			// | Scene / Game | Hierarchy | Project | Inspector |
			inspector = ImGui::DockBuilderSplitNode(views, ImGuiDir_Right, 0.22f, nullptr, &views);
			project = ImGui::DockBuilderSplitNode(views, ImGuiDir_Right, 0.22f, nullptr, &views);
			hierarchy = ImGui::DockBuilderSplitNode(views, ImGuiDir_Right, 0.28f, nullptr, &views);
			game = ImGui::DockBuilderSplitNode(views, ImGuiDir_Down, 0.50f, nullptr, &views);
			utilities = project;
		}
		else
		{
			// | Hierarchy | Scene / Game | Inspector |
			// |   Project / Console      |           |
			inspector = ImGui::DockBuilderSplitNode(views, ImGuiDir_Right, 0.24f, nullptr, &views);
			project = ImGui::DockBuilderSplitNode(views, ImGuiDir_Down, 0.30f, nullptr, &views);
			hierarchy = ImGui::DockBuilderSplitNode(views, ImGuiDir_Left, 0.22f, nullptr, &views);
			game = views;
			utilities = project;
		}

		dock("Hierarchy", hierarchy);
		dock("Asset Browser", project);
		for (const auto* id : kInspectorPanels) dock(id, inspector);
		for (const auto* id : kUtilityPanels) dock(id, utilities);
		for (const auto* id : kViewPanels) dock(id, std::string_view{ id } == "Scene View" ? views : game);

		ImGui::DockBuilderFinish(p_dockspace);
	});

	// Once the windows are docked, bring the primary tab of each area to the front.
	DelayAction([this]
	{
		for (const auto* id : { "Asset Browser", "Inspector", "Hierarchy", "Scene View" })
		{
			auto& panel = m_panelsManager.GetPanelAs<OvUI::Panels::PanelWindow>(id);
			if (panel.IsOpened()) panel.Focus();
		}
	}, 1);
}
