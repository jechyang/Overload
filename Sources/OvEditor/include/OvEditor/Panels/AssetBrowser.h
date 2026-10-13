/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <OvUI/Panels/PanelWindow.h>

namespace OvUI::Plugins { class ContextualMenu; }

namespace OvEditor::Panels
{
	/**
	* Project window, laid out like Unity's two-column Project browser: a folder tree on the
	* left, the selected folder's content as a thumbnail grid (or list) on the right, with
	* breadcrumbs, search and a zoom slider.
	*/
	class AssetBrowser : public OvUI::Panels::PanelWindow
	{
	public:
		/**
		* Constructor
		* @param p_title
		* @param p_opened
		* @param p_windowSettings
		*/
		AssetBrowser(
			const std::string& p_title,
			bool p_opened,
			const OvUI::Settings::PanelWindowSettings& p_windowSettings
		);

		~AssetBrowser() override;

		/**
		* Rescan the asset folders on the next draw
		*/
		void Refresh();

		/**
		* Show the given folder in the content area and reveal it in the folder tree
		* @param p_folder
		*/
		void ShowFolder(const std::filesystem::path& p_folder);

		/**
		* Returns the folder currently shown in the content area
		*/
		const std::filesystem::path& GetCurrentFolder() const;

	private:
		struct State;

		void DrawBrowser();
		void Rebuild();
		void DrawToolbar();
		void DrawFolderTree();
		void DrawFolderNode(const std::filesystem::path& p_folder, const std::string& p_label);
		void DrawContent();
		void DrawFooter();
		void HandleShortcuts();
		void AcceptDrops(const std::filesystem::path& p_folder);
		void AcceptPayloads(const std::filesystem::path& p_folder);
		OvUI::Plugins::ContextualMenu& FolderMenu(const std::filesystem::path& p_folder);
		void Wire(OvUI::Plugins::ContextualMenu& p_menu, bool p_directory, bool p_script);
		void MoveInto(const std::filesystem::path& p_folder, const std::string& p_resourcePath, bool p_directory);
		void OnRenamed(const std::filesystem::path& p_previous, const std::filesystem::path& p_next, bool p_directory, bool p_script);
		void OnDestroyed(const std::filesystem::path& p_path);
		void Select(const std::filesystem::path& p_path);
		bool IsEngine(const std::filesystem::path& p_path) const;

	private:
		std::unique_ptr<State> m_state;
	};
}
