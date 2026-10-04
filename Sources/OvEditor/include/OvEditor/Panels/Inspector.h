/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once
#include <OvTools/Utils/CallbackScope.h>

#include <OvRendering/Resources/Loaders/TextureLoader.h>

#include <OvUI/Panels/PanelWindow.h>
#include <OvUI/Widgets/Layout/GroupCollapsable.h>
#include <OvUI/Widgets/Visual/Separator.h>
#include <OvUI/Widgets/Selection/CheckBox.h>

#include "OvEditor/Panels/Hierarchy.h"
#include "OvEditor/Panels/AssetBrowser.h"

namespace OvEditor::Panels
{
	class Inspector : public OvUI::Panels::PanelWindow
	{
	public:
        // Omitted callbacks use EditorActions; hosts/tests may provide their event loop.
        struct Callbacks
        {
            std::function<void(std::function<void()>)> defer;
            std::function<void(OvCore::ECS::Actor&)> selected;
            std::function<void(OvCore::ECS::Actor&)> unselected;
        };
		/**
		* Constructor
		* @param p_title
		* @param p_opened
		* @param p_windowSettings
		*/
		Inspector
		(
			const std::string& p_title,
			bool p_opened,
			const OvUI::Settings::PanelWindowSettings& p_windowSettings,
            Callbacks callbacks = {}
		);

		/**
		* Destructor
		*/
		~Inspector();

		/**
		* Focus the given actor
		* @param p_target
		*/
		void FocusActor(OvCore::ECS::Actor& p_target);
        void FocusActors(const std::vector<OvCore::ECS::Actor*>& targets);
        const std::vector<OvCore::ECS::Actor*>& GetTargetActors() const { return m_targets; }

		/**
		* Unfocus the currently targeted actor
		*/
		void UnFocus();

		/**
		* Returns the currently selected actor
		*/
		OvTools::Utils::OptRef<OvCore::ECS::Actor> GetTargetActor() const;

		/**
		* Refresh the inspector
		*/
		void Refresh();

	private:
		void _Draw_Impl() override;
		void _Populate();
        void _QueueRefresh();
        void _InvalidateView();
		void _PopulateActorInfo();
		void _PopulateActorComponents();
		void _PopulateActorBehaviours();
		void _DrawAddSection();
		void _DrawComponent(OvCore::ECS::Components::AComponent& p_component, int p_index, int p_total);
		void _DrawBehaviour(OvCore::ECS::Components::Behaviour& p_behaviour, int p_index, int p_total);

	private:
		OvTools::Utils::OptRef<OvCore::ECS::Actor> m_targetActor = std::nullopt;
        std::vector<OvCore::ECS::Actor*> m_targets;
        struct Listeners { OvCore::ECS::Actor* actor; uint64_t added, removed, behaviourAdded, behaviourRemoved; };
        std::vector<Listeners> m_targetListeners;
        std::vector<std::unique_ptr<OvUI::Internal::WidgetContainer>> m_batchControls;
		OvUI::Widgets::Layout::Group* m_content;
		OvUI::Widgets::Layout::Group* m_emptyState;

        Callbacks m_callbacks;
        OvTools::Utils::CallbackScope m_focusCallbacks;
        OvTools::Utils::CallbackScope m_viewCallbacks;
        bool m_refreshPending = false;
		uint64_t m_componentAddedListener	= 0;
		uint64_t m_componentRemovedListener = 0;
		uint64_t m_behaviourAddedListener	= 0;
		uint64_t m_behaviourRemovedListener = 0;
		uint64_t m_destroyedListener		= 0;
	};
}
