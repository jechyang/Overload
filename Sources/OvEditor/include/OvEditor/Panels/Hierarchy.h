/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/Resources/Loaders/TextureLoader.h>
#include <OvRendering/Entities/Camera.h>

#include <OvCore/SceneSystem/SceneManager.h>

#include <OvUI/Panels/PanelWindow.h>
#include <OvUI/Widgets/Layout/Group.h>
#include <OvUI/Widgets/Layout/TreeNode.h>
#include <OvUI/Widgets/InputFields/InputText.h>
#include <OvUI/Widgets/Menu/MenuList.h>
#include <OvUI/Widgets/Texts/Text.h>

#include <memory>

namespace OvEditor::Panels
{
	class Hierarchy : public OvUI::Panels::PanelWindow
	{
	public:
		/**
		* Constructor
		* @param p_title
		* @param p_opened
		* @param p_windowSettings
		*/
		Hierarchy
		(
			const std::string& p_title,
			bool p_opened,
			const OvUI::Settings::PanelWindowSettings& p_windowSettings
		);
		
		/**
		* Clear hierarchy nodes
		*/
        ~Hierarchy() override;
		void Clear();

		/**
		* Unselect every widgets
		*/
		void UnselectActorsWidgets();

		/**
		* Select the widget corresponding to the given actor
		* @param p_actor
		*/
		void SelectActorByInstance(OvCore::ECS::Actor& p_actor);
        void SelectActorFromClick(OvCore::ECS::Actor& actor, bool toggle, bool range);

		/**
		* Select the widget
		* @param p_actor
		*/
		void SelectActorByWidget(OvUI::Widgets::Layout::TreeNode& p_widget);

		/**
		* Reconcile committed scene entities before drawing, preserving existing nodes.
		*/
		void Synchronize();

		/**
		* Filter by entity name or tag (case insensitive for ASCII).
		*/
		void SetSearchFilter(const std::string& p_filter);

		/**
		* Delete the widget referencing the given actor
		* @param p_actor
		*/
		void DeleteActorByInstance(OvCore::ECS::Actor& p_actor);

		/**
		* Add a widget referencing the given actor
		* @param p_actor
		*/
		void AddActorByInstance(OvCore::ECS::Actor& p_actor);

	public:
		OvTools::Eventing::Event<OvCore::ECS::Actor&> ActorSelectedEvent;
		OvTools::Eventing::Event<OvCore::ECS::Actor&> ActorUnselectedEvent;

	private:
        void _Draw_Impl() override;
        void DrawToolbar();
        void DrawSceneRow();
        void ApplyFilter();
        void RemoveActorWidget(OvCore::ECS::Actor* p_actor);
        OvCore::SceneSystem::SceneManager& m_sceneManager;
        uint64_t m_destroyedListener = 0, m_unloadListener = 0;
        OvCore::ECS::Actor* m_selectedActor = nullptr;
        bool m_revealSelection = false;
        uint64_t m_selectionAnchor = 0;
        std::string m_filter;
        std::string m_searchInput;
        std::unique_ptr<OvUI::Widgets::Menu::MenuList> m_createMenu;
        OvUI::Internal::TextureID m_sceneIcon;
        bool m_sceneExpanded = true;
        bool m_actorsIndented = false;
        OvUI::Widgets::Texts::Text* m_status = nullptr;
        std::vector<OvUI::Widgets::Layout::TreeNode*> m_nodesToCollapse;
		std::unordered_map<OvCore::ECS::Actor*, OvUI::Widgets::Layout::TreeNode*> m_widgetActorLink;
		OvUI::Internal::WidgetContainer& m_actions;
		OvUI::Widgets::Layout::Group& m_actors;
	};
}
