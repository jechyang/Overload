/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvUI/Internal/TextureID.h>
#include <OvUI/Panels/PanelWindow.h>

struct ImGuiWindow;

namespace OvEditor::Panels
{
	/**
	* Main editor toolbar, pinned under the menu bar like Unity's: transform tools and
	* handle settings on the left, play controls centered, scripts and layouts on the right.
	*/
	class Toolbar : public OvUI::Panels::PanelWindow
	{
	public:
		/**
		* Constructor
		* @param p_title
		* @param p_opened
		* @param p_windowSettings
		*/
		Toolbar
		(
			const std::string& p_title,
			bool p_opened,
			const OvUI::Settings::PanelWindowSettings& p_windowSettings
		);

		/**
		* Custom implementation of the draw method
		*/
		void _Draw_Impl() override;

	private:
		bool DrawIconButton(const char* p_id, const OvUI::Internal::TextureID& p_icon, bool p_active, const char* p_tooltip);
		void DrawTools();
		void DrawPlayControls();
		void DrawRightControls();

	private:
		OvUI::Internal::TextureID m_moveIcon;
		OvUI::Internal::TextureID m_rotateIcon;
		OvUI::Internal::TextureID m_scaleIcon;
		OvUI::Internal::TextureID m_rectIcon;
		OvUI::Internal::TextureID m_playIcon;
		OvUI::Internal::TextureID m_pauseIcon;
		OvUI::Internal::TextureID m_nextIcon;
		OvUI::Internal::TextureID m_refreshIcon;
		ImGuiWindow* m_restoreFocus = nullptr;
	};
}
