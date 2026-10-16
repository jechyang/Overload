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
#include <array>
#include <cfloat>
#include <filesystem>
#include <functional>
#include <unordered_set>
#include <imgui.h>
#include <imgui_internal.h>

#include <OvUI/Styling/Style.h>
#include <OvUI/Widgets/Buttons/Button.h>
#include <OvUI/Widgets/Selection/CheckBox.h>
#include <OvUI/Widgets/Texts/TextDisabled.h>
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
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvCore/ECS/Components/CModelRenderer.h>

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

	class HierarchyCustomDraw final : public OvUI::Widgets::AWidget
	{
	public:
		explicit HierarchyCustomDraw(std::function<void()> p_draw) : m_draw(std::move(p_draw)) {}
	private:
		void _Draw_Impl() override { m_draw(); }
		std::function<void()> m_draw;
	};
}

class ActorContextualMenu : public OvUI::Plugins::ContextualMenu
{
public:
	ActorContextualMenu(OvCore::ECS::Actor* p_target, std::function<void()> p_rename = {}) :
		m_targetActor(p_target),
		m_rename(std::move(p_rename))
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

			auto& createChildButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Create Empty Child", "Alt+Shift+N");
			createChildButton.ClickedEvent += [this]
			{
				if (auto* target = GetTargetActor())
				{
					EDITOR_EXEC(CreateEmptyActor(true, target, ""));
				}
			};

			CreateWidget<OvUI::Widgets::Visual::Separator>();

			auto& copyButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Copy", "Ctrl+C");
			copyButton.ClickedEvent += [this]
			{
				if (auto* target = GetTargetActor())
				{
					EDITOR_EXEC(CopyActor(*target));
				}
			};

			auto& duplicateButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Duplicate", "Ctrl+D");
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

			auto& pasteButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Paste", "Ctrl+V");
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
			
			CreateWidget<OvUI::Widgets::Visual::Separator>();

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

			CreateWidget<OvUI::Widgets::Visual::Separator>();

			auto& deleteButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Delete", "Delete");
			deleteButton.ClickedEvent += [this]
			{
				if (auto* target = GetTargetActor())
				{
                    if (EDITOR_EXEC(IsActorSelected(*target))) EDITOR_EXEC(DestroySelectedActors());
                    else EDITOR_EXEC(DestroyActor(*target));
				}
			};

			// Rename in place on the row, like Unity (also F2).
			auto& renameButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Rename", "F2");
			renameButton.ClickedEvent += [this]
			{
				if (GetTargetActor() && m_rename) m_rename();
			};

			CreateWidget<OvUI::Widgets::Visual::Separator>();
		}
		else
		{
			auto& createEmptyButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Create Empty", "Ctrl+Shift+N");
			createEmptyButton.ClickedEvent += [this]
			{
				EDITOR_EXEC(CreateEmptyActor(true, nullptr, ""));
			};

			auto& pasteButton = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Paste", "Ctrl+V");
			pasteButton.ClickedEvent += [this]
			{
				EDITOR_EXEC(DelayAction(EDITOR_BIND(PasteActor, nullptr), 0));
			};

			CreateWidget<OvUI::Widgets::Visual::Separator>();
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
	std::function<void()> m_rename;
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
	if (auto* sceneTexture = EDITOR_CONTEXT(editorResources)->GetTexture("Scene"))
		m_sceneIcon = OvUI::Internal::TextureID{ sceneTexture->RetainTexture() };

	m_createMenu = std::make_unique<OvUI::Widgets::Menu::MenuList>("Create");
	OvEditor::Utils::ActorCreationMenu::GenerateActorCreationMenu(*m_createMenu);

	// Unity layout: "+" create dropdown and search on one row, then the scene root row.
	m_actions.CreateWidget<HierarchyCustomDraw>([this] { DrawToolbar(); });
	m_status = &m_actions.CreateWidget<OvUI::Widgets::Texts::TextDisabled>();
	m_actions.CreateWidget<HierarchyCustomDraw>([this] { DrawSceneRow(); });

	// Entities are drawn as children of the scene row.
	CreateWidget<HierarchyCustomDraw>([this] { if (m_actorsIndented) ImGui::Unindent(); m_actorsIndented = false; });

	auto& windowDDTarget = AddPlugin<OvUI::Plugins::DDTarget<std::pair<OvCore::ECS::Actor*, OvUI::Widgets::Layout::TreeNode*>>>("Actor");
	windowDDTarget.showYellowRect = false;
	windowDDTarget.DataReceivedEvent += [this](std::pair<OvCore::ECS::Actor*, OvUI::Widgets::Layout::TreeNode*> p_element)
	{
		if (auto found = m_widgetActorLink.find(p_element.first);
			found != m_widgetActorLink.end() && found->second == p_element.second && p_element.first->IsAlive())
			EDITOR_EXEC(ReparentSelection(*p_element.first, nullptr));
	};

	auto& windowFileDDTarget = AddPlugin<OvUI::Plugins::DDTarget<std::pair<std::string, OvUI::Widgets::Layout::Group*>>>("File");
	windowFileDDTarget.showYellowRect = false;
	windowFileDDTarget.DataReceivedEvent += [this](auto p_data)
	{
		HandleFileDrop(p_data.first, nullptr);
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
	if (IsFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2, false))
		BeginRenameSelection();
	PanelWindow::_Draw_Impl();
}

bool OvEditor::Panels::Hierarchy::BeginRenameSelection()
{
	const auto selection = EDITOR_EXEC(GetSelectedActors());
	if (selection.size() != 1) return false;
	const auto found = m_widgetActorLink.find(selection.front());
	if (found == m_widgetActorLink.end()) return false;
	if (!found->second->enabled) SetSearchFilter("");
	m_sceneExpanded = true;
	SelectActorByWidget(*found->second);
	found->second->BeginRename();
	return true;
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
		OvUI::Internal::WidgetContainer* destination = parent == m_widgetActorLink.end() ? static_cast<OvUI::Internal::WidgetContainer*>(&m_actors) : parent->second;
		if (node->GetParent() != destination && node->HasParent()) node->GetParent()->UnconsiderWidget(*node);
	}
	if (scene) for (auto* actor : scene->GetActors())
	{
		if (!live.contains(actor)) continue;
		auto* node = m_widgetActorLink.at(actor);
		if (!node->HasParent())
		{
			auto parent = m_widgetActorLink.find(actor->GetParent());
			OvUI::Internal::WidgetContainer* destination = parent == m_widgetActorLink.end() ? static_cast<OvUI::Internal::WidgetContainer*>(&m_actors) : parent->second;
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
			m_sceneExpanded = true;
			SelectActorByWidget(*selected->second);
			m_revealSelection = false;
		}
	}
}

void OvEditor::Panels::Hierarchy::DrawToolbar()
{
	const float scale = OVUI_SCALE;
	ImGui::PushID("HierarchyToolbar");

	const float arrowSpace = 12.0f * scale;
	ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, { 0.0f, 0.5f });
	const bool openCreate = ImGui::Button("+##Create", { ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 2.0f + arrowSpace, 0.0f });
	ImGui::PopStyleVar();
	const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
	const float arrowScale = 0.6f;
	ImGui::RenderArrow(ImGui::GetWindowDrawList(), { max.x - arrowSpace, min.y + (max.y - min.y - ImGui::GetFontSize() * arrowScale) * 0.5f },
		ImGui::GetColorU32(ImGuiCol_Text), ImGuiDir_Down, arrowScale);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !ImGui::IsPopupOpen("##Create"))
		ImGui::SetTooltip("Create entities, primitives, lights and prefabs");
	if (openCreate) ImGui::OpenPopup("##Create");
	ImGui::SetNextWindowPos({ min.x, max.y + 2.0f * scale });
	if (ImGui::BeginPopup("##Create"))
	{
		m_createMenu->DrawWidgets();
		ImGui::EndPopup();
	}

	ImGui::SameLine(0.0f, 4.0f * scale);
	std::array<char, 256> search{};
	std::copy_n(m_searchInput.data(), std::min(m_searchInput.size(), search.size() - 1), search.data());
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::InputTextWithHint("##Search", "Search", search.data(), search.size()))
		SetSearchFilter(search.data());
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
		ImGui::SetTooltip("Filter entities by name or tag");

	ImGui::PopID();
}

void OvEditor::Panels::Hierarchy::DrawSceneRow()
{
	auto* scene = m_sceneManager.GetCurrentScene();
	m_actors.enabled = scene != nullptr;
	if (!scene) return;

	const auto source = m_sceneManager.GetCurrentSceneSourcePath();
	const std::string name = (source.empty() ? std::string{ "Untitled" } : std::filesystem::path{ source }.stem().string())
		+ (EDITOR_EXEC(IsSceneModified()) ? "*" : "");

	// A search always shows its matches.
	const bool searching = !m_filter.empty();
	ImGui::SetNextItemOpen(m_sceneExpanded || searching, ImGuiCond_Always);
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
	const bool open = ImGui::TreeNodeEx("##SceneRoot",
		ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick,
		"%s", "");
	ImGui::PopStyleColor(3);

	// Scene icon and name after the arrow.
	{
		const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
		const float icon = ImGui::GetTextLineHeight();
		const float x = min.x + ImGui::GetTreeNodeToLabelSpacing();
		const float y = (min.y + max.y - icon) * 0.5f;
		auto* drawList = ImGui::GetWindowDrawList();
		if (m_sceneIcon.id != 0)
		{
			m_sceneIcon.RetainForCapture();
			drawList->AddImage(m_sceneIcon.id, { x, y }, { x + icon, y + icon }, { 0.0f, 1.0f }, { 1.0f, 0.0f });
		}
		drawList->AddText({ x + icon + 4.0f * OVUI_SCALE, y }, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
	}
	if (ImGui::IsItemToggledOpen() && !searching) m_sceneExpanded = open;
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !source.empty())
		ImGui::SetTooltip("%s", source.c_str());

	// Dropping an entity or file on the scene row moves/places it at the root.
	if (ImGui::BeginDragDropTarget())
	{
		if (const auto* payload = ImGui::AcceptDragDropPayload("Actor"))
		{
			const auto element = *static_cast<const std::pair<OvCore::ECS::Actor*, OvUI::Widgets::Layout::TreeNode*>*>(payload->Data);
			if (auto found = m_widgetActorLink.find(element.first); found != m_widgetActorLink.end() && found->second == element.second && element.first->IsAlive())
				EDITOR_EXEC(ReparentSelection(*element.first, nullptr));
		}
		if (const auto* filePayload = ImGui::AcceptDragDropPayload("File"))
		{
			const auto* data = static_cast<const std::pair<std::string, OvUI::Widgets::Layout::Group*>*>(filePayload->Data);
			HandleFileDrop(data->first, nullptr);
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::BeginPopupContextItem("##SceneContext"))
	{
		if (ImGui::MenuItem("Save Scene", "Ctrl+S")) EDITOR_EXEC(DelayAction([] { EDITOR_EXEC(SaveSceneChanges()); }, 0));
		ImGui::Separator();
		if (ImGui::MenuItem("Expand All"))
		{
			m_sceneExpanded = true;
			for (auto& [actor, node] : m_widgetActorLink) node->Open();
		}
		if (ImGui::MenuItem("Collapse All"))
		{
			SetSearchFilter("");
			for (auto& [actor, node] : m_widgetActorLink) node->Close();
		}
		ImGui::Separator();
		if (ImGui::BeginMenu("Create"))
		{
			m_createMenu->DrawWidgets();
			ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}

	m_actors.enabled = open;
	m_actorsIndented = open;
	if (open) ImGui::Indent();
}

void OvEditor::Panels::Hierarchy::SetSearchFilter(const std::string& p_filter)
{
	m_filter = p_filter;
	m_searchInput = p_filter;
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
	// Like Unity, counts only appear while searching; an empty scene gets a hint.
	m_status->enabled = !m_filter.empty() || m_widgetActorLink.empty();
	if (m_widgetActorLink.empty()) m_status->content = "Empty scene. Use + to create an entity.";
	else if (matches.empty()) m_status->content = "No matching entities";
	else m_status->content = std::to_string(matches.size()) + " of " + std::to_string(m_widgetActorLink.size()) + " entities";
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

	textSelectable.AddPlugin<ActorContextualMenu>(&p_actor, [&textSelectable] { textSelectable.BeginRename(); });
	textSelectable.RenamedEvent += [&p_actor](std::string p_name)
	{
		EDITOR_EXEC(BeginSceneEdit("Rename entity"));
		p_actor.SetName(p_name);
	};
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
	textSelectable.AddPlugin<OvUI::Plugins::DDTarget<std::pair<std::string, OvUI::Widgets::Layout::Group*>>>("File").DataReceivedEvent += [this, &p_actor](auto p_data)
	{
		HandleFileDrop(p_data.first, &p_actor);
	};
	auto& dispatcher = textSelectable.AddPlugin<OvUI::Plugins::DataDispatcher<std::string>>();

	OvCore::ECS::Actor* targetPtr = &p_actor;
	dispatcher.RegisterGatherer([targetPtr, &textSelectable, actorIconID, prefabIconID]
	{
		// Unity colors: prefab instances blue, inactive entities dimmed.
		const bool prefab = IsPartOfPrefabInstance(*targetPtr);
		textSelectable.labelColor = !targetPtr->IsActive() ? OVUI_STYLE(TextDisabled) : prefab ? OVUI_STYLE(Accent) : OVUI_STYLE(Text);

		textSelectable.iconTextureID = prefab && prefabIconID.id != 0 ? prefabIconID : actorIconID;

		return targetPtr->GetName();
	});

	m_widgetActorLink[targetPtr] = &textSelectable;

	textSelectable.ClickedEvent += [this, &p_actor] { SelectActorFromClick(p_actor, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift); };
	textSelectable.DoubleClickedEvent += EDITOR_BIND(MoveToTarget, std::ref(p_actor));
}

void OvEditor::Panels::Hierarchy::HandleFileDrop(const std::string& p_path, OvCore::ECS::Actor* p_parent)
{
	using namespace OvTools::Utils;
	using enum PathParser::EFileType;

	auto* currentScene = m_sceneManager.GetCurrentScene();
	if (!currentScene)
		return;

	OvCore::ECS::Actor* validParent = nullptr;
	if (p_parent && p_parent->IsAlive() && p_parent->GetScene() == currentScene)
	{
		validParent = p_parent;
	}

	switch (PathParser::GetFileType(p_path))
	{
	case SCENE:
		EDITOR_EXEC(DelayAction([p_path] { EDITOR_EXEC(OpenScene(p_path)); }, 0));
		break;

	case MODEL:
	{
		auto& actor = EDITOR_EXEC(CreateActorWithModel(p_path, true, validParent));
		EDITOR_EXEC(SelectActor(actor));
		break;
	}

	case PREFAB:
	{
		if (auto* actor = EDITOR_EXEC(InstantiatePrefab(p_path, validParent)))
		{
			EDITOR_EXEC(SelectActor(*actor));
		}
		break;
	}

	case MATERIAL:
	{
		if (validParent)
		{
			if (auto materialRenderer = validParent->GetComponent<OvCore::ECS::Components::CMaterialRenderer>())
			{
				const auto resourcePath = EDITOR_EXEC(GetResourcePath(p_path));
				if (const auto material = EDITOR_CONTEXT(materialManager)[resourcePath])
				{
					EDITOR_EXEC(BeginSceneEdit("Assign material"));
					materialRenderer->SetMaterialAtIndex(0, *material);
				}
			}
		}
		break;
	}

	case SOUND:
	{
		const auto resourcePath = EDITOR_EXEC(GetResourcePath(p_path));
		if (auto* sound = EDITOR_CONTEXT(soundManager)[resourcePath])
		{
			EDITOR_EXEC(BeginSceneEdit("Assign sound"));
			if (!validParent)
			{
				auto& actor = EDITOR_EXEC(CreateEmptyActor(true, nullptr, std::filesystem::path(p_path).stem().string()));
				actor.AddComponent<OvCore::ECS::Components::CAudioSource>().SetSound(sound);
			}
			else
			{
				auto* audio = validParent->GetComponent<OvCore::ECS::Components::CAudioSource>();
				if (!audio) audio = &validParent->AddComponent<OvCore::ECS::Components::CAudioSource>();
				audio->SetSound(sound);
			}
		}
		break;
	}

	default:
		break;
	}
}
