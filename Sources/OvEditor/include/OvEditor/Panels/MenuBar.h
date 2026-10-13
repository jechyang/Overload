/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <unordered_map>
#include <vector>

#include <OvUI/Panels/PanelMenuBar.h>
#include <OvUI/Panels/PanelWindow.h>
#include <OvUI/Widgets/Menu/MenuItem.h>
#include <OvUI/Widgets/Menu/MenuList.h>

namespace OvEditor::Panels
{
	/**
	* Main menu, organized like Unity's: File, Edit, Assets, GameObject, Component, Window, Help
	*/
	class MenuBar : public OvUI::Panels::PanelMenuBar
	{
	public:
		/**
		* Constructor
		*/
		MenuBar();

		/**
		* Check inputs for menubar shortcuts
		* @param p_deltaTime
		*/
		void HandleShortcuts(float p_deltaTime);

		/**
		* Register a panel so it can be toggled from the Window menu
		*/
		void RegisterPanel(const std::string& p_name, OvUI::Panels::PanelWindow& p_panel);

		/**
		* Build the menus that depend on other panels (Window, Edit > Preferences)
		* @note This needs to be called after all other panels have been initialized and named
		*/
		void InitializeSettingsMenu();

	private:
		void CreateFileMenu();
		void CreateEditMenu();
		void CreateAssetsMenu();
		void CreateGameObjectMenu();
		void CreateComponentMenu();
		void CreateWindowMenu();
		void CreateHelpMenu();

		void InitializeWindowMenu();
		void AddPanelToggle(OvUI::Widgets::Menu::MenuList& p_menu, const std::string& p_panelID);
		void UpdateToggleableItems();

	private:
		std::unordered_map<std::string, std::reference_wrapper<OvUI::Panels::PanelWindow>> m_panels;
		std::vector<std::pair<OvUI::Widgets::Menu::MenuItem*, OvUI::Panels::PanelWindow*>> m_panelToggles;
		OvUI::Widgets::Menu::MenuItem* m_undoItem = nullptr;
		OvUI::Widgets::Menu::MenuItem* m_redoItem = nullptr;
		OvUI::Widgets::Menu::MenuItem* m_playItem = nullptr;
		OvUI::Widgets::Menu::MenuItem* m_pauseItem = nullptr;
		OvUI::Widgets::Menu::MenuItem* m_stepItem = nullptr;
		OvUI::Widgets::Menu::MenuItem* m_saveItem = nullptr;
		OvUI::Widgets::Menu::MenuItem* m_saveAsItem = nullptr;
		OvUI::Widgets::Menu::MenuList* m_settingsMenu = nullptr;
		OvUI::Widgets::Menu::MenuList* m_windowMenu = nullptr;
	};
}
