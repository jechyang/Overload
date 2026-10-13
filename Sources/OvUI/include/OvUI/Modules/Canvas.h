/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <algorithm>
#include <functional>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <OvTools/Eventing/Event.h>

#include "OvUI/API/IDrawable.h"
#include "OvUI/Panels/APanel.h"

namespace OvUI::Modules
{
	/**
	* A Canvas represents the whole frame available for UI drawing
	*/
	class Canvas : public API::IDrawable
	{
	public:
		/**
		* Adds a panel to the canvas
		* @param p_panel
		*/
		void AddPanel(Panels::APanel& p_panel);

		/**
		* Removes a panel from the canvas
		* @param p_panel
		*/
		void RemovePanel(Panels::APanel& p_panel);

		/**
		* Removes every panels from the canvas
		*/
		void RemoveAllPanels();

		/**
		* Makes the canvas a dockspace (Dockable panels will be able to attach themselves to the canvas)
		*/
		void MakeDockspace(bool p_state);

		/**
		* Returns true if the canvas is a dockspace
		*/
		bool IsDockspace() const;

		/**
		* Rebuild the dockspace layout on the next frame. The builder is invoked inside the
		* dockspace host, before the dockspace is submitted, with its ID and available size,
		* so it can use the ImGui DockBuilder API.
		* @param p_builder
		*/
		void RequestDockspaceLayout(std::function<void(ImGuiID, ImVec2)> p_builder);

		/**
		* Draw the canvas (Draw every panels)
		*/
		void Draw() override;

		// Build CPU draw data; renderer initialization can be performed separately.
		bool BuildFrame(bool p_beginRendererFrame);

	private:
		std::vector<std::reference_wrapper<Panels::APanel>> m_panels;
		bool m_isDockspace = false;
		std::function<void(ImGuiID, ImVec2)> m_pendingLayout;
	};
}
