/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <filesystem>
#include <format>
#include <ranges>
#include <string>

#include <OvCore/ECS/Components/CTransform.h>
#include <OvCore/ECS/ComponentRegistry.h>
#include <OvCore/Helpers/GUIDrawer.h>
#include <OvCore/Helpers/GUIHelpers.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Core/EditorResources.h>
#include <OvEditor/Helpers/PickerHelpers.h>
#include <OvEditor/Panels/Inspector.h>
#include <OvEditor/Panels/InspectorBindings.h>
#include <OvCore/Resources/Material.h>
#include <OvEditor/Panels/SceneView.h>
#include <imgui.h>
#include <OvTools/Utils/PathParser.h>
#include <OvUI/Widgets/Buttons/Button.h>
#include <OvUI/Widgets/Layout/Columns.h>
#include <OvUI/Widgets/Layout/Spacing.h>
#include <OvUI/Widgets/Texts/TextDisabled.h>
#include <OvUI/Styling/Style.h>
#include <OvWindowing/Dialogs/MessageBox.h>

using namespace OvCore::ECS;
using namespace OvCore::ECS::Components;
using namespace OvUI::Widgets;

OvEditor::Panels::Inspector::Inspector(
	const std::string& p_title,
	bool p_opened,
	const OvUI::Settings::PanelWindowSettings & p_windowSettings,
    Callbacks callbacks
) : PanelWindow(p_title, p_opened, p_windowSettings), m_callbacks(std::move(callbacks))
{
    if (!m_callbacks.defer) m_callbacks.defer = [](std::function<void()> action) { EDITOR_EXEC(DelayAction(std::move(action))); };
    if (!m_callbacks.selected) m_callbacks.selected = [](Actor& actor) { EDITOR_EVENT(ActorSelectedEvent).Invoke(actor); };
    if (!m_callbacks.unselected) m_callbacks.unselected = [](Actor& actor) { EDITOR_EVENT(ActorUnselectedEvent).Invoke(actor); };
	m_emptyState = &CreateWidget<Layout::Group>();
	m_emptyState->CreateWidget<Texts::Text>("No entity selected");
	m_emptyState->CreateWidget<Texts::TextDisabled>("Select an entity in the Hierarchy or Scene View.");
	m_emptyState->CreateWidget<Texts::TextDisabled>("Double-click a Hierarchy entry to frame it.");
	m_content = &CreateWidget<Layout::Group>();

	m_destroyedListener = Actor::DestroyedEvent += [this](Actor& destroyed) {
        if (std::find(m_targets.begin(), m_targets.end(), &destroyed) != m_targets.end())
        {
            auto remaining = m_targets;
            std::erase(remaining, &destroyed);
            FocusActors(remaining);
        }
    };
}

OvEditor::Panels::Inspector::~Inspector()
{
	Actor::DestroyedEvent -= m_destroyedListener;
	UnFocus();
}

void OvEditor::Panels::Inspector::FocusActor(Actor& target)
{
    FocusActors({&target});
}

void OvEditor::Panels::Inspector::FocusActors(const std::vector<Actor*>& targets)
{
    std::vector<Actor*> unique;
    for (auto* actor : targets)
        if (actor && actor->IsAlive() && std::find(unique.begin(), unique.end(), actor) == unique.end()) unique.push_back(actor);
    if (unique == m_targets) return;
    UnFocus();
    if (unique.empty()) return;
    m_targets = std::move(unique);
    m_targetActor = *m_targets.front();
    for (auto* actor : m_targets)
    {
        m_targetListeners.push_back({actor,
            actor->ComponentAddedEvent += [this](auto&) { _QueueRefresh(); },
            actor->ComponentRemovedEvent += [this](auto&) { _QueueRefresh(); },
            actor->BehaviourAddedEvent += [this](auto&) { _QueueRefresh(); },
            actor->BehaviourRemovedEvent += [this](auto&) { _QueueRefresh(); }});
    }
    _Populate();
    m_callbacks.selected(m_targetActor.value());
}

void OvEditor::Panels::Inspector::_Draw_Impl()
{
    // Capture before any custom property control/button can mutate the scene.
    // Idle frames do not serialize; CommitSceneEdit discards no-op gestures.
    if (ImGui::IsMouseClicked(0) && !EDITOR_PANEL(SceneView, "Scene View").IsTransformEditing()) EDITOR_EXEC(CommitSceneEdit(true));
    if (ImGui::IsMouseDown(0) || ImGui::GetIO().WantTextInput) EDITOR_EXEC(BeginSceneEdit());
    PanelWindow::_Draw_Impl();
}

void OvEditor::Panels::Inspector::UnFocus()
{
    m_emptyState->enabled = true;
    m_focusCallbacks.Invalidate();
    _InvalidateView();
    m_refreshPending = false;
	if (!m_targetActor)
	{
		return;
	}

    for (const auto& listeners : m_targetListeners)
    {
        listeners.actor->ComponentAddedEvent -= listeners.added;
        listeners.actor->ComponentRemovedEvent -= listeners.removed;
        listeners.actor->BehaviourAddedEvent -= listeners.behaviourAdded;
        listeners.actor->BehaviourRemovedEvent -= listeners.behaviourRemoved;
    }
    m_targetListeners.clear();
    m_targets.clear();
    m_batchControls.clear();

	m_content->RemoveAllWidgets();

	m_callbacks.unselected(m_targetActor.value());

	m_targetActor.reset();
}

OvTools::Utils::OptRef<Actor> OvEditor::Panels::Inspector::GetTargetActor() const
{
	return m_targetActor;
}

void OvEditor::Panels::Inspector::_Populate()
{
    m_emptyState->enabled = false;
    m_content->enabled = true;
	OVASSERT(m_targetActor.has_value(), "Cannot populate inspector without a target actor");
    if (m_targets.size() > 1)
    {
        m_content->CreateWidget<Texts::Text>(std::to_string(m_targets.size()) + " entities selected");
        m_content->CreateWidget<Texts::TextDisabled>("Common components only. (Mixed) indicates different values.");
    }
	_PopulateActorInfo();
	m_content->CreateWidget<Visual::Separator>();
	_PopulateActorComponents();
	_PopulateActorBehaviours();
	m_content->CreateWidget<Visual::Separator>();
	_DrawAddSection();
}

void OvEditor::Panels::Inspector::_PopulateActorInfo()
{
    auto ticket = m_viewCallbacks.Ticket();
    auto populate = [ticket](OvUI::Internal::WidgetContainer& columns, Actor& actor)
    {
        OvCore::Helpers::GUIDrawer::DrawString(columns, "Name",
            [&actor, ticket] { return ticket.expired() ? std::string{} : actor.GetName(); },
            [&actor, ticket](const std::string& value) { if (!ticket.expired()) actor.SetName(value); });
        OvCore::Helpers::GUIDrawer::DrawString(columns, "Tag",
            [&actor, ticket] { return ticket.expired() ? std::string{} : actor.GetTag(); },
            [&actor, ticket](const std::string& value) { if (!ticket.expired()) actor.SetTag(value); });
        OvCore::Helpers::GUIDrawer::DrawBoolean(columns, "Active",
            [&actor, ticket] { return !ticket.expired() && actor.IsSelfActive(); },
            [&actor, ticket](bool value) { if (!ticket.expired()) actor.SetActive(value); });
    };
    auto& headerColumns = m_content->CreateWidget<Layout::Columns<2>>();
    headerColumns.SetID("inspector_actor_header");
    headerColumns.widths[0] = 140 * OVUI_SCALE;
    populate(headerColumns, m_targetActor.value());
    std::vector<OvUI::Internal::WidgetContainer*> roots{&headerColumns};
    for (size_t i = 1; i < m_targets.size(); ++i)
    {
        auto controls = std::make_unique<OvUI::Internal::WidgetContainer>();
        populate(*controls, *m_targets[i]);
        roots.push_back(controls.get()); m_batchControls.push_back(std::move(controls));
    }
    InspectorBindings::Connect(roots, ticket);
    if (m_targets.size() > 1) return;

	auto& identity = m_content->CreateWidget<Layout::GroupCollapsable>("Identity & Prefab");
	identity.SetID("inspector_identity");
	identity.defaultExpanded = false;
	auto& identityColumns = identity.CreateWidget<Layout::Columns<2>>();
	identityColumns.SetID("inspector_identity_columns");
	identityColumns.widths[0] = 100 * OVUI_SCALE;
	OvCore::Helpers::GUIDrawer::DrawReadOnlyString(
		identityColumns,
		"GUID",
		[this, ticket] { return ticket.expired() ? std::string{} : std::format("{:016X}", m_targetActor->GetGUID()); }
	);

	auto& prefabSourceField = OvCore::Helpers::GUIDrawer::DrawAsset(
		identityColumns,
		"Prefab Source",
		[this, ticket] { return ticket.expired() ? std::string{} : m_targetActor->GetPrefabSource(); },
		[](const std::string&) {},
		OvTools::Utils::PathParser::EFileType::PREFAB
	);
	prefabSourceField.disabled = true;
}

void OvEditor::Panels::Inspector::_PopulateActorComponents()
{
	auto& components = m_targetActor->GetComponents();
	const int total = static_cast<int>(components.size());
	for (int i = 0; i < total; ++i)
	{
        const auto type = components[i]->GetTypeName();
        const bool common = std::all_of(m_targets.begin(), m_targets.end(), [&type](auto* actor) {
            return std::any_of(actor->GetComponents().begin(), actor->GetComponents().end(), [&type](const auto& c) { return c->GetTypeName() == type; });
        });
        if (common) _DrawComponent(*components[i], i, total);
	}
}

void OvEditor::Panels::Inspector::_PopulateActorBehaviours()
{
	auto& order = m_targetActor->GetBehavioursOrder();
	auto& behaviours = m_targetActor->GetBehaviours();
	const int total = static_cast<int>(order.size());
	for (int i = 0; i < total; ++i)
	{
		auto it = behaviours.find(order[i]);
		if (it != behaviours.end() && std::all_of(m_targets.begin(), m_targets.end(), [&](auto* actor) { return actor->GetBehaviours().contains(order[i]); }))
			_DrawBehaviour(it->second, i, total);
	}
}

void OvEditor::Panels::Inspector::_DrawAddSection()
{
	auto& addButton = m_content->CreateWidget<Buttons::Button>("Add Component...", OvMaths::FVector2{ -1.f, 0 });
	addButton.ClickedEvent += [this, ticket = m_viewCallbacks.Ticket()] {
        if (ticket.expired()) return;
		if (!m_targetActor.has_value())
			return;

		const auto componentIconID = EDITOR_CONTEXT(editorResources)->GetTexture("Component")->RetainTexture();
		const auto addScriptIconID = EDITOR_CONTEXT(editorResources)->GetTexture("Add_Script")->RetainTexture();

		OvCore::Helpers::GUIHelpers::PickerItemList items;

		for (const auto& info : GetComponentRegistry().Components())
		{
            if (!std::all_of(m_targets.begin(), m_targets.end(), [&info](auto* actor) { return info.IsAddable(*actor); })) continue;

			const std::string name = std::string(info.GetName());
			items.Add({
				name,
				name,
				name,
				componentIconID,
				[this, name, &info, ticket] {
                    if (ticket.expired()) return;
					if (!m_targetActor.has_value())
						return;

					if (!info.IsAddable(m_targetActor.value()))
					{
						OvWindowing::Dialogs::MessageBox(
							"Component already attached",
							"The component \"" + name + "\" is already attached to this actor.",
							OvWindowing::Dialogs::MessageBox::EMessageType::ERROR,
							OvWindowing::Dialogs::MessageBox::EButtonLayout::OK
						);
						return;
					}

                    EDITOR_EXEC(BeginSceneEdit("Add component"));
                    for (auto* actor : m_targets) if (info.IsAddable(*actor)) info.AddComponent(*actor);
				}
			});
		}

        for (const auto& identity : EDITOR_CONTEXT(scriptEngine)->CSharpTypes())
        {
            if (std::any_of(m_targets.begin(), m_targets.end(), [&](auto* actor) { return actor->GetBehaviour(identity) != nullptr; })) continue;
            const auto label = "C# / " + identity.type + " [" + identity.assembly + "]";
            items.Add({identity.Key(), label, label, addScriptIconID, [this, ticket, identity] {
                if (ticket.expired() || !m_targetActor.has_value()) return;
                auto& engine = *EDITOR_CONTEXT(scriptEngine);
                if (engine.Status(identity) == OvCore::Scripting::ScriptStatus::TypeReady)
                {
                    EDITOR_EXEC(BeginSceneEdit("Add script"));
                    for (auto* actor : m_targets) if (!actor->GetBehaviour(identity)) actor->AddBehaviour(identity);
                }
            }});
        }
		OvEditor::Helpers::PickerHelpers::AddFileItems(
			items,
			OvTools::Utils::PathParser::EFileType::SCRIPT,
			[this, ticket](const std::string& p_resourcePath) {
                if (ticket.expired()) return;
				if (!m_targetActor.has_value())
					return;

				const std::string scriptPath = EDITOR_EXEC(GetScriptPath(p_resourcePath));
				const std::string displayName = OvTools::Utils::PathParser::GetElementName(scriptPath);

				if (m_targetActor->GetBehaviour(scriptPath))
				{
					OvWindowing::Dialogs::MessageBox(
						"Script already attached",
						"The script \"" + displayName + "\" is already attached to this actor.",
						OvWindowing::Dialogs::MessageBox::EMessageType::ERROR,
						OvWindowing::Dialogs::MessageBox::EButtonLayout::OK
					);
					return;
				}

                EDITOR_EXEC(BeginSceneEdit("Add script"));
                for (auto* actor : m_targets) if (!actor->GetBehaviour(scriptPath)) actor->AddBehaviour(scriptPath);
			},
			true, false,
			[this, ticket](const std::string& p_resourcePath) {
                auto extension = std::filesystem::path(p_resourcePath).extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ticket.expired() || extension == ".cs") return false;
				const std::string scriptPath = EDITOR_EXEC(GetScriptPath(p_resourcePath));
				return std::none_of(m_targets.begin(), m_targets.end(), [&](auto* actor) { return actor->GetBehaviour(scriptPath) != nullptr; });
			}
		);

		items.Add({
			"Create Script...",
			"Create Script...",
			"Create Script...",
			addScriptIconID,
			[this, ticket] {
                if (ticket.expired()) return;
				if (!m_targetActor.has_value())
					return;

				const std::string searchText = OvCore::Helpers::GUIHelpers::GetPickerSearchText();
				const std::string initialDir = EDITOR_CONTEXT(projectAssetsPath).string();
				const std::string destination = EDITOR_EXEC(CreateScript(initialDir, searchText));

				if (ticket.expired() || destination.empty())
					return;

				const std::string scriptPath = EDITOR_EXEC(GetScriptPath(destination));
				const std::string displayName = OvTools::Utils::PathParser::GetElementName(scriptPath);

				if (m_targetActor->GetBehaviour(scriptPath))
				{
					OvWindowing::Dialogs::MessageBox(
						"Script already attached",
						"The script \"" + displayName + "\" is already attached to this actor.",
						OvWindowing::Dialogs::MessageBox::EMessageType::ERROR,
						OvWindowing::Dialogs::MessageBox::EButtonLayout::OK
					);
					return;
				}

                EDITOR_EXEC(BeginSceneEdit("Add script"));
                for (auto* actor : m_targets) if (!actor->GetBehaviour(scriptPath)) actor->AddBehaviour(scriptPath);
			},
			true  /* alwaysVisible */
		});

		OvCore::Helpers::GUIHelpers::OpenPicker(std::move(items), "Add Component");
	};
}

void OvEditor::Panels::Inspector::_DrawComponent(AComponent& p_component, int p_index, int p_total)
{
	auto& header = m_content->CreateWidget<Layout::GroupCollapsable>(p_component.GetName());
	header.SetID("inspector_component_" + p_component.GetName());
	const bool isTransform = dynamic_cast<CTransform*>(&p_component) != nullptr;
	header.closable = !isTransform;
	header.CloseEvent += [this, &p_component, ticket = m_viewCallbacks.Ticket()] {
        if (ticket.expired()) return;
        EDITOR_EXEC(BeginSceneEdit("Remove component"));
        const auto type = p_component.GetTypeName();
        for (auto* actor : m_targets)
            for (const auto& component : actor->GetComponents())
                if (component->GetTypeName() == type) { actor->RemoveComponent(*component); break; }
	};

	if (!isTransform)
	{
		header.reorderable = m_targets.size() == 1;
		header.canMoveUp = (p_index > 1);
		header.canMoveDown = (p_index < p_total - 1);

		auto move = [this, &p_component, ticket = m_viewCallbacks.Ticket()](bool up) {
            if (ticket.expired()) return;
            EDITOR_EXEC(BeginSceneEdit("Reorder component"));
			auto& comps = p_component.owner.GetComponents();
			auto it = std::find_if(comps.begin(), comps.end(), [&](const auto& c) { return c.get() == &p_component; });
			if (it == comps.end()) return;
			if (up) {
				if (it != comps.begin() && !dynamic_cast<CTransform*>(std::prev(it)->get()))
					std::iter_swap(it, std::prev(it));
			} else {
				if (auto next = std::next(it); next != comps.end())
					std::iter_swap(it, next);
			}
			_QueueRefresh();
		};
		header.MoveUpEvent += [move] { move(true); };
		header.MoveDownEvent += [move] { move(false); };
	}

	auto& columns = header.CreateWidget<Layout::Columns<2>>();
	columns.SetID("comp_" + p_component.GetName());
	columns.widths[0] = 140 * OVUI_SCALE;
	p_component.OnInspector(columns);
    std::vector<OvUI::Internal::WidgetContainer*> roots{&columns};
    for (size_t i = 1; i < m_targets.size(); ++i)
        for (const auto& component : m_targets[i]->GetComponents())
            if (component->GetTypeName() == p_component.GetTypeName())
            {
                auto controls = std::make_unique<OvUI::Internal::WidgetContainer>();
                component->OnInspector(*controls); roots.push_back(controls.get());
                m_batchControls.push_back(std::move(controls)); break;
            }
    InspectorBindings::Connect(roots, m_viewCallbacks.Ticket());
}

void OvEditor::Panels::Inspector::_DrawBehaviour(Behaviour& p_behaviour, int p_index, int p_total)
{
	const auto identity = p_behaviour.CurrentIdentity();
	const auto simpleName = identity.type.substr(identity.type.find_last_of(".+") + 1);
	auto& header = m_content->CreateWidget<Layout::GroupCollapsable>(p_behaviour.Identity().backend == "lua" ?
		std::filesystem::path(p_behaviour.name).replace_extension().string() : simpleName + " (Script)");
	header.SetID("inspector_behaviour_" + identity.Key());
	if (identity.backend == "csharp") header.tooltip = identity.type + "\n" + identity.assembly;
	header.closable = true;
	header.CloseEvent += [this, &p_behaviour, ticket = m_viewCallbacks.Ticket()] {
        if (ticket.expired()) return;
        EDITOR_EXEC(BeginSceneEdit("Remove script"));
        const auto identity = p_behaviour.Identity();
        for (auto* actor : m_targets)
            if (auto* behaviour = actor->GetBehaviour(identity)) actor->RemoveBehaviour(*behaviour);
	};

	header.reorderable = m_targets.size() == 1;
	header.canMoveUp = (p_index > 0);
	header.canMoveDown = (p_index < p_total - 1);

	auto move = [this, &p_behaviour, ticket = m_viewCallbacks.Ticket()](bool up) {
        if (ticket.expired()) return;
        EDITOR_EXEC(BeginSceneEdit("Reorder script"));
		auto& order = p_behaviour.owner.GetBehavioursOrder();
		auto it = std::find(order.begin(), order.end(), p_behaviour.Identity().Key());
        if (it == order.end()) return;
		if (up) {
			if (it != order.begin())
				std::iter_swap(it, std::prev(it));
		} else {
			if (auto next = std::next(it); next != order.end())
				std::iter_swap(it, next);
		}
		_QueueRefresh();
	};
	header.MoveUpEvent += [move] { move(true); };
	header.MoveDownEvent += [move] { move(false); };

	const auto editIconID = EDITOR_CONTEXT(editorResources)->GetTexture("Edit")->RetainTexture();
	const auto scriptPath = EDITOR_CONTEXT(projectAssetsPath) / p_behaviour.name;
	header.actions.push_back({editIconID, [scriptPath] {
		EDITOR_EXEC(OpenInCodeEditor(scriptPath));
	}});

	auto& columns = header.CreateWidget<Layout::Columns<2>>();
	columns.SetID("bhv_" + identity.Key());
	columns.widths[0] = 140 * OVUI_SCALE;
	p_behaviour.OnInspector(columns);
    std::vector<OvUI::Internal::WidgetContainer*> roots{&columns};
    for (size_t i = 1; i < m_targets.size(); ++i)
    {
        auto controls = std::make_unique<OvUI::Internal::WidgetContainer>();
        m_targets[i]->GetBehaviour(p_behaviour.Identity())->OnInspector(*controls);
        roots.push_back(controls.get()); m_batchControls.push_back(std::move(controls));
    }
    InspectorBindings::Connect(roots, m_viewCallbacks.Ticket());
}

void OvEditor::Panels::Inspector::Refresh()
{
    _InvalidateView();
    m_refreshPending = false;
	if (m_targetActor)
	{
		m_content->RemoveAllWidgets();
        m_batchControls.clear();
		_Populate();
	}
}

void OvEditor::Panels::Inspector::_InvalidateView()
{
    m_viewCallbacks.Invalidate();
    // Removal events happen before destruction. Stop the remaining controls in
    // the current draw traversal until the deferred rebuild has replaced them.
    auto disable = [](OvUI::Internal::WidgetContainer& root, auto&& self) -> void
    {
        for (auto [widget, mode] : root.GetWidgets())
        {
            widget->enabled = false;
            if (auto* children = dynamic_cast<OvUI::Internal::WidgetContainer*>(widget)) self(*children, self);
        }
    };
    disable(*m_content, disable);
}

void OvEditor::Panels::Inspector::_QueueRefresh()
{
    _InvalidateView();
    if (m_refreshPending) return;
    m_refreshPending = true;
    auto ticket = m_focusCallbacks.Ticket();
    m_callbacks.defer([this, ticket] { if (!ticket.expired()) Refresh(); });
}
