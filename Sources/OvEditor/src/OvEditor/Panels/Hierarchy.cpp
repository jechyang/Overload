/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include "OvEditor/Panels/Hierarchy.h"
#include "OvEditor/Core/EditorActions.h"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <unordered_set>
#include <imgui.h>

#include <OvUI/Styling/Style.h>
#include <OvUI/Widgets/Buttons/Button.h>
#include <OvUI/Widgets/Selection/CheckBox.h>
#include <OvUI/Widgets/Visual/Separator.h>
#include <OvUI/Plugins/DDSource.h>
#include <OvUI/Plugins/DDTarget.h>

#include <OvDebug/Logger.h>

#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Helpers/GUIHelpers.h>
#include <OvCore/Helpers/GUIDrawer.h>

#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/ECS/Components/CPointLight.h>
#include <OvCore/ECS/Components/CDirectionalLight.h>
#include <OvCore/ECS/Components/CSpotLight.h>
#include <OvCore/ECS/Components/CAmbientBoxLight.h>
#include <OvCore/ECS/Components/CAmbientSphereLight.h>
#include <OvCore/ECS/Components/CPhysicalBox.h>
#include <OvCore/ECS/Components/CPhysicalSphere.h>
#include <OvCore/ECS/Components/CPhysicalCapsule.h>
#include <OvCore/ECS/Components/CAudioSource.h>
#include <OvCore/ECS/Components/CAudioListener.h>

#include <OvUI/Plugins/ContextualMenu.h>
#include <OvWindowing/Dialogs/MessageBox.h>
#include <OvWindowing/Dialogs/SaveFileDialog.h>

#include <OvTools/Utils/PathParser.h>

#include "OvEditor/Core/EditorResources.h"
#include "OvEditor/Utils/ActorCreationMenu.h"

namespace
{
	const OvCore::ECS::Actor* GetPrefabInstanceRoot(const OvCore::ECS::Actor& p_actor)
	{
		const OvCore::ECS::Actor* current = &p_actor;

		while (current)
		{
			if (current->HasPrefabSource())
			{
				return current;
			}

			current = current->GetParent();
		}

		return nullptr;
	}

	bool IsPartOfPrefabInstance(const OvCore::ECS::Actor& p_actor)
	{
		return GetPrefabInstanceRoot(p_actor) != nullptr;
	}
}

class ActorContextualMenu : public OvUI::Plugins::ContextualMenu
{
public:
	ActorContextualMenu(OvCore::ECS::Actor* p_target) :
		m_targetActor(p_target)
	{
		using namespace OvUI::Panels;
		using namespace OvUI::Widgets;
		using namespace OvUI::Widgets::Menu;
		using namespace OvCore::ECS::Components;

		if (m_targetActor)
		{
			auto& focusButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Focus");
			focusButton.ClickedEvent += [this]
			{
				if (auto* target = GetTargetActor())
				{
					EDITOR_EXEC(MoveToTarget(*target));
				}
			};

			auto& copyButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Copy");
			copyButton.ClickedEvent += [this]
			{
				if (auto* target = GetTargetActor())
				{
					EDITOR_EXEC(CopyActor(*target));
				}
			};

			auto& duplicateButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Duplicate");
			duplicateButton.ClickedEvent += [this]
			{
				if (auto* target = GetTargetActor())
				{
                    auto* scene = EDITOR_CONTEXT(sceneManager).GetCurrentScene();
                    const auto lifetime = scene->GetLifetimeToken();
                    const auto guid = target->GetGUID();
                    EDITOR_EXEC(DelayAction([scene, lifetime, guid]
                    {
                        if (EDITOR_CONTEXT(sceneManager).GetCurrentScene() != scene || scene->GetLifetimeToken() != lifetime) return;
                        if (auto* actor = scene->FindActorByGUID(guid))
                        {
                            if (EDITOR_EXEC(IsActorSelected(*actor))) EDITOR_EXEC(DuplicateSelectedActors());
                            else EDITOR_EXEC(DuplicateActor(*actor));
                        }
                    }, 0));
				}
			};

			auto& pasteButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Paste");
			pasteButton.ClickedEvent += [this]
			{
				if (auto* target = GetTargetActor())
				{
                    auto* scene = EDITOR_CONTEXT(sceneManager).GetCurrentScene();
                    const auto lifetime = scene->GetLifetimeToken();
                    const auto guid = target->GetGUID();
                    EDITOR_EXEC(DelayAction([scene, lifetime, guid]
                    {
                        if (EDITOR_CONTEXT(sceneManager).GetCurrentScene() != scene || scene->GetLifetimeToken() != lifetime) return;
                        if (auto* actor = scene->FindActorByGUID(guid)) EDITOR_EXEC(PasteActor(actor));
                    }, 0));
				}
			};
			
			auto& saveAsPrefabButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Save as Prefab...");
			saveAsPrefabButton.ClickedEvent += [this]
			{
				auto* target = GetTargetActor();
				if (!target)
				{
					return;
				}

				OvWindowing::Dialogs::SaveFileDialog dialog("Save Prefab");
				dialog.SetInitialDirectory(EDITOR_CONTEXT(projectAssetsPath).string());
				dialog.SetInitialFilename(target->GetName());
				dialog.DefineExtension("Overload Prefab", ".ovprefab");
				dialog.Show();

				if (!dialog.HasSucceeded())
				{
					return;
				}

				if (dialog.IsFileExisting())
				{
					OvWindowing::Dialogs::MessageBox message(
						"File already exists!",
						"The file \"" + dialog.GetSelectedFileName() + "\" already exists.\n\nOverwriting this file will replace the previous prefab content.\n\nAre you ok with that?",
						OvWindowing::Dialogs::MessageBox::EMessageType::WARNING,
						OvWindowing::Dialogs::MessageBox::EButtonLayout::YES_NO,
						true
					);

					if (message.GetUserAction() != OvWindowing::Dialogs::MessageBox::EUserAction::YES)
					{
						return;
					}
				}

				EDITOR_EXEC(SaveActorAsPrefab(*target, dialog.GetSelectedFilePath()));
			};

			auto& applyToPrefabButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Apply to Prefab");
			m_applyToPrefabButton = &applyToPrefabButton;
			applyToPrefabButton.ClickedEvent += [this]
			{
				if (auto* target = GetTargetActor())
				{
					EDITOR_EXEC(ApplyActorToPrefab(*target));
				}
			};

			auto& revertToPrefabButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Revert to Prefab");
			m_revertToPrefabButton = &revertToPrefabButton;
			revertToPrefabButton.ClickedEvent += [this]
			{
				if (auto* target = GetTargetActor())
				{
					EDITOR_EXEC(RevertActorToPrefab(*target));
				}
			};

			auto& deleteButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Delete");
			deleteButton.ClickedEvent += [this]
			{
				if (auto* target = GetTargetActor())
				{
                    if (EDITOR_EXEC(IsActorSelected(*target))) EDITOR_EXEC(DestroySelectedActors());
                    else EDITOR_EXEC(DestroyActor(*target));
				}
			};

			auto& renameMenu = CreateWidget<OvUI::Widgets::Menu::MenuList>("Rename to...");

			auto& nameEditor = renameMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
			nameEditor.selectAllOnClick = true;

			renameMenu.ClickedEvent += [this, &nameEditor]
			{
				if (auto* target = GetTargetActor())
				{
					nameEditor.content = target->GetName();
				}
			};

			nameEditor.EnterPressedEvent += [this](std::string p_newName)
			{
				if (auto* target = GetTargetActor())
				{
					EDITOR_EXEC(BeginSceneEdit("Rename entity"));
					target->SetName(p_newName);
				}
			};
		}
		else
		{
			auto& pasteButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Paste");
			pasteButton.ClickedEvent += [this]
			{
				EDITOR_EXEC(DelayAction(EDITOR_BIND(PasteActor, nullptr), 0));
			};
		}

		auto& createActor = CreateWidget<OvUI::Widgets::Menu::MenuList>("Create...");

		OvEditor::Utils::ActorCreationMenu::GenerateActorCreationMenu(
			createActor,
			m_targetActor
		);
	}

	virtual void Execute(OvUI::Plugins::EPluginExecutionContext p_context) override
	{
		if (m_applyToPrefabButton || m_revertToPrefabButton)
		{
			auto* target = GetTargetActor();
			const bool canEditPrefab = target && GetPrefabInstanceRoot(*target) != nullptr;

			if (m_applyToPrefabButton)
			{
				m_applyToPrefabButton->enabled = canEditPrefab;
			}

			if (m_revertToPrefabButton)
			{
				m_revertToPrefabButton->enabled = canEditPrefab;
			}
		}

		if (m_widgets.size() > 0)
			OvUI::Plugins::ContextualMenu::Execute(p_context);
	}

private:
	OvCore::ECS::Actor* GetTargetActor() const
	{
		if (!m_targetActor)
		{
			return nullptr;
		}

		auto* currentScene = EDITOR_CONTEXT(sceneManager).GetCurrentScene();
		if (!currentScene)
		{
			return nullptr;
		}

		const auto& actors = currentScene->GetActors();
		const bool actorIsStillInScene =
			std::find(actors.begin(), actors.end(), m_targetActor) != actors.end();

		return actorIsStillInScene ? m_targetActor : nullptr;
	}

	OvCore::ECS::Actor* m_targetActor = nullptr;
	OvUI::Widgets::Menu::MenuItem* m_applyToPrefabButton = nullptr;
	OvUI::Widgets::Menu::MenuItem* m_revertToPrefabButton = nullptr;
};

void ExpandTreeNode(OvUI::Widgets::Layout::TreeNode& p_toExpand)
{
	p_toExpand.Open();

	if (p_toExpand.HasParent())
	{
		if (auto parent = dynamic_cast<OvUI::Widgets::Layout::TreeNode*>(p_toExpand.GetParent()); parent)
		{
			ExpandTreeNode(*parent);
		}
	}
}

void ExpandTreeNodeAndEnable(OvUI::Widgets::Layout::TreeNode& p_toExpand, std::vector<OvUI::Widgets::Layout::TreeNode*>& nodesToCollapse)
{
	if (!p_toExpand.IsOpened())
	{
		p_toExpand.Open();
		if (std::find(nodesToCollapse.begin(), nodesToCollapse.end(), &p_toExpand) == nodesToCollapse.end())
			nodesToCollapse.push_back(&p_toExpand);
	}

	p_toExpand.enabled = true;

	if (p_toExpand.HasParent())
	{
		if (auto parent = dynamic_cast<OvUI::Widgets::Layout::TreeNode*>(p_toExpand.GetParent()); parent)
		{
			ExpandTreeNodeAndEnable(*parent, nodesToCollapse);
		}
	}
}

OvEditor::Panels::Hierarchy::Hierarchy
(
	const std::string & p_title,
	bool p_opened,
	const OvUI::Settings::PanelWindowSettings& p_windowSettings
) :
	PanelWindow(p_title, p_opened, p_windowSettings),
    m_sceneManager(EDITOR_CONTEXT(sceneManager)),
	m_actions(CreateWidget<OvUI::Widgets::Layout::Group>()),
	m_actors(CreateWidget<OvUI::Widgets::Layout::Group>())
{
	const auto searchIconID = []() -> OvUI::Internal::TextureID {
		if (auto* tex = EDITOR_CONTEXT(editorResources)->GetTexture("Search"))
			return tex->RetainTexture();
		return 0u;
	}();

	auto& createButton = m_actions.CreateWidget<OvUI::Widgets::Buttons::Button>("+ Create");
	createButton.tooltip = "Create an empty entity. Right-click the tree for primitives, lights and prefabs.";
	createButton.lineBreak = false;
	createButton.ClickedEvent += [] { EDITOR_EXEC(CreateEmptyActor(true)); };
	auto& expand = m_actions.CreateWidget<OvUI::Widgets::Buttons::Button>("Expand All");
	expand.lineBreak = false;
	expand.ClickedEvent += [this] { for (auto& [actor, node] : m_widgetActorLink) node->Open(); };
	m_actions.CreateWidget<OvUI::Widgets::Buttons::Button>("Collapse All").ClickedEvent += [this]
	{
		SetSearchFilter("");
		for (auto& [actor, node] : m_widgetActorLink) node->Close();
	};
	m_searchBar = &OvCore::Helpers::GUIDrawer::DrawSearchBar(m_actions, searchIconID);
	m_searchBar->tooltip = "Filter entities by name or tag";
	m_searchBar->ContentChangedEvent += [this](const std::string& content) { SetSearchFilter(content); };
	m_status = &m_actions.CreateWidget<OvUI::Widgets::Texts::Text>();
	m_actions.CreateWidget<OvUI::Widgets::Visual::Separator>();

	auto& windowDDTarget = AddPlugin<OvUI::Plugins::DDTarget<std::pair<OvCore::ECS::Actor*, OvUI::Widgets::Layout::TreeNode*>>>("Actor");
	windowDDTarget.showYellowRect = false;
	windowDDTarget.DataReceivedEvent += [this](std::pair<OvCore::ECS::Actor*, OvUI::Widgets::Layout::TreeNode*> p_element)
	{
		if (auto found = m_widgetActorLink.find(p_element.first);
			found != m_widgetActorLink.end() && found->second == p_element.second && p_element.first->IsAlive())
			EDITOR_EXEC(ReparentSelection(*p_element.first, nullptr));
	};

	AddPlugin<ActorContextualMenu>(nullptr);

    // EditorActions owns its events and dies before panels; external sources are explicitly disconnected.
	EDITOR_EVENT(ActorUnselectedEvent) += std::bind(&Hierarchy::UnselectActorsWidgets, this);
	m_unloadListener = m_sceneManager.SceneUnloadEvent += std::bind(&Hierarchy::Clear, this);
	m_destroyedListener = OvCore::ECS::Actor::DestroyedEvent += std::bind(&Hierarchy::DeleteActorByInstance, this, std::placeholders::_1);
	EDITOR_EVENT(ActorSelectedEvent) += std::bind(&Hierarchy::SelectActorByInstance, this, std::placeholders::_1);
}

OvEditor::Panels::Hierarchy::~Hierarchy()
{
    m_sceneManager.SceneUnloadEvent -= m_unloadListener;
    OvCore::ECS::Actor::DestroyedEvent -= m_destroyedListener;
}

void OvEditor::Panels::Hierarchy::Clear()
{
    m_selectionAnchor = 0;
    m_nodesToCollapse.clear();
	EDITOR_EXEC(UnselectActor());
	UnselectActorsWidgets();

	while (!m_widgetActorLink.empty()) RemoveActorWidget(m_widgetActorLink.begin()->first);
}

void OvEditor::Panels::Hierarchy::UnselectActorsWidgets()
{
	m_selectedActor = nullptr;
	m_revealSelection = false;
	for (auto& widget : m_widgetActorLink)
		widget.second->selected = false;
}

void OvEditor::Panels::Hierarchy::SelectActorByInstance(OvCore::ECS::Actor& p_actor)
{
	UnselectActorsWidgets();
	m_selectedActor = &p_actor;
	m_revealSelection = true;
}

void OvEditor::Panels::Hierarchy::SelectActorByWidget(OvUI::Widgets::Layout::TreeNode & p_widget)
{
	p_widget.selected = true;
	p_widget.ScrollIntoView();

	if (p_widget.HasParent())
	{
		if (auto parent = dynamic_cast<OvUI::Widgets::Layout::TreeNode*>(p_widget.GetParent()); parent)
		{
			ExpandTreeNode(*parent);
		}
	}
}

void OvEditor::Panels::Hierarchy::SelectActorFromClick(OvCore::ECS::Actor& actor, bool toggle, bool range)
{
    if (range && m_selectionAnchor)
    {
        std::unordered_map<OvUI::Widgets::Layout::TreeNode*, OvCore::ECS::Actor*> lookup;
        for (auto& [target, node] : m_widgetActorLink) lookup[node] = target;
        std::vector<OvCore::ECS::Actor*> visible;
        auto visit = [&](OvUI::Internal::WidgetContainer& root, auto&& self) -> void {
            for (auto [widget, mode] : root.GetWidgets())
                if (auto* node = dynamic_cast<OvUI::Widgets::Layout::TreeNode*>(widget); node && node->enabled && !node->IsDestroyed())
                {
                    if (lookup.contains(node)) visible.push_back(lookup.at(node));
                    if (node->IsOpened()) self(*node, self);
                }
        };
        visit(m_actors, visit);
        auto begin = std::find_if(visible.begin(), visible.end(), [this](auto* target) { return target->GetGUID() == m_selectionAnchor; });
        auto end = std::find(visible.begin(), visible.end(), &actor);
        if (begin != visible.end() && end != visible.end())
        {
            if (begin > end) std::swap(begin, end);
            auto selection = toggle ? EDITOR_EXEC(GetSelectedActors()) : std::vector<OvCore::ECS::Actor*>{};
            selection.insert(selection.begin(), &actor);
            selection.insert(selection.end(), begin, end + 1);
            EDITOR_EXEC(SetSelection(selection));
            return;
        }
    }
    m_selectionAnchor = actor.GetGUID();
    if (toggle) EDITOR_EXEC(ToggleActorSelection(actor)); else EDITOR_EXEC(SelectActor(actor));
}

void OvEditor::Panels::Hierarchy::_Draw_Impl()
{
	// CreatedEvent fires before scene ownership is assigned. Reconcile the
	// committed scene at the UI boundary, including runtime and prefab batches.
	Synchronize();
	PanelWindow::_Draw_Impl();
}

void OvEditor::Panels::Hierarchy::Synchronize()
{
	const auto selection = EDITOR_EXEC(GetSelectedActors());
	const std::unordered_set<OvCore::ECS::Actor*> selectedActors(selection.begin(), selection.end());
	auto* scene = m_sceneManager.GetCurrentScene();
	std::unordered_set<OvCore::ECS::Actor*> live;
	if (scene)
	{
		live.reserve(scene->GetActors().size());
		for (auto* actor : scene->GetActors())
			if (actor->IsAlive()) live.insert(actor);
	}
	std::vector<OvCore::ECS::Actor*> removed;
	for (const auto& [actor, node] : m_widgetActorLink)
		if (!live.contains(actor)) removed.push_back(actor);
	for (auto* actor : removed) RemoveActorWidget(actor);
	if (scene)
		for (auto* actor : scene->GetActors())
			if (live.contains(actor) && !m_widgetActorLink.contains(actor)) AddActorByInstance(*actor);

	// Detach changed nodes first so reversed parent/child relations cannot
	// temporarily create a cycle in widget ownership.
	for (auto& [actor, node] : m_widgetActorLink)
	{
		auto parent = m_widgetActorLink.find(actor->GetParent());
		OvUI::Internal::WidgetContainer* destination = parent == m_widgetActorLink.end() ? &m_actors : parent->second;
		if (node->GetParent() != destination && node->HasParent()) node->GetParent()->UnconsiderWidget(*node);
	}
	if (scene) for (auto* actor : scene->GetActors())
	{
		if (!live.contains(actor)) continue;
		auto* node = m_widgetActorLink.at(actor);
		if (!node->HasParent())
		{
			auto parent = m_widgetActorLink.find(actor->GetParent());
			OvUI::Internal::WidgetContainer* destination = parent == m_widgetActorLink.end() ? &m_actors : parent->second;
			destination->ConsiderWidget(*node);
		}
		node->name = actor->GetName();
		node->leaf = actor->GetChildren().empty();
		node->selected = selectedActors.contains(actor);
	}
	ApplyFilter();
	if (m_revealSelection)
	{
		if (auto selected = m_widgetActorLink.find(m_selectedActor); selected != m_widgetActorLink.end())
		{
			// Explicit selection/creation must remain visible through an old search.
			if (!selected->second->enabled) SetSearchFilter("");
			SelectActorByWidget(*selected->second);
			m_revealSelection = false;
		}
	}
}

void OvEditor::Panels::Hierarchy::SetSearchFilter(const std::string& p_filter)
{
	m_filter = p_filter;
	m_searchBar->content = p_filter;
	std::transform(m_filter.begin(), m_filter.end(), m_filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	ApplyFilter();
}

void OvEditor::Panels::Hierarchy::ApplyFilter()
{
	if (m_filter.empty())
	{
		for (auto* node : m_nodesToCollapse) node->Close();
		m_nodesToCollapse.clear();
	}
	std::vector<OvUI::Widgets::Layout::TreeNode*> matches;
	for (auto& [actor, node] : m_widgetActorLink)
	{
		node->enabled = true;
		if (!m_filter.empty())
		{
			auto text = actor->GetName() + " " + actor->GetTag();
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			node->enabled = text.find(m_filter) != std::string::npos;
		}
		if (node->enabled) matches.push_back(node);
	}
	if (!m_filter.empty())
		for (auto* node : matches)
			if (auto* parent = dynamic_cast<OvUI::Widgets::Layout::TreeNode*>(node->GetParent()))
				ExpandTreeNodeAndEnable(*parent, m_nodesToCollapse);
	m_status->content = m_filter.empty()
		? std::to_string(m_widgetActorLink.size()) + " entities"
		: std::to_string(matches.size()) + " matches / " + std::to_string(m_widgetActorLink.size()) + " entities";
	if (m_widgetActorLink.empty()) m_status->content += " - Use + Create to get started";
	else if (matches.empty()) m_status->content += " - No matching entities";
}

void OvEditor::Panels::Hierarchy::RemoveActorWidget(OvCore::ECS::Actor* p_actor)
{
	if (m_selectedActor == p_actor) { m_selectedActor = nullptr; m_revealSelection = false; }
	if (auto found = m_widgetActorLink.find(p_actor); found != m_widgetActorLink.end())
	{
		auto* node = found->second;
		// Preserve surviving children; disable dead callbacks immediately even
		// during an active draw traversal. Deletion waits for widget collection.
		const auto children = node->GetWidgets();
		for (auto [child, mode] : children)
		{
			node->UnconsiderWidget(*child);
			m_actors.ConsiderWidget(*child);
		}
		std::erase(m_nodesToCollapse, node);
		node->enabled = false;
		node->Destroy();
		m_widgetActorLink.erase(found);
	}
}

void OvEditor::Panels::Hierarchy::DeleteActorByInstance(OvCore::ECS::Actor& p_actor)
{
	RemoveActorWidget(&p_actor);
}

void OvEditor::Panels::Hierarchy::AddActorByInstance(OvCore::ECS::Actor & p_actor)
{
	if (p_actor.GetScene() != m_sceneManager.GetCurrentScene() || !p_actor.IsAlive() || m_widgetActorLink.contains(&p_actor)) return;
	auto& textSelectable = m_actors.CreateWidget<OvUI::Widgets::Layout::TreeNode>(p_actor.GetName(), true);
	textSelectable.leaf = true;

	const auto actorIconID = OvCore::Helpers::GUIHelpers::GetActorIconID();
	const auto prefabIconID = OvCore::Helpers::GUIHelpers::GetIconForFileType(OvTools::Utils::PathParser::EFileType::PREFAB);

	textSelectable.iconTextureID = IsPartOfPrefabInstance(p_actor) && prefabIconID.id != 0 ? prefabIconID : actorIconID;

	textSelectable.AddPlugin<ActorContextualMenu>(&p_actor);
	textSelectable.AddPlugin<OvUI::Plugins::DDSource<std::pair<OvCore::ECS::Actor*, OvUI::Widgets::Layout::TreeNode*>>>("Actor", "Attach to...", std::make_pair(&p_actor, &textSelectable));
	textSelectable.AddPlugin<OvUI::Plugins::DDTarget<std::pair<OvCore::ECS::Actor*, OvUI::Widgets::Layout::TreeNode*>>>("Actor").DataReceivedEvent += [this, &p_actor](std::pair<OvCore::ECS::Actor*, OvUI::Widgets::Layout::TreeNode*> p_element)
	{
		const auto source = m_widgetActorLink.find(p_element.first);
		if (source == m_widgetActorLink.end() || source->second != p_element.second || !p_element.first->IsAlive() || &p_actor == p_element.first)
		{
			return;
		}

		if (p_actor.IsDescendantOf(p_element.first))
		{
			OVLOG_WARNING("Cannot attach \"" + p_element.first->GetName() + "\" to \"" + p_actor.GetName() + "\" because it is a descendant of the latter.");
			return;
		}

		EDITOR_EXEC(ReparentSelection(*p_element.first, &p_actor));
	};
	auto& dispatcher = textSelectable.AddPlugin<OvUI::Plugins::DataDispatcher<std::string>>();

	OvCore::ECS::Actor* targetPtr = &p_actor;
	dispatcher.RegisterGatherer([targetPtr, &textSelectable, actorIconID, prefabIconID]
	{
		const bool isActive = targetPtr->IsActive();
		textSelectable.labelColor = isActive
			? OVUI_STYLE(Text)
			: OVUI_STYLE(TextDisabled);

		textSelectable.iconTextureID = IsPartOfPrefabInstance(*targetPtr) && prefabIconID.id != 0 ? prefabIconID : actorIconID;

		return targetPtr->GetName();
	});

	m_widgetActorLink[targetPtr] = &textSelectable;

	textSelectable.ClickedEvent += [this, &p_actor] { SelectActorFromClick(p_actor, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift); };
	textSelectable.DoubleClickedEvent += EDITOR_BIND(MoveToTarget, std::ref(p_actor));
}
