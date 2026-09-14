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
#include <OvTools/Utils/PathParser.h>
#include <OvUI/Widgets/Buttons/Button.h>
#include <OvUI/Widgets/Layout/Columns.h>
#include <OvUI/Widgets/Layout/Spacing.h>
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
	m_content = &CreateWidget<Layout::Group>();

	m_destroyedListener = Actor::DestroyedEvent += [this](Actor& p_destroyed) {
		if (m_targetActor.has_value() &&  &m_targetActor.value() == &p_destroyed)
		{
			UnFocus();
		}
	};
}

OvEditor::Panels::Inspector::~Inspector()
{
	Actor::DestroyedEvent -= m_destroyedListener;
	UnFocus();
}

void OvEditor::Panels::Inspector::FocusActor(Actor& p_target)
{
	if (m_targetActor)
	{
		UnFocus();
	}

	m_targetActor = p_target;

	m_componentAddedListener = m_targetActor->ComponentAddedEvent += [this] (auto&) { _QueueRefresh(); };
	m_behaviourAddedListener = m_targetActor->BehaviourAddedEvent += [this](auto&) { _QueueRefresh(); };
	m_componentRemovedListener = m_targetActor->ComponentRemovedEvent += [this](auto&) { _QueueRefresh(); };
	m_behaviourRemovedListener = m_targetActor->BehaviourRemovedEvent += [this](auto&) { _QueueRefresh(); };

	_Populate();

	m_callbacks.selected(m_targetActor.value());
}

void OvEditor::Panels::Inspector::UnFocus()
{
    m_focusCallbacks.Invalidate();
    _InvalidateView();
    m_refreshPending = false;
	if (!m_targetActor)
	{
		return;
	}

	m_targetActor->ComponentAddedEvent -= m_componentAddedListener;
	m_targetActor->ComponentRemovedEvent -= m_componentRemovedListener;
	m_targetActor->BehaviourAddedEvent -= m_behaviourAddedListener;
	m_targetActor->BehaviourRemovedEvent -= m_behaviourRemovedListener;

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
    m_content->enabled = true;
	OVASSERT(m_targetActor.has_value(), "Cannot populate inspector without a target actor");
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
	auto& headerColumns = m_content->CreateWidget<Layout::Columns<2>>();

	OvCore::Helpers::GUIDrawer::DrawString(
		headerColumns,
		"Name",
		[this, ticket] { return ticket.expired() ? std::string{} : m_targetActor->GetName(); },
		[this, ticket](const std::string& p_newName) { if (!ticket.expired()) m_targetActor->SetName(p_newName); }
	);

	OvCore::Helpers::GUIDrawer::DrawString(
		headerColumns,
		"Tag",
		[this, ticket] { return ticket.expired() ? std::string{} : m_targetActor->GetTag(); },
		[this, ticket](const std::string& p_newName) { if (!ticket.expired()) m_targetActor->SetTag(p_newName); }
	);

	OvCore::Helpers::GUIDrawer::DrawBoolean(
		headerColumns,
		"Active",
		[this, ticket]{ return !ticket.expired() && m_targetActor->IsSelfActive(); },
		[this, ticket](bool p_active) { if (!ticket.expired()) m_targetActor->SetActive(p_active); }
	);

	OvCore::Helpers::GUIDrawer::DrawReadOnlyString(
		headerColumns,
		"GUID",
		[this, ticket] { return ticket.expired() ? std::string{} : std::format("{:016X}", m_targetActor->GetGUID()); }
	);

	auto& prefabSourceField = OvCore::Helpers::GUIDrawer::DrawAsset(
		headerColumns,
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
		_DrawComponent(*components[i], i, total);
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
		if (it != behaviours.end())
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
			if (!info.IsAddable(m_targetActor.value()))
				continue;

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

					info.AddComponent(m_targetActor.value());
				}
			});
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

				m_targetActor->AddBehaviour(scriptPath);
			},
			true, false,
			[this, ticket](const std::string& p_resourcePath) {
                if (ticket.expired()) return false;
				const std::string scriptPath = EDITOR_EXEC(GetScriptPath(p_resourcePath));
				return !m_targetActor->GetBehaviour(scriptPath);
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

				m_targetActor->AddBehaviour(scriptPath);
			},
			true  /* alwaysVisible */
		});

		OvCore::Helpers::GUIHelpers::OpenPicker(std::move(items), "Add Component");
	};
}

void OvEditor::Panels::Inspector::_DrawComponent(AComponent& p_component, int p_index, int p_total)
{
	auto& header = m_content->CreateWidget<Layout::GroupCollapsable>(p_component.GetName());
	const bool isTransform = dynamic_cast<CTransform*>(&p_component) != nullptr;
	header.closable = !isTransform;
	header.CloseEvent += [this, &p_component, ticket = m_viewCallbacks.Ticket()] {
        if (ticket.expired()) return;
		p_component.owner.RemoveComponent(p_component);
	};

	if (!isTransform)
	{
		header.reorderable = true;
		header.canMoveUp = (p_index > 1);
		header.canMoveDown = (p_index < p_total - 1);

		auto move = [this, &p_component, ticket = m_viewCallbacks.Ticket()](bool up) {
            if (ticket.expired()) return;
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
	columns.widths[0] = 200 * OVUI_SCALE;
	p_component.OnInspector(columns);
}

void OvEditor::Panels::Inspector::_DrawBehaviour(Behaviour& p_behaviour, int p_index, int p_total)
{
	auto& header = m_content->CreateWidget<Layout::GroupCollapsable>(p_behaviour.Identity().backend == "lua" ?
		std::filesystem::path(p_behaviour.name).replace_extension().string() : p_behaviour.CurrentIdentity().type);
	header.closable = true;
	header.CloseEvent += [&p_behaviour, ticket = m_viewCallbacks.Ticket()] {
        if (ticket.expired()) return;
		p_behaviour.owner.RemoveBehaviour(p_behaviour);
	};

	header.reorderable = true;
	header.canMoveUp = (p_index > 0);
	header.canMoveDown = (p_index < p_total - 1);

	auto move = [this, &p_behaviour, ticket = m_viewCallbacks.Ticket()](bool up) {
        if (ticket.expired()) return;
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
	columns.SetID("bhv_" + p_behaviour.name);
	columns.widths[0] = 200 * OVUI_SCALE;
	p_behaviour.OnInspector(columns);
}

void OvEditor::Panels::Inspector::Refresh()
{
    _InvalidateView();
    m_refreshPending = false;
	if (m_targetActor)
	{
		m_content->RemoveAllWidgets();
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
