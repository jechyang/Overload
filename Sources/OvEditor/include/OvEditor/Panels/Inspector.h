/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once
#include <OvTools/Filesystem/IniFile.h>
#include <OvTools/Utils/CallbackScope.h>
#include <OvTools/Utils/PathParser.h>
#include <OvEditor/Panels/InspectorPropertyContext.h>

#include <OvRendering/Resources/Loaders/TextureLoader.h>

#include <OvUI/Panels/PanelWindow.h>
#include <OvUI/Widgets/Layout/Columns.h>
#include <OvUI/Widgets/Layout/GroupCollapsable.h>
#include <OvUI/Widgets/Visual/Separator.h>
#include <OvUI/Widgets/Selection/CheckBox.h>
#include <OvUI/Widgets/Texts/TextDisabled.h>

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

		/**
		* Open the searchable "Add Component" picker for the current selection
		*/
		void OpenAddComponentPicker();
        void FocusActors(const std::vector<OvCore::ECS::Actor*>& targets);
        const std::vector<OvCore::ECS::Actor*>& GetTargetActors() const { return m_targets; }
        // Scene selection remains independent when the Inspector is locked.
        const std::vector<OvCore::ECS::Actor*>& GetSelectedActors() const { return m_selection; }
        void FocusAsset(const std::string& p_path);
        const std::string& GetTargetAsset() const { return m_targetAsset; }
        const std::string& GetSelectedAsset() const { return m_selectedAsset; }
        OvTools::Filesystem::IniFile* GetAssetMetadata() { return m_assetMetadata.get(); }
        void ApplyAssetSettings();
        void RevertAssetSettings();
        void ResetAssetSettings();
        void PreviewAsset();
        void SetLocked(bool locked);
        bool IsLocked() const { return m_locked; }
        std::vector<uint64_t> GetLockedTargetGUIDs() const;
        void RestoreLockedTargets(const std::vector<uint64_t>& guids);
        void SetSearchFilter(const std::string& filter);
        const std::string& GetSearchFilter() const { return m_searchFilter; }
        bool CopyComponentValues(const std::string& type);
        bool PasteComponentValues(const std::string& type);
        bool ResetComponentValues(const std::string& type);
        bool CanPasteComponentValues(const std::string& type) const;
        // Unity "Paste Component As New": adds the copied component type to every target.
        bool CanPasteComponentAsNew() const;
        bool PasteComponentAsNew();
        bool RemoveComponents(const std::string& type);
        bool CanSelectBack() const;
        bool CanSelectForward() const;
        bool SelectBack();
        bool SelectForward();
        void ClearSelectionHistory();

		/**
		* Clear scene selection; locked inspection targets are retained.
		*/
		void UnFocus();

		/**
		* Returns the primary inspected actor, which may differ from scene selection.
		*/
		OvTools::Utils::OptRef<OvCore::ECS::Actor> GetTargetActor() const;

		/**
		* Refresh the inspector
		*/
		void Refresh();

	private:
		void _Draw_Impl() override;
		void _Populate();
        void _PopulateAsset();
        void _PopulateAssetSettings(OvUI::Widgets::Layout::Columns<2>& p_columns, OvTools::Utils::PathParser::EFileType p_fileType);
        void _SetTargets(const std::vector<OvCore::ECS::Actor*>& targets);
        void _SetAssetTarget(const std::string& p_resourcePath);
        void _ClearTargets();
        void _DrawToolbar();
        void _ApplySearchFilter();
        std::vector<OvCore::ECS::Actor*> _ResolveHistoryEntry(const std::vector<uint64_t>& guids) const;
        void _RecordSelectionHistory(const std::vector<OvCore::ECS::Actor*>& selection);
        void _AttachComponentMenu(OvUI::Widgets::Layout::GroupCollapsable& header, const std::string& type, bool removable);
        bool _ApplyComponentValues(const std::string& type, const std::string& xml, const std::string& label);
        void _QueueRefresh();
        void _InvalidateView();
        InspectorBindings::PropertyContext _PropertyContext(const InspectorBindings::PropertyDefaults* defaults = nullptr);
		void _PopulateActorInfo();
		void _PopulateActorComponents();
		void _PopulateActorBehaviours();
		void _DrawAddSection();
		void _DrawComponent(OvCore::ECS::Components::AComponent& p_component, int p_index, int p_total);
		void _DrawBehaviour(OvCore::ECS::Components::Behaviour& p_behaviour, int p_index, int p_total);

	private:
		OvTools::Utils::OptRef<OvCore::ECS::Actor> m_targetActor = std::nullopt;
        std::vector<OvCore::ECS::Actor*> m_targets;
        std::vector<OvCore::ECS::Actor*> m_selection;
        std::string m_targetAsset;
        std::string m_selectedAsset;
        mutable std::string m_restorableLockedAsset;
        std::unique_ptr<OvTools::Filesystem::IniFile> m_assetMetadata;
        std::vector<std::vector<uint64_t>> m_selectionHistory;
        size_t m_selectionHistoryIndex = 0;
        bool m_navigatingSelectionHistory = false;
        bool m_locked = false;
        bool m_proportionalScale = false;
        std::string m_searchFilter, m_clipboardType, m_clipboardXML;
        InspectorBindings::PropertyClipboard m_propertyClipboard;
        std::map<std::string,InspectorBindings::PropertyDefaults> m_propertyDefaults;
        std::vector<OvUI::Widgets::Layout::GroupCollapsable*> m_sections;
        OvUI::Widgets::Texts::TextDisabled* m_searchStatus = nullptr;
        OvCore::SceneSystem::SceneManager* m_sceneManager = nullptr;
        uint64_t m_sceneUnloadListener = 0;
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
