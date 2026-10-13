/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include "OvUI/Modules/Canvas.h"

#include <imgui_internal.h>
#include <baregl/ResourceExecutor.h>

void OvUI::Modules::Canvas::Draw() { BuildFrame(true); }

bool OvUI::Modules::Canvas::BuildFrame(bool p_beginRendererFrame)
{
	if (!m_panels.empty())
	{
		if (p_beginRendererFrame)
            baregl::ResourceExecutor::Invoke([] { ImGui_ImplOpenGL3_NewFrame(); });
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		if (m_isDockspace)
		{
			// The work area excludes viewport side bars (main menu, toolbar, status bar).
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

			ImGui::Begin("##dockspace", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
			ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
			if (m_pendingLayout)
			{
				auto builder = std::move(m_pendingLayout);
				m_pendingLayout = nullptr;
				builder(dockspace_id, viewport->WorkSize);
			}
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), static_cast<ImGuiDockNodeFlags>(static_cast<int>(ImGuiDockNodeFlags_PassthruCentralNode) | static_cast<int>(ImGuiDockNodeFlags_NoWindowMenuButton)));
			ImGui::End();

			ImGui::PopStyleVar(3);
		}

		for (auto& panel : m_panels)
			panel.get().Draw();

		ImGui::Render();
		return true;
	}
	return false;
}

void OvUI::Modules::Canvas::AddPanel(Panels::APanel & p_panel)
{
	m_panels.push_back(std::ref(p_panel));
}

void OvUI::Modules::Canvas::RemovePanel(Panels::APanel & p_panel)
{
	m_panels.erase(std::remove_if(m_panels.begin(), m_panels.end(), [&p_panel](std::reference_wrapper<Panels::APanel>& p_item)
	{
		return &p_panel == &p_item.get();
	}));
}

void OvUI::Modules::Canvas::RemoveAllPanels()
{
	m_panels.clear();
}

void OvUI::Modules::Canvas::MakeDockspace(bool p_state)
{
	m_isDockspace = p_state;
}

void OvUI::Modules::Canvas::RequestDockspaceLayout(std::function<void(ImGuiID, ImVec2)> p_builder)
{
	m_pendingLayout = std::move(p_builder);
}

bool OvUI::Modules::Canvas::IsDockspace() const
{
	return m_isDockspace;
}
