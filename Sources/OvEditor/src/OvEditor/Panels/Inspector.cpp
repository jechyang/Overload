/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <filesystem>
#include <format>
#include <ranges>
#include <string>
#include <cctype>
#include <sstream>
#include <tinyxml2.h>

#include <OvCore/ECS/Components/CTransform.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/ECS/ComponentRegistry.h>
#include <OvCore/Helpers/GUIDrawer.h>
#include <OvCore/Helpers/ManagedFieldInput.h>
#include <OvCore/Helpers/GUIHelpers.h>
#include <OvCore/ResourceManagement/MaterialManager.h>
#include <OvCore/ResourceManagement/ModelManager.h>
#include <OvCore/ResourceManagement/TextureManager.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Core/EditorResources.h>
#include <OvEditor/Helpers/PickerHelpers.h>
#include <OvEditor/Panels/AssetView.h>
#include <OvEditor/Panels/Inspector.h>
#include <OvEditor/Panels/InspectorBindings.h>
#include <OvEditor/Panels/MaterialEditor.h>
#include <OvEditor/Panels/ProjectSettings.h>
#include <OvCore/Resources/Material.h>
#include <OvEditor/Panels/SceneView.h>
#include <OvRendering/Resources/Parsers/EmbeddedAssetPath.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <OvTools/Utils/PathParser.h>
#include <OvTools/Utils/SizeConverter.h>
#include <OvUI/Internal/TextureID.h>
#include <OvUI/Widgets/Buttons/Button.h>
#include <OvUI/Widgets/Layout/Columns.h>
#include <OvUI/Widgets/Layout/Spacing.h>
#include <OvUI/Widgets/Selection/ComboBox.h>
#include <OvUI/Widgets/Texts/TextDisabled.h>
#include <OvUI/Styling/Style.h>
#include <OvWindowing/Dialogs/MessageBox.h>

using namespace OvCore::ECS;
using namespace OvCore::ECS::Components;
using namespace OvUI::Widgets;

namespace
{
    class InspectorToolbar final : public AWidget
    {
    public:
        explicit InspectorToolbar(std::function<void()> draw) : m_draw(std::move(draw)) {}
    private:
        void _Draw_Impl() override { m_draw(); }
        std::function<void()> m_draw;
    };

    class TagSelector final : public DataWidget<std::string>
    {
    public:
        explicit TagSelector(Texts::TextColored* label = nullptr)
            : DataWidget<std::string>(content), m_label(label) {}

        std::string content;

    private:
        void _Draw_Impl() override
        {
            const bool mixed = m_label && m_label->content.ends_with(" (Mixed)");
            const std::string preview = mixed ? "-- (Mixed)" : (content.empty() ? "Untagged" : content);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo(("##Tag" + m_widgetID).c_str(), preview.c_str(), ImGuiComboFlags_HeightLarge))
            {
                const auto comboItem = GImGui->LastItemData;
                const bool untaggedSelected = !mixed && content.empty();
                if (ImGui::Selectable("Untagged", untaggedSelected))
                {
                    content.clear();
                    NotifyChange();
                }
                if (untaggedSelected)
                    ImGui::SetItemDefaultFocus();

                std::vector<std::string> tags = OvEditor::Core::Context::GetBuiltInTags();
                if (auto* actions = OvCore::Global::ServiceLocator::TryGet<OvEditor::Core::EditorActions>())
                    tags = actions->GetContext().GetAvailableTags();
                if (!mixed && !content.empty() && std::find(tags.begin(), tags.end(), content) == tags.end())
                    tags.push_back(content);

                for (const auto& tag : tags)
                {
                    const bool selected = !mixed && content == tag;
                    if (ImGui::Selectable(tag.c_str(), selected))
                    {
                        content = tag;
                        NotifyChange();
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::Separator();
                if (ImGui::Selectable("Add Tag...", false))
                {
                    if (auto* actions = OvCore::Global::ServiceLocator::TryGet<OvEditor::Core::EditorActions>())
                        actions->GetPanelsManager().GetPanelAs<OvEditor::Panels::ProjectSettings>("Project Settings").FocusTags();
                }
                ImGui::EndCombo();
                GImGui->LastItemData = comboItem;
            }
        }

        Texts::TextColored* m_label = nullptr;
    };
    std::string Lower(std::string value)
    {
        for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    }
    void SetVisible(AWidget& widget, bool visible, bool expand = false)
    {
        widget.visible = visible;
        if (auto* group = dynamic_cast<Layout::GroupCollapsable*>(&widget)) group->expandForSearch = expand && visible;
        if (auto* collection=dynamic_cast<OvCore::Helpers::ReflectionProperties::CollectionField*>(&widget)) collection->expandForSearch=expand && visible;
        if (auto* children = dynamic_cast<OvUI::Internal::WidgetContainer*>(&widget))
            for (auto [child, mode] : children->GetWidgets()) SetVisible(*child,visible,expand);
    }
    bool FilterProperties(OvUI::Internal::WidgetContainer& root, const std::string& context,
        const std::vector<std::string>& terms)
    {
        const auto matches = [&](const std::string& text) {
            const auto lower = Lower(text);
            return std::all_of(terms.begin(),terms.end(),[&](const auto& term) { return lower.find(term) != std::string::npos; });
        };
        if (matches(context))
        {
            for (auto [widget, mode] : root.GetWidgets()) SetVisible(*widget,true,!terms.empty());
            return true;
        }
        bool any = false;
        std::vector<AWidget*> block;
        std::string title = context;
        const auto flush = [&] {
            if (block.empty()) return;
            bool hit = matches(title);
            for (auto* widget : block)
            {
                if (auto* children = dynamic_cast<OvUI::Internal::WidgetContainer*>(widget))
                {
                    auto* group = dynamic_cast<Layout::GroupCollapsable*>(widget);
                    auto* collection=dynamic_cast<OvCore::Helpers::ReflectionProperties::CollectionField*>(widget);
                    widget->visible = FilterProperties(*children,title+(group ? " "+group->name : collection ? " "+collection->Name() : ""),terms);
                    if (group) group->expandForSearch = !terms.empty() && widget->visible;
                    if (collection) collection->expandForSearch = !terms.empty() && widget->visible;
                    hit |= widget->visible;
                }
                else widget->visible = hit;
            }
            for (auto* widget : block)
                if (!dynamic_cast<OvUI::Internal::WidgetContainer*>(widget)) widget->visible = hit;
            any |= hit; block.clear();
        };
        for (auto [widget, mode] : root.GetWidgets())
        {
            if (auto* label = dynamic_cast<Texts::TextColored*>(widget))
            { flush(); title = context+" "+label->content; }
            if (auto* managed=dynamic_cast<OvCore::Helpers::ManagedFieldInput*>(widget); managed && managed->composite)
            { flush(); title=context+" "+managed->displayName; }
            if (dynamic_cast<Visual::Separator*>(widget))
            { flush(); widget->visible = false; title = context; continue; }
            block.push_back(widget);
        }
        flush(); return any;
    }
    AComponent* FindComponent(Actor& actor, const std::string& type)
    {
        auto normalize = [](std::string_view s) -> std::string_view {
            if (s.starts_with("class ")) return s.substr(6);
            if (s.starts_with("struct ")) return s.substr(7);
            return s;
        };
        const auto cleanType = normalize(type);
        for (auto& component : actor.GetComponents())
        {
            if (component->GetTypeName() == type || normalize(component->GetTypeName()) == cleanType)
                return component.get();
        }
        return nullptr;
    }
    Actor* FindPrefabInstanceRoot(Actor* actor)
    {
        while (actor && !actor->HasPrefabSource())
            actor = actor->GetParent();
        return actor;
    }
    std::string ComponentXML(AComponent& component)
    {
        tinyxml2::XMLDocument doc; auto* data = doc.NewElement("data"); doc.InsertEndChild(data);
        if (!component.IsSelfEnabled()) data->SetAttribute("enabled", false);
        component.OnSerialize(doc,data); tinyxml2::XMLPrinter printer(nullptr,true); doc.Print(&printer);
        return printer.CStr();
    }
}

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
    CreateWidget<InspectorToolbar>([this] { _DrawToolbar(); });
    m_searchStatus = &CreateWidget<Texts::TextDisabled>(); m_searchStatus->enabled = false;
	m_emptyState = &CreateWidget<Layout::Group>();
	m_emptyState->CreateWidget<Texts::Text>("No entity selected");
	m_emptyState->CreateWidget<Texts::TextDisabled>("Select an entity in the Hierarchy or Scene View.");
	m_emptyState->CreateWidget<Texts::TextDisabled>("Double-click a Hierarchy entry to frame it.");
	m_content = &CreateWidget<Layout::Group>();
    if (auto* actions = OvCore::Global::ServiceLocator::TryGet<OvEditor::Core::EditorActions>())
    {
        m_sceneManager = &actions->GetContext().sceneManager;
        m_sceneUnloadListener = m_sceneManager->SceneUnloadEvent += [this] { m_locked = false; _ClearTargets(); UnFocus(); };
    }

	m_destroyedListener = Actor::DestroyedEvent += [this](Actor& destroyed) {
        if (std::find(m_selection.begin(),m_selection.end(),&destroyed) != m_selection.end())
        {
            auto remaining = m_selection;
            std::erase(remaining, &destroyed);
            const bool previousNav = m_navigatingSelectionHistory;
            m_navigatingSelectionHistory = true;
            FocusActors(remaining);
            m_navigatingSelectionHistory = previousNav;
        }
        if (std::find(m_targets.begin(),m_targets.end(),&destroyed) != m_targets.end())
        {
            auto remaining = m_targets; std::erase(remaining,&destroyed);
            if (remaining.empty()) { m_locked = false; _SetTargets(m_selection); }
            else _SetTargets(remaining);
        }
    };
}

OvEditor::Panels::Inspector::~Inspector()
{
	Actor::DestroyedEvent -= m_destroyedListener;
    if (m_sceneManager) m_sceneManager->SceneUnloadEvent -= m_sceneUnloadListener;
	m_locked = false;
	UnFocus();
    _ClearTargets();
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
    const bool hadAsset = !m_selectedAsset.empty() || (!m_locked && !m_targetAsset.empty());
    if (!unique.empty()) m_selectedAsset.clear();
    if (unique == m_selection && !hadAsset) return;
    if (unique != m_selection)
    {
        _RecordSelectionHistory(unique);
        auto* previous = m_selection.empty() ? nullptr : m_selection.front();
        m_selection = unique;
        if (previous) m_callbacks.unselected(*previous);
        if (!m_locked) _SetTargets(unique);
        if (!m_selection.empty()) m_callbacks.selected(*m_selection.front());
    }
    else if (!m_locked && !unique.empty())
    {
        _SetTargets(unique);
    }
}

void OvEditor::Panels::Inspector::FocusAsset(const std::string& p_path)
{
    if (p_path.empty())
    {
        m_selectedAsset.clear();
        if (!m_locked && !m_targetAsset.empty())
        {
            _ClearTargets();
            if (!m_selection.empty()) _SetTargets(m_selection);
        }
        return;
    }

    std::string resourcePath = p_path;
    if (auto* actions = OvCore::Global::ServiceLocator::TryGet<OvEditor::Core::EditorActions>())
    {
        if (!p_path.starts_with(":") && !OvRendering::Resources::Parsers::ParseEmbeddedAssetPath(p_path).has_value() &&
            std::filesystem::path(p_path).is_absolute())
        {
            const auto norm = std::filesystem::path(p_path).lexically_normal().string();
            const auto eng = std::filesystem::path(actions->GetContext().engineAssetsPath).lexically_normal().string();
            resourcePath = actions->GetResourcePath(norm, norm.starts_with(eng));
        }
        else if (!p_path.starts_with(":") && !OvRendering::Resources::Parsers::ParseEmbeddedAssetPath(p_path).has_value())
        {
            resourcePath = actions->GetResourcePath(p_path, false);
        }
    }

    m_selectedAsset = resourcePath;
    if (!m_locked)
        _SetAssetTarget(resourcePath);
}

void OvEditor::Panels::Inspector::_SetTargets(const std::vector<Actor*>& targets)
{
    if (targets == m_targets && m_targetAsset.empty()) return;
    _ClearTargets();
    if (targets.empty()) return;
    m_targets = targets;
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
}

void OvEditor::Panels::Inspector::_SetAssetTarget(const std::string& p_resourcePath)
{
    if (p_resourcePath == m_targetAsset && m_targets.empty()) return;
    _ClearTargets();
    if (p_resourcePath.empty()) return;
    m_targetAsset = p_resourcePath;
    _PopulateAsset();
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
    m_selectedAsset.clear();
    FocusActors({});
    if (!m_locked && !m_targetAsset.empty())
        _ClearTargets();
}

void OvEditor::Panels::Inspector::_ClearTargets()
{
    m_emptyState->enabled = true;
    m_focusCallbacks.Invalidate();
    _InvalidateView();
    m_refreshPending = false;
	if (!m_targetActor && m_targetAsset.empty())
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
    m_sections.clear();

	m_content->RemoveAllWidgets();

	m_targetActor.reset();
    m_targetAsset.clear();
    m_assetMetadata.reset();
    m_searchStatus->enabled = false;
}

OvTools::Utils::OptRef<Actor> OvEditor::Panels::Inspector::GetTargetActor() const
{
	return m_targetActor;
}

void OvEditor::Panels::Inspector::SetLocked(bool locked)
{
    if (locked == m_locked || (locked && m_targets.empty() && m_targetAsset.empty())) return;
    m_locked = locked;
    if (!locked)
    {
        if (!m_selectedAsset.empty()) _SetAssetTarget(m_selectedAsset);
        else _SetTargets(m_selection);
    }
}

std::vector<uint64_t> OvEditor::Panels::Inspector::GetLockedTargetGUIDs() const
{
    std::vector<uint64_t> result;
    m_restorableLockedAsset = (m_locked && !m_targetAsset.empty()) ? m_targetAsset : std::string{};
    if (m_locked) for (auto* actor : m_targets) if (actor->IsAlive()) result.push_back(actor->GetGUID());
    return result;
}

void OvEditor::Panels::Inspector::RestoreLockedTargets(const std::vector<uint64_t>& guids)
{
    if (guids.empty() && !m_restorableLockedAsset.empty())
    {
        const auto asset = std::move(m_restorableLockedAsset);
        m_restorableLockedAsset.clear();
        _SetAssetTarget(asset);
        m_locked = true;
        return;
    }
    m_restorableLockedAsset.clear();
    std::vector<Actor*> targets;
    if (m_sceneManager && m_sceneManager->GetCurrentScene())
        for (auto guid : guids) if (auto* actor = m_sceneManager->GetCurrentScene()->FindActorByGUID(guid)) targets.push_back(actor);
    if (targets.empty()) { SetLocked(false); return; }
    _SetTargets(targets); m_locked = true;
}

std::vector<Actor*> OvEditor::Panels::Inspector::_ResolveHistoryEntry(const std::vector<uint64_t>& guids) const
{
    std::vector<Actor*> resolved;
    if (m_sceneManager && m_sceneManager->GetCurrentScene())
    {
        auto* scene = m_sceneManager->GetCurrentScene();
        for (auto guid : guids)
            if (auto* actor = scene->FindActorByGUID(guid); actor && actor->IsAlive() &&
                std::find(resolved.begin(), resolved.end(), actor) == resolved.end())
                resolved.push_back(actor);
    }
    return resolved;
}

void OvEditor::Panels::Inspector::_RecordSelectionHistory(const std::vector<Actor*>& selection)
{
    if (m_navigatingSelectionHistory || selection.empty()) return;
    std::vector<uint64_t> guids;
    for (auto* actor : selection) if (actor && actor->IsAlive()) guids.push_back(actor->GetGUID());
    if (guids.empty()) return;
    if (!m_selectionHistory.empty() && m_selectionHistoryIndex < m_selectionHistory.size() &&
        m_selectionHistory[m_selectionHistoryIndex] == guids)
        return;
    if (!m_selectionHistory.empty() && m_selectionHistoryIndex + 1 < m_selectionHistory.size())
        m_selectionHistory.erase(m_selectionHistory.begin() + static_cast<std::ptrdiff_t>(m_selectionHistoryIndex + 1), m_selectionHistory.end());
    if (m_selectionHistory.empty() || m_selectionHistory.back() != guids)
    {
        m_selectionHistory.push_back(std::move(guids));
        if (m_selectionHistory.size() > 64)
            m_selectionHistory.erase(m_selectionHistory.begin());
    }
    m_selectionHistoryIndex = m_selectionHistory.size() - 1;
}

bool OvEditor::Panels::Inspector::CanSelectBack() const
{
    if (m_selectionHistory.empty() || m_selectionHistoryIndex >= m_selectionHistory.size()) return false;
    const int start = m_selection.empty() ? static_cast<int>(m_selectionHistoryIndex) : static_cast<int>(m_selectionHistoryIndex) - 1;
    for (int i = start; i >= 0; --i)
    {
        const auto resolved = _ResolveHistoryEntry(m_selectionHistory[static_cast<size_t>(i)]);
        if (!resolved.empty() && resolved != m_selection) return true;
    }
    return false;
}

bool OvEditor::Panels::Inspector::CanSelectForward() const
{
    if (m_selectionHistory.empty() || m_selectionHistoryIndex >= m_selectionHistory.size()) return false;
    for (size_t i = m_selectionHistoryIndex + 1; i < m_selectionHistory.size(); ++i)
    {
        const auto resolved = _ResolveHistoryEntry(m_selectionHistory[i]);
        if (!resolved.empty() && resolved != m_selection) return true;
    }
    return false;
}

bool OvEditor::Panels::Inspector::SelectBack()
{
    if (m_selectionHistory.empty() || m_selectionHistoryIndex >= m_selectionHistory.size()) return false;
    const int start = m_selection.empty() ? static_cast<int>(m_selectionHistoryIndex) : static_cast<int>(m_selectionHistoryIndex) - 1;
    for (int i = start; i >= 0; --i)
    {
        const auto resolved = _ResolveHistoryEntry(m_selectionHistory[static_cast<size_t>(i)]);
        if (!resolved.empty() && resolved != m_selection)
        {
            m_selectionHistoryIndex = static_cast<size_t>(i);
            m_navigatingSelectionHistory = true;
            FocusActors(resolved);
            m_navigatingSelectionHistory = false;
            return true;
        }
    }
    return false;
}

bool OvEditor::Panels::Inspector::SelectForward()
{
    if (m_selectionHistory.empty() || m_selectionHistoryIndex >= m_selectionHistory.size()) return false;
    for (size_t i = m_selectionHistoryIndex + 1; i < m_selectionHistory.size(); ++i)
    {
        const auto resolved = _ResolveHistoryEntry(m_selectionHistory[i]);
        if (!resolved.empty() && resolved != m_selection)
        {
            m_selectionHistoryIndex = i;
            m_navigatingSelectionHistory = true;
            FocusActors(resolved);
            m_navigatingSelectionHistory = false;
            return true;
        }
    }
    return false;
}

void OvEditor::Panels::Inspector::ClearSelectionHistory()
{
    m_selectionHistory.clear();
    m_selectionHistoryIndex = 0;
}

bool OvEditor::Panels::Inspector::CanEditPrefabInstance() const
{
    return m_targets.size() == 1 && m_targetActor.has_value() && m_targetActor->IsAlive() &&
        FindPrefabInstanceRoot(&m_targetActor.value()) != nullptr;
}

bool OvEditor::Panels::Inspector::SelectPrefabAsset()
{
    if (!CanEditPrefabInstance()) return false;
    auto* root = FindPrefabInstanceRoot(&m_targetActor.value());
    if (!root || root->GetPrefabSource().empty()) return false;
    FocusAsset(root->GetPrefabSource());
    return true;
}

bool OvEditor::Panels::Inspector::RevertPrefabInstance()
{
    if (!CanEditPrefabInstance()) return false;
    auto* actions = OvCore::Global::ServiceLocator::TryGet<OvEditor::Core::EditorActions>();
    if (!actions) return false;
    actions->CommitSceneEdit(true);
    if (!actions->RevertActorToPrefab(m_targetActor.value())) return false;
    if (m_sceneManager && m_sceneManager->GetCurrentScene())
        m_sceneManager->GetCurrentScene()->CollectGarbages();
    Refresh();
    actions->CommitSceneEdit(true);
    return true;
}

bool OvEditor::Panels::Inspector::ApplyPrefabInstance()
{
    if (!CanEditPrefabInstance()) return false;
    auto* actions = OvCore::Global::ServiceLocator::TryGet<OvEditor::Core::EditorActions>();
    if (!actions) return false;
    actions->CommitSceneEdit(true);
    if (!actions->ApplyActorToPrefab(m_targetActor.value())) return false;
    Refresh();
    return true;
}

void OvEditor::Panels::Inspector::_DrawToolbar()
{
    const float scale = OVUI_SCALE;
    ImGui::PushID(this); ImGui::PushID("InspectorToolbar");
    ImGui::BeginDisabled(!CanSelectBack());
    if (ImGui::Button("<##SelectBack",{22*scale,0})) SelectBack();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Previous selection");
    ImGui::EndDisabled(); ImGui::SameLine(0,2*scale);
    ImGui::BeginDisabled(!CanSelectForward());
    if (ImGui::Button(">##SelectForward",{22*scale,0})) SelectForward();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Next selection");
    ImGui::EndDisabled(); ImGui::SameLine(0,6*scale);
    ImGui::BeginDisabled(m_targets.empty() && m_targetAsset.empty());
    if (ImGui::Button(m_locked ? "Locked" : "Lock",{64*scale,0})) SetLocked(!m_locked);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Keep inspecting these entities or assets while selecting others");
    ImGui::EndDisabled(); ImGui::SameLine(0,6*scale);
    char search[256]{}; std::copy_n(m_searchFilter.data(),std::min(m_searchFilter.size(),sizeof(search)-1),search);
    ImGui::SetNextItemWidth(std::max(70*scale,ImGui::GetContentRegionAvail().x-28*scale));
    if (ImGui::InputTextWithHint("##Search","Search components / properties",search,sizeof(search))) SetSearchFilter(search);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Search names, or combine words such as camera far");
    ImGui::SameLine(0,4*scale);
    if (ImGui::Button("x##ClearSearch",{24*scale,0})) SetSearchFilter("");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear Inspector search");
    if (m_locked)
    {
        if (m_targetActor)
        {
            ImGui::TextDisabled("Inspecting: %s",m_targetActor->GetName().c_str());
            if (m_targets.size() > 1) { ImGui::SameLine(); ImGui::TextDisabled("(+%zu)",m_targets.size()-1); }
        }
        else if (!m_targetAsset.empty())
        {
            ImGui::TextDisabled("Inspecting: %s",OvTools::Utils::PathParser::GetElementName(m_targetAsset).c_str());
        }
    }
    ImGui::Separator(); ImGui::PopID(); ImGui::PopID();
}

void OvEditor::Panels::Inspector::SetSearchFilter(const std::string& filter)
{
    if (filter == m_searchFilter) return;
    m_searchFilter = filter; _ApplySearchFilter();
}

void OvEditor::Panels::Inspector::_ApplySearchFilter()
{
    std::vector<std::string> terms;
    std::istringstream input(Lower(m_searchFilter)); std::string word;
    while (input >> word) terms.push_back(word);
    size_t matches = 0;
    for (auto* section : m_sections)
    {
        section->visible = FilterProperties(*section,section->name,terms);
        section->expandForSearch = !terms.empty() && section->visible;
        matches += section->visible ? 1 : 0;
    }
    m_searchStatus->enabled = !terms.empty() && (!m_targets.empty() || !m_targetAsset.empty());
    m_searchStatus->content = matches ? std::to_string(matches)+(matches == 1 ? " matching section" : " matching sections") : "No matching components or properties";
}

bool OvEditor::Panels::Inspector::CopyComponentValues(const std::string& type)
{
    if (!m_targetActor || !m_targetActor->IsAlive()) return false;
    auto* component = FindComponent(m_targetActor.value(),type);
    if (!component) return false;
    m_clipboardXML = ComponentXML(*component); m_clipboardType = type;
    return true;
}

bool OvEditor::Panels::Inspector::CanPasteComponentValues(const std::string& type) const
{
    return !m_clipboardXML.empty() && type == m_clipboardType && !m_targets.empty() &&
        std::all_of(m_targets.begin(),m_targets.end(),[&](auto* actor) { return actor->IsAlive() && FindComponent(*actor,type); });
}

bool OvEditor::Panels::Inspector::_ApplyComponentValues(const std::string& type, const std::string& xml, const std::string& label)
{
    if (m_targets.empty()) return false;
    std::vector<AComponent*> targets;
    for (auto* actor : m_targets)
    {
        if (!actor->IsAlive()) return false;
        auto* component = FindComponent(*actor,type); if (!component) return false;
        targets.push_back(component);
    }
    tinyxml2::XMLDocument doc;
    auto* data = doc.Parse(xml.c_str()) == tinyxml2::XML_SUCCESS ? doc.FirstChildElement("data") : nullptr;
    if (!data) return false;
    bool enabled = true;
    data->QueryBoolAttribute("enabled", &enabled);
    EDITOR_EXEC(CommitSceneEdit(true)); EDITOR_EXEC(BeginSceneEdit(label));
    // Deserialize on the existing instances so native component references and
    // actor identity/parenting survive the operation. Scene history owns undo.
    for (auto* component : targets)
    {
        component->SetEnabled(enabled);
        component->OnDeserialize(doc,data);
    }
    _QueueRefresh(); EDITOR_EXEC(CommitSceneEdit(true)); return true;
}

bool OvEditor::Panels::Inspector::PasteComponentValues(const std::string& type)
{
    return CanPasteComponentValues(type) && _ApplyComponentValues(type,m_clipboardXML,"Paste component values");
}

bool OvEditor::Panels::Inspector::ResetComponentValues(const std::string& type)
{
    const auto* description = GetComponentRegistry().Find(type);
    if (!description || m_targets.empty()) return false;
    bool playing = false; Actor defaults(0,"Component defaults","",playing); defaults.SetActive(false);
    auto* component = description->DeserializeComponent(defaults);
    return component && _ApplyComponentValues(type,ComponentXML(*component),"Reset component values");
}

bool OvEditor::Panels::Inspector::CanPasteComponentAsNew() const
{
    if (m_clipboardXML.empty() || m_targets.empty()) return false;
    const auto* description = GetComponentRegistry().Find(m_clipboardType);
    return description && std::all_of(m_targets.begin(),m_targets.end(),[&](auto* actor) { return actor->IsAlive() && description->IsAddable(*actor); });
}

bool OvEditor::Panels::Inspector::PasteComponentAsNew()
{
    if (!CanPasteComponentAsNew()) return false;
    tinyxml2::XMLDocument doc;
    auto* data = doc.Parse(m_clipboardXML.c_str()) == tinyxml2::XML_SUCCESS ? doc.FirstChildElement("data") : nullptr;
    if (!data) return false;
    bool enabled = true;
    data->QueryBoolAttribute("enabled", &enabled);
    const auto* description = GetComponentRegistry().Find(m_clipboardType);
    EDITOR_EXEC(CommitSceneEdit(true)); EDITOR_EXEC(BeginSceneEdit("Paste component as new"));
    for (auto* actor : m_targets)
    {
        description->AddComponent(*actor);
        if (auto* component = FindComponent(*actor,m_clipboardType))
        {
            component->SetEnabled(enabled);
            component->OnDeserialize(doc,data);
        }
    }
    _QueueRefresh(); EDITOR_EXEC(CommitSceneEdit(true)); return true;
}

bool OvEditor::Panels::Inspector::RemoveComponents(const std::string& type)
{
    if (m_targets.empty()) return false;
    EDITOR_EXEC(BeginSceneEdit("Remove component"));
    for (auto* actor : m_targets)
    {
        if (auto* component = FindComponent(*actor, type))
            actor->RemoveComponent(*component);
    }
    return true;
}

void OvEditor::Panels::Inspector::_AttachComponentMenu(Layout::GroupCollapsable& header, const std::string& type, bool removable)
{
    header.contextMenu = [this,type,removable,ticket = m_viewCallbacks.Ticket()] {
        if (ticket.expired()) return;
        if (ImGui::MenuItem("Copy component values")) CopyComponentValues(type);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy values from the primary inspected entity");
        if (ImGui::MenuItem("Paste component values",nullptr,false,CanPasteComponentValues(type)))
            m_callbacks.defer([this,type,ticket] { if (!ticket.expired()) PasteComponentValues(type); });
        if (ImGui::MenuItem("Paste component as new",nullptr,false,CanPasteComponentAsNew()))
            m_callbacks.defer([this,ticket] { if (!ticket.expired()) PasteComponentAsNew(); });
        ImGui::Separator();
        if (ImGui::MenuItem("Reset component"))
            m_callbacks.defer([this,type,ticket] { if (!ticket.expired()) ResetComponentValues(type); });
        if (removable)
        {
            ImGui::Separator();
            if (ImGui::MenuItem("Remove component"))
                m_callbacks.defer([this,type,ticket] { if (!ticket.expired()) RemoveComponents(type); });
        }
    };
}

OvEditor::Panels::InspectorBindings::PropertyContext OvEditor::Panels::Inspector::_PropertyContext(
    const InspectorBindings::PropertyDefaults* defaults)
{
    return {&m_propertyClipboard,defaults,m_callbacks.defer,
        [this,ticket = m_viewCallbacks.Ticket()] { if (!ticket.expired()) _QueueRefresh(); }};
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
    _ApplySearchFilter();
}

void OvEditor::Panels::Inspector::_PopulateActorInfo()
{
    auto ticket = m_viewCallbacks.Ticket();
    // Unity header: active toggle and name share one row, Tag below. The row keeps its
    // (hidden) titles so bindings, mixed-value labels and property menus still have names.
    auto populate = [ticket](OvUI::Internal::WidgetContainer& root, Actor& actor)
    {
        auto& row = root.CreateWidget<Layout::Group>();
        row.horizontal = true;
        row.stretchWidget = 3;
        OvCore::Helpers::GUIDrawer::DrawBoolean(row, "Active",
            [&actor, ticket] { return !ticket.expired() && actor.IsSelfActive(); },
            [&actor, ticket](bool value) { if (!ticket.expired()) actor.SetActive(value); });
        OvCore::Helpers::GUIDrawer::DrawString(row, "Name",
            [&actor, ticket] { return ticket.expired() ? std::string{} : actor.GetName(); },
            [&actor, ticket](const std::string& value) { if (!ticket.expired()) actor.SetName(value); });
        row.GetWidgets()[0].first->enabled = false;
        row.GetWidgets()[2].first->enabled = false;
        row.GetWidgets()[1].first->tooltip = "Active";

        auto& columns = root.CreateWidget<Layout::Columns<2>>();
        columns.widths[0] = 140 * OVUI_SCALE;
        OvCore::Helpers::GUIDrawer::CreateTitle(columns, "Tag");
        auto* tagLabel = dynamic_cast<Texts::TextColored*>(columns.GetWidgets().back().first);
        auto& tagWidget = columns.CreateWidget<TagSelector>(tagLabel);
        auto& tagDispatcher = tagWidget.AddPlugin<OvUI::Plugins::DataDispatcher<std::string>>();
        tagDispatcher.RegisterGatherer([&actor, ticket] { return ticket.expired() ? std::string{} : actor.GetTag(); });
        tagDispatcher.RegisterProvider([&actor, ticket](const std::string& value) { if (!ticket.expired()) actor.SetTag(value); });
    };
    auto& header = m_content->CreateWidget<Layout::Group>();
    header.SetID("inspector_actor_header");
    populate(header, m_targetActor.value());
    std::vector<OvUI::Internal::WidgetContainer*> roots{&header};
    for (size_t i = 1; i < m_targets.size(); ++i)
    {
        auto controls = std::make_unique<OvUI::Internal::WidgetContainer>();
        populate(*controls, *m_targets[i]);
        roots.push_back(controls.get()); m_batchControls.push_back(std::move(controls));
    }
    static const InspectorBindings::PropertyDefaults actorDefaults{{"1/1/Tag",std::string{}},{"0/1/Active",true}};
    InspectorBindings::Connect(roots, ticket,_PropertyContext(&actorDefaults));
    if (m_targets.size() > 1) return;

    if (CanEditPrefabInstance())
    {
        auto* prefabRoot = FindPrefabInstanceRoot(&m_targetActor.value());
        const std::string prefabSource = prefabRoot ? prefabRoot->GetPrefabSource() : std::string{};
        const bool isSubActor = prefabRoot && prefabRoot != &m_targetActor.value();
        auto& prefabCols = m_content->CreateWidget<Layout::Columns<2>>();
        prefabCols.SetID("inspector_prefab_row");
        prefabCols.widths[0] = 140 * OVUI_SCALE;
        OvCore::Helpers::GUIDrawer::CreateTitle(prefabCols, "Prefab");
        prefabCols.GetWidgets().back().first->tooltip =
            prefabSource + (isSubActor ? " (Child of Prefab Instance)" : "");
        prefabCols.CreateWidget<InspectorToolbar>([this, ticket, prefabSource]
        {
            if (ticket.expired()) return;
            const float scale = OVUI_SCALE;
            ImGui::PushID("InspectorPrefabRow");
            const float avail = std::max(90.0f * scale, ImGui::GetContentRegionAvail().x);
            const float spacing = 4.0f * scale;
            const float btnW = (avail - spacing * 2.0f) / 3.0f;
            if (ImGui::Button("Select##PrefabSelect", { btnW, 0.0f }))
                m_callbacks.defer([this, ticket] { if (!ticket.expired()) SelectPrefabAsset(); });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Inspect the source Prefab asset (%s)", prefabSource.c_str());
            ImGui::SameLine(0.0f, spacing);
            if (ImGui::Button("Revert##PrefabRevert", { btnW, 0.0f }))
                m_callbacks.defer([this, ticket] { if (!ticket.expired()) RevertPrefabInstance(); });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Revert this Prefab instance to the saved Prefab asset");
            ImGui::SameLine(0.0f, spacing);
            if (ImGui::Button("Apply##PrefabApply", { btnW, 0.0f }))
                m_callbacks.defer([this, ticket] { if (!ticket.expired()) ApplyPrefabInstance(); });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Apply overrides from this Prefab instance to the Prefab asset");
            ImGui::PopID();
        });
    }

	auto& identity = m_content->CreateWidget<Layout::GroupCollapsable>("Identity & Prefab");
    m_sections.push_back(&identity);
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
		[this, ticket] {
			if (ticket.expired() || !m_targetActor) return std::string{};
			if (auto* root = FindPrefabInstanceRoot(&m_targetActor.value()))
				return root->GetPrefabSource();
			return m_targetActor->GetPrefabSource();
		},
		[](const std::string&) {},
		OvTools::Utils::PathParser::EFileType::PREFAB
	);
	prefabSourceField.disabled = true;
}

void OvEditor::Panels::Inspector::_PopulateActorComponents()
{
	auto& components = m_targetActor->GetComponents();
	const bool hasRectTransform = m_targetActor->GetComponent<OvCore::ECS::Components::CRectTransform>() != nullptr;
	const int total = static_cast<int>(components.size());
	for (int i = 0; i < total; ++i)
	{
		if (hasRectTransform && dynamic_cast<OvCore::ECS::Components::CTransform*>(components[i].get()) != nullptr)
			continue;

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
        if (!ticket.expired()) OpenAddComponentPicker();
	};
}

void OvEditor::Panels::Inspector::OpenAddComponentPicker()
{
	if (!m_targetActor.has_value())
		return;

	const auto ticket = m_viewCallbacks.Ticket();

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
}

void OvEditor::Panels::Inspector::_DrawComponent(AComponent& p_component, int p_index, int p_total)
{
	auto& header = m_content->CreateWidget<Layout::GroupCollapsable>(p_component.GetName());
    m_sections.push_back(&header);
	const bool isTransform = dynamic_cast<CTransform*>(&p_component) != nullptr;
	const bool isRectTransform = dynamic_cast<OvCore::ECS::Components::CRectTransform*>(&p_component) != nullptr;
	const bool isSpatialTransform = isTransform || isRectTransform;
    _AttachComponentMenu(header,p_component.GetTypeName(),!isSpatialTransform);
	header.SetID("inspector_component_" + p_component.GetName());
	header.closable = !isSpatialTransform;
	header.CloseEvent += [this, &p_component, ticket = m_viewCallbacks.Ticket()] {
        if (ticket.expired()) return;
        RemoveComponents(p_component.GetTypeName());
	};

	if (isRectTransform)
	{
		if (auto* rectComp = dynamic_cast<OvCore::ECS::Components::CRectTransform*>(&p_component))
		{
			rectComp->LayoutChangedEvent += [this, ticket = m_viewCallbacks.Ticket()] {
				if (!ticket.expired()) _QueueRefresh();
			};
		}
	}

	if (!isSpatialTransform)
	{
		const std::string compType = p_component.GetTypeName();
		const auto ticket = m_viewCallbacks.Ticket();
		header.checkable = true;
		header.checked = p_component.IsSelfEnabled();
		header.checkedGatherer = [this, compType, ticket] {
			if (ticket.expired() || !m_targetActor || !m_targetActor->IsAlive()) return false;
			auto* comp = FindComponent(m_targetActor.value(), compType);
			return comp && comp->IsSelfEnabled();
		};
		header.mixedGatherer = [this, compType, ticket] {
			if (ticket.expired() || m_targets.size() <= 1) return false;
			auto* first = FindComponent(*m_targets[0], compType);
			if (!first) return false;
			const bool state = first->IsSelfEnabled();
			for (size_t i = 1; i < m_targets.size(); ++i)
			{
				auto* c = FindComponent(*m_targets[i], compType);
				if (c && c->IsSelfEnabled() != state) return true;
			}
			return false;
		};
		header.checkedProvider = [this, compType, ticket](bool enabled) {
			if (ticket.expired() || m_targets.empty()) return;
			EDITOR_EXEC(CommitSceneEdit(true));
			EDITOR_EXEC(BeginSceneEdit("Toggle component"));
			for (auto* actor : m_targets)
			{
				if (actor && actor->IsAlive())
				{
					if (auto* comp = FindComponent(*actor, compType))
						comp->SetEnabled(enabled);
				}
			}
			EDITOR_EXEC(CommitSceneEdit(true));
		};

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
    if (isTransform)
        for (auto [widget, mode] : columns.GetWidgets())
            if (auto* vector = dynamic_cast<Drags::DragMultipleScalars<float,3>*>(widget); vector && vector->showProportionalLock)
                vector->proportionalLink = &m_proportionalScale;
    std::vector<OvUI::Internal::WidgetContainer*> roots{&columns};
    for (size_t i = 1; i < m_targets.size(); ++i)
        for (const auto& component : m_targets[i]->GetComponents())
            if (component->GetTypeName() == p_component.GetTypeName())
            {
                auto controls = std::make_unique<OvUI::Internal::WidgetContainer>();
                component->OnInspector(*controls); roots.push_back(controls.get());
                m_batchControls.push_back(std::move(controls)); break;
            }
    // Snapshot native constructor defaults once per component type. Only owned
    // property values survive the temporary actor and its Inspector bindings.
    const auto type = p_component.GetTypeName();
    auto [entry,fresh] = m_propertyDefaults.try_emplace(type);
    if (fresh)
        if (const auto* description = GetComponentRegistry().Find(type))
        {
            bool playing = false; Actor owner(0,"Property defaults","",playing); owner.SetActive(false);
            if (auto* component = description->DeserializeComponent(owner))
            {
                OvUI::Internal::WidgetContainer controls; component->OnInspector(controls);
                InspectorBindings::CapturePropertyDefaults(controls,entry->second);
            }
        }
    InspectorBindings::Connect(roots, m_viewCallbacks.Ticket(),_PropertyContext(&entry->second));
}

void OvEditor::Panels::Inspector::_DrawBehaviour(Behaviour& p_behaviour, int p_index, int p_total)
{
	const auto identity = p_behaviour.CurrentIdentity();
	const auto simpleName = identity.type.substr(identity.type.find_last_of(".+") + 1);
	auto& header = m_content->CreateWidget<Layout::GroupCollapsable>(p_behaviour.Identity().backend == "lua" ?
		std::filesystem::path(p_behaviour.name).replace_extension().string() : simpleName + " (Script)");
    m_sections.push_back(&header);
	header.SetID("inspector_behaviour_" + identity.Key());
	if (identity.backend == "csharp") header.tooltip = identity.type + "\n" + identity.assembly;
	{
		const auto scriptIdentity = p_behaviour.Identity();
		const auto ticket = m_viewCallbacks.Ticket();
		header.checkable = true;
		header.checked = p_behaviour.IsSelfEnabled();
		header.checkedGatherer = [this, scriptIdentity, ticket] {
			if (ticket.expired() || !m_targetActor || !m_targetActor->IsAlive()) return false;
			auto* b = m_targetActor->GetBehaviour(scriptIdentity);
			return b && b->IsSelfEnabled();
		};
		header.mixedGatherer = [this, scriptIdentity, ticket] {
			if (ticket.expired() || m_targets.size() <= 1) return false;
			auto* first = m_targets[0]->GetBehaviour(scriptIdentity);
			if (!first) return false;
			const bool state = first->IsSelfEnabled();
			for (size_t i = 1; i < m_targets.size(); ++i)
			{
				auto* b = m_targets[i]->GetBehaviour(scriptIdentity);
				if (b && b->IsSelfEnabled() != state) return true;
			}
			return false;
		};
		header.checkedProvider = [this, scriptIdentity, ticket](bool enabled) {
			if (ticket.expired() || m_targets.empty()) return;
			EDITOR_EXEC(CommitSceneEdit(true));
			EDITOR_EXEC(BeginSceneEdit("Toggle script"));
			for (auto* actor : m_targets)
			{
				if (actor && actor->IsAlive())
				{
					if (auto* b = actor->GetBehaviour(scriptIdentity))
						b->SetEnabled(enabled);
				}
			}
			EDITOR_EXEC(CommitSceneEdit(true));
		};
	}
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
    InspectorBindings::Connect(roots, m_viewCallbacks.Ticket(),_PropertyContext());
}

void OvEditor::Panels::Inspector::_PopulateAsset()
{
    using EFileType = OvTools::Utils::PathParser::EFileType;
    m_emptyState->enabled = false;
    m_content->enabled = true;
    auto ticket = m_viewCallbacks.Ticket();

    const auto embedded = OvRendering::Resources::Parsers::ParseEmbeddedAssetPath(m_targetAsset);
    const bool readOnly = m_targetAsset.starts_with(":") || embedded.has_value();
    const std::string elementName = embedded ? embedded->assetName : OvTools::Utils::PathParser::GetElementName(m_targetAsset);
    const EFileType fileType = OvTools::Utils::PathParser::GetFileType(elementName);
    const std::string friendlyType = OvTools::Utils::PathParser::FileTypeToString(fileType);
    std::string realPath;
    if (auto* actions = OvCore::Global::ServiceLocator::TryGet<OvEditor::Core::EditorActions>(); actions && !embedded)
        realPath = actions->GetRealPath(m_targetAsset);

    const bool canPreviewInAssetView =
        fileType == EFileType::MODEL || fileType == EFileType::TEXTURE || fileType == EFileType::MATERIAL;

    m_content->CreateWidget<InspectorToolbar>([this, ticket, elementName, friendlyType, readOnly, canPreviewInAssetView]
    {
        if (ticket.expired()) return;
        const float scale = OVUI_SCALE;
        ImGui::PushID("InspectorAssetHeader");
        if (auto* actions = OvCore::Global::ServiceLocator::TryGet<OvEditor::Core::EditorActions>())
        {
            if (auto* iconTex = actions->GetContext().editorResources->GetFileIcon(elementName))
            {
                OvUI::Internal::TextureID icon{ iconTex->RetainTexture() };
                icon.RetainForCapture();
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const float iconSize = 32.0f * scale;
                ImGui::GetWindowDrawList()->AddImage(icon.id, pos, { pos.x + iconSize, pos.y + iconSize }, { 0.0f, 1.0f }, { 1.0f, 0.0f });
                ImGui::Dummy({ iconSize, iconSize });
                ImGui::SameLine(0.0f, 8.0f * scale);
            }
        }
        ImGui::BeginGroup();
        ImGui::TextUnformatted(elementName.c_str());
        ImGui::TextDisabled("%s Asset%s", friendlyType.c_str(), readOnly ? " (Read-Only)" : "");
        ImGui::EndGroup();

        const float openWidth = 54.0f * scale;
        const float viewWidth = canPreviewInAssetView ? (82.0f * scale + 6.0f * scale) : 0.0f;
        const float rightStart = ImGui::GetWindowContentRegionMax().x - openWidth - viewWidth;
        if (rightStart > ImGui::GetCursorPosX() + 8.0f * scale)
        {
            ImGui::SameLine(rightStart);
            if (canPreviewInAssetView)
            {
                if (ImGui::Button("Asset View", { 82.0f * scale, 0.0f })) PreviewAsset();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open in the 3D / 2D Asset View panel");
                ImGui::SameLine(0.0f, 6.0f * scale);
            }
            if (ImGui::Button("Open", { openWidth, 0.0f }))
                OvCore::Helpers::GUIHelpers::Open(m_targetAsset);
        }
        ImGui::PopID();
    });
    m_content->CreateWidget<Visual::Separator>();

    if (!embedded && (fileType == EFileType::MODEL || fileType == EFileType::TEXTURE))
    {
        if (!realPath.empty())
            m_assetMetadata = std::make_unique<OvTools::Filesystem::IniFile>(realPath + ".meta");
        else
            m_assetMetadata.reset();

        if (m_assetMetadata)
        {
            auto& settingsGroup = m_content->CreateWidget<Layout::GroupCollapsable>("Import Settings");
            settingsGroup.SetID("inspector_asset_import_settings");
            m_sections.push_back(&settingsGroup);
            auto& settingsColumns = settingsGroup.CreateWidget<Layout::Columns<2>>();
            settingsColumns.SetID("inspector_asset_import_columns");
            settingsColumns.widths[0] = 150 * OVUI_SCALE;
            settingsColumns.disabled = readOnly;
            _PopulateAssetSettings(settingsColumns, fileType);

            settingsGroup.CreateWidget<InspectorToolbar>([this, ticket, readOnly]
            {
                if (ticket.expired()) return;
                const float scale = OVUI_SCALE;
                ImGui::PushID("InspectorAssetActions");
                ImGui::BeginDisabled(readOnly);
                if (ImGui::Button("Revert", { 64.0f * scale, 0.0f }))
                    m_callbacks.defer([this, ticket] { if (!ticket.expired()) RevertAssetSettings(); });
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Reload saved import settings from disk");
                ImGui::SameLine(0.0f, 6.0f * scale);
                if (ImGui::Button("Reset", { 64.0f * scale, 0.0f }))
                    m_callbacks.defer([this, ticket] { if (!ticket.expired()) ResetAssetSettings(); });
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Reset import settings to defaults");
                ImGui::SameLine(0.0f, 6.0f * scale);
                if (ImGui::Button("Apply", { 64.0f * scale, 0.0f }))
                    m_callbacks.defer([this, ticket] { if (!ticket.expired()) ApplyAssetSettings(); });
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Save .meta settings and reimport the asset");
                ImGui::EndDisabled();
                ImGui::PopID();
            });
        }
    }
    else
    {
        m_assetMetadata.reset();
    }

    auto& infoGroup = m_content->CreateWidget<Layout::GroupCollapsable>("Asset Info");
    infoGroup.SetID("inspector_asset_info");
    m_sections.push_back(&infoGroup);
    auto& infoCols = infoGroup.CreateWidget<Layout::Columns<2>>();
    infoCols.SetID("inspector_asset_info_columns");
    infoCols.widths[0] = 140 * OVUI_SCALE;
    OvCore::Helpers::GUIDrawer::CreateTitle(infoCols, "Resource");
    infoCols.CreateWidget<Texts::Text>(OvTools::Utils::PathParser::GetFriendlyPath(m_targetAsset));
    OvCore::Helpers::GUIDrawer::CreateTitle(infoCols, "Type");
    infoCols.CreateWidget<Texts::Text>(friendlyType);
    if (!realPath.empty() && std::filesystem::exists(realPath))
    {
        OvCore::Helpers::GUIDrawer::CreateTitle(infoCols, "Path");
        infoCols.CreateWidget<Texts::Text>(realPath);
        OvCore::Helpers::GUIDrawer::CreateTitle(infoCols, "Size");
        const auto [size, unit] = OvTools::Utils::SizeConverter::ConvertToOptimalUnit(
            static_cast<float>(std::filesystem::file_size(realPath)),
            OvTools::Utils::SizeConverter::ESizeUnit::BYTE);
        infoCols.CreateWidget<Texts::Text>(std::format("{:.1f} {}", size, OvTools::Utils::SizeConverter::UnitToString(unit)));
        OvCore::Helpers::GUIDrawer::CreateTitle(infoCols, "Metadata");
        infoCols.CreateWidget<Texts::Text>(std::filesystem::exists(realPath + ".meta") ? "Yes" : "No");
    }
    if (fileType == EFileType::TEXTURE)
    {
        if (auto* tm = OvCore::Global::ServiceLocator::TryGet<OvCore::ResourceManagement::TextureManager>())
            if (auto* tex = (*tm)[m_targetAsset])
            {
                const auto& desc = tex->GetTexture().GetDesc();
                OvCore::Helpers::GUIDrawer::CreateTitle(infoCols, "Dimensions");
                infoCols.CreateWidget<Texts::Text>(std::format("{} x {}", desc.width, desc.height));
            }
    }
    else if (fileType == EFileType::MODEL)
    {
        if (auto* mm = OvCore::Global::ServiceLocator::TryGet<OvCore::ResourceManagement::ModelManager>())
            if (auto* model = (*mm)[m_targetAsset])
            {
                OvCore::Helpers::GUIDrawer::CreateTitle(infoCols, "Meshes");
                infoCols.CreateWidget<Texts::Text>(std::to_string(model->GetMeshes().size()));
                OvCore::Helpers::GUIDrawer::CreateTitle(infoCols, "Material Slots");
                infoCols.CreateWidget<Texts::Text>(std::to_string(model->GetMaterialNames().size()));
            }
    }
    else if (fileType == EFileType::MATERIAL)
    {
        if (auto* matm = OvCore::Global::ServiceLocator::TryGet<OvCore::ResourceManagement::MaterialManager>())
            if (auto* mat = (*matm)[m_targetAsset])
            {
                OvCore::Helpers::GUIDrawer::CreateTitle(infoCols, "Shader");
                infoCols.CreateWidget<Texts::Text>(mat->GetShader() ? OvTools::Utils::PathParser::GetFriendlyPath(mat->GetShader()->path) : "None");
            }
    }
    size_t prefabActorCount = 0;
    std::string prefabRootName;
    if (fileType == EFileType::PREFAB && !realPath.empty() && std::filesystem::exists(realPath))
    {
        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(realPath.c_str()) == tinyxml2::XML_SUCCESS)
            if (auto* rootNode = doc.FirstChildElement("root"))
                if (auto* prefabNode = rootNode->FirstChildElement("prefab"))
                    if (auto* actorsNode = prefabNode->FirstChildElement("actors"))
                        for (auto* actorNode = actorsNode->FirstChildElement("actor"); actorNode; actorNode = actorNode->NextSiblingElement("actor"))
                        {
                            ++prefabActorCount;
                            if (prefabRootName.empty())
                                if (auto* nameNode = actorNode->FirstChildElement("name"); nameNode && nameNode->GetText())
                                    prefabRootName = nameNode->GetText();
                        }
        if (prefabActorCount > 0)
        {
            OvCore::Helpers::GUIDrawer::CreateTitle(infoCols, "Root Entity");
            infoCols.CreateWidget<Texts::Text>(prefabRootName.empty() ? "<unnamed>" : prefabRootName);
            OvCore::Helpers::GUIDrawer::CreateTitle(infoCols, "Entities");
            infoCols.CreateWidget<Texts::Text>(std::to_string(prefabActorCount));
        }
    }

    auto& previewGroup = m_content->CreateWidget<Layout::GroupCollapsable>("Preview");
    previewGroup.SetID("inspector_asset_preview");
    m_sections.push_back(&previewGroup);
    previewGroup.CreateWidget<InspectorToolbar>([this, ticket, fileType, elementName, friendlyType, prefabActorCount, prefabRootName]
    {
        if (ticket.expired()) return;
        const float scale = OVUI_SCALE;
        ImGui::PushID("InspectorAssetPreview");
        const float width = std::max(120.0f * scale, ImGui::GetContentRegionAvail().x);
        const float boxHeight = 180.0f * scale;
        const ImVec2 boxMin = ImGui::GetCursorScreenPos();
        const ImVec2 boxMax{ boxMin.x + width, boxMin.y + boxHeight };
        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(boxMin, boxMax, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f * scale);
        drawList->AddRect(boxMin, boxMax, ImGui::GetColorU32(ImGuiCol_Border), 4.0f * scale);

        bool drewCustom = false;
        if (fileType == EFileType::TEXTURE)
        {
            if (auto* tm = OvCore::Global::ServiceLocator::TryGet<OvCore::ResourceManagement::TextureManager>())
                if (auto* tex = (*tm)[m_targetAsset])
                {
                    OvUI::Internal::TextureID tid{ tex->RetainTexture() };
                    tid.RetainForCapture();
                    const auto& desc = tex->GetTexture().GetDesc();
                    const float aspect = (desc.width > 0 && desc.height > 0) ? static_cast<float>(desc.width) / static_cast<float>(desc.height) : 1.0f;
                    const float maxW = width - 24.0f * scale;
                    const float maxH = boxHeight - 32.0f * scale;
                    ImVec2 imgSize{ maxW, maxH };
                    if (imgSize.x / imgSize.y > aspect) imgSize.x = imgSize.y * aspect;
                    else imgSize.y = imgSize.x / aspect;
                    const ImVec2 center{ (boxMin.x + boxMax.x) * 0.5f, boxMin.y + (boxHeight - 18.0f * scale) * 0.5f };
                    drawList->AddImage(tid.id, { center.x - imgSize.x * 0.5f, center.y - imgSize.y * 0.5f },
                        { center.x + imgSize.x * 0.5f, center.y + imgSize.y * 0.5f }, { 0.0f, 1.0f }, { 1.0f, 0.0f });
                    const std::string caption = std::format("{} ({}x{})", elementName, desc.width, desc.height);
                    const float textW = ImGui::CalcTextSize(caption.c_str()).x;
                    drawList->AddText({ boxMin.x + std::max(6.0f * scale, (width - textW) * 0.5f), boxMax.y - 20.0f * scale },
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), caption.c_str());
                    drewCustom = true;
                }
        }
        else if (fileType == EFileType::MODEL)
        {
            if (auto* mm = OvCore::Global::ServiceLocator::TryGet<OvCore::ResourceManagement::ModelManager>())
                if (auto* model = (*mm)[m_targetAsset])
                {
                    ImGui::SetCursorScreenPos({ boxMin.x + 12.0f * scale, boxMin.y + 10.0f * scale });
                    ImGui::BeginGroup();
                    ImGui::Text("%s (%zu meshes)", elementName.c_str(), model->GetMeshes().size());
                    ImGui::Spacing();
                    const auto& mats = model->GetMaterialNames();
                    for (size_t i = 0; i < std::min<size_t>(mats.size(), 5); ++i)
                        ImGui::TextDisabled("Slot %zu: %s", i, mats[i].empty() ? "<unnamed>" : mats[i].c_str());
                    if (mats.size() > 5)
                        ImGui::TextDisabled("+%zu more material slots", mats.size() - 5);
                    ImGui::EndGroup();
                    drewCustom = true;
                }
        }
        else if (fileType == EFileType::MATERIAL)
        {
            if (auto* matm = OvCore::Global::ServiceLocator::TryGet<OvCore::ResourceManagement::MaterialManager>())
                if (auto* mat = (*matm)[m_targetAsset])
                {
                    ImGui::SetCursorScreenPos({ boxMin.x + 12.0f * scale, boxMin.y + 10.0f * scale });
                    ImGui::BeginGroup();
                    ImGui::Text("%s", elementName.c_str());
                    ImGui::TextDisabled("Shader: %s", mat->GetShader() ? OvTools::Utils::PathParser::GetFriendlyPath(mat->GetShader()->path).c_str() : "None");
                    ImGui::Spacing();
                    ImGui::TextDisabled("Blendable: %s | Two-Sided: %s", mat->IsBlendable() ? "Yes" : "No", (!mat->HasBackfaceCulling() && !mat->HasFrontfaceCulling()) ? "Yes" : "No");
                    ImGui::Spacing();
                    if (ImGui::Button("Edit in Material Editor"))
                        OvCore::Helpers::GUIHelpers::Open(m_targetAsset);
                    ImGui::EndGroup();
                    drewCustom = true;
                }
        }
        else if (fileType == EFileType::PREFAB && prefabActorCount > 0)
        {
            ImGui::SetCursorScreenPos({ boxMin.x + 12.0f * scale, boxMin.y + 10.0f * scale });
            ImGui::BeginGroup();
            ImGui::Text("%s", elementName.c_str());
            ImGui::TextDisabled("Root Entity: %s (%zu %s)",
                prefabRootName.empty() ? "<unnamed>" : prefabRootName.c_str(),
                prefabActorCount,
                prefabActorCount == 1 ? "entity" : "entities");
            ImGui::Spacing();
            if (ImGui::Button("Instantiate in Scene"))
            {
                const std::string targetPath = m_targetAsset;
                m_callbacks.defer([targetPath]
                {
                    if (auto* actions = OvCore::Global::ServiceLocator::TryGet<OvEditor::Core::EditorActions>())
                        if (auto* instance = actions->InstantiatePrefab(targetPath))
                            actions->SelectActor(*instance);
                });
            }
            ImGui::EndGroup();
            drewCustom = true;
        }

        if (!drewCustom)
        {
            if (auto* actions = OvCore::Global::ServiceLocator::TryGet<OvEditor::Core::EditorActions>())
                if (auto* iconTex = actions->GetContext().editorResources->GetFileIcon(elementName))
                {
                    OvUI::Internal::TextureID tid{ iconTex->RetainTexture() };
                    tid.RetainForCapture();
                    const float iconSize = 64.0f * scale;
                    const ImVec2 center{ (boxMin.x + boxMax.x) * 0.5f, boxMin.y + (boxHeight - 20.0f * scale) * 0.5f };
                    drawList->AddImage(tid.id, { center.x - iconSize * 0.5f, center.y - iconSize * 0.5f },
                        { center.x + iconSize * 0.5f, center.y + iconSize * 0.5f }, { 0.0f, 1.0f }, { 1.0f, 0.0f });
                }
            const std::string caption = elementName + " (" + friendlyType + ")";
            const float textW = ImGui::CalcTextSize(caption.c_str()).x;
            drawList->AddText({ boxMin.x + std::max(6.0f * scale, (width - textW) * 0.5f), boxMax.y - 20.0f * scale },
                ImGui::GetColorU32(ImGuiCol_TextDisabled), caption.c_str());
        }

        ImGui::SetCursorScreenPos(boxMin);
        ImGui::Dummy({ width, boxHeight });
        ImGui::PopID();
    });

    _ApplySearchFilter();
}

void OvEditor::Panels::Inspector::_PopulateAssetSettings(
    Layout::Columns<2>& p_columns,
    OvTools::Utils::PathParser::EFileType p_fileType)
{
    if (!m_assetMetadata) return;
    using EFileType = OvTools::Utils::PathParser::EFileType;
    if (p_fileType == EFileType::MODEL)
    {
        static constexpr std::pair<const char*, bool> kModelFlags[] = {
            {"GENERATE_EMBEDDED_ASSETS", true},
            {"CALC_TANGENT_SPACE", true},
            {"JOIN_IDENTICAL_VERTICES", true},
            {"MAKE_LEFT_HANDED", false},
            {"TRIANGULATE", true},
            {"REMOVE_COMPONENT", false},
            {"GEN_NORMALS", false},
            {"GEN_SMOOTH_NORMALS", true},
            {"SPLIT_LARGE_MESHES", false},
            {"PRE_TRANSFORM_VERTICES", false},
            {"LIMIT_BONE_WEIGHTS", true},
            {"VALIDATE_DATA_STRUCTURE", false},
            {"IMPROVE_CACHE_LOCALITY", true},
            {"REMOVE_REDUNDANT_MATERIALS", false},
            {"FIX_INFACING_NORMALS", false},
            {"SORT_BY_PTYPE", false},
            {"FIND_DEGENERATES", false},
            {"FIND_INVALID_DATA", true},
            {"GEN_UV_COORDS", true},
            {"TRANSFORM_UV_COORDS", false},
            {"FIND_INSTANCES", true},
            {"OPTIMIZE_MESHES", true},
            {"OPTIMIZE_GRAPH", true},
            {"FLIP_UVS", false},
            {"FLIP_WINDING_ORDER", false},
            {"SPLIT_BY_BONE_COUNT", false},
            {"DEBONE", false},
            {"GLOBAL_SCALE", true},
            {"EMBED_TEXTURES", false},
            {"FORCE_GEN_NORMALS", false},
            {"DROP_NORMALS", false},
            {"GEN_BOUNDING_BOXES", false}
        };
        for (const auto& [key, defVal] : kModelFlags)
        {
            m_assetMetadata->Add(key, defVal);
            const std::string settingKey = key;
            OvCore::Helpers::GUIDrawer::DrawBoolean(p_columns, settingKey,
                [this, settingKey] { return m_assetMetadata ? m_assetMetadata->Get<bool>(settingKey) : false; },
                [this, settingKey](bool value) { if (m_assetMetadata) m_assetMetadata->Set<bool>(settingKey, value); });
        }
    }
    else if (p_fileType == EFileType::TEXTURE)
    {
        using namespace OvRendering::Settings;
        using namespace baregl::types;
        const std::string kMinFilter = "MIN_FILTER";
        const std::string kMagFilter = "MAG_FILTER";
        const std::string kHorizontalWrap = "HORIZONTAL_WRAP";
        const std::string kVerticalWrap = "VERTICAL_WRAP";
        const std::string kEnableMipmapping = "ENABLE_MIPMAPPING";

        m_assetMetadata->Add(kMinFilter, static_cast<int>(ETextureFilteringMode::LINEAR_MIPMAP_LINEAR));
        m_assetMetadata->Add(kMagFilter, static_cast<int>(ETextureFilteringMode::LINEAR));
        m_assetMetadata->Add(kHorizontalWrap, static_cast<int>(ETextureWrapMode::REPEAT));
        m_assetMetadata->Add(kVerticalWrap, static_cast<int>(ETextureWrapMode::REPEAT));
        m_assetMetadata->Add(kEnableMipmapping, true);

        const auto filteringModes = std::map<int, std::string>{
            {static_cast<int>(ETextureFilteringMode::NEAREST), "NEAREST"},
            {static_cast<int>(ETextureFilteringMode::LINEAR), "LINEAR"},
            {static_cast<int>(ETextureFilteringMode::NEAREST_MIPMAP_NEAREST), "NEAREST_MIPMAP_NEAREST"},
            {static_cast<int>(ETextureFilteringMode::LINEAR_MIPMAP_LINEAR), "LINEAR_MIPMAP_LINEAR"},
            {static_cast<int>(ETextureFilteringMode::LINEAR_MIPMAP_NEAREST), "LINEAR_MIPMAP_NEAREST"},
            {static_cast<int>(ETextureFilteringMode::NEAREST_MIPMAP_LINEAR), "NEAREST_MIPMAP_LINEAR"}
        };
        const auto wrapModes = std::map<int, std::string>{
            {static_cast<int>(ETextureWrapMode::REPEAT), "REPEAT"},
            {static_cast<int>(ETextureWrapMode::CLAMP_TO_EDGE), "CLAMP_TO_EDGE"},
            {static_cast<int>(ETextureWrapMode::CLAMP_TO_BORDER), "CLAMP_TO_BORDER"},
            {static_cast<int>(ETextureWrapMode::MIRRORED_REPEAT), "MIRRORED_REPEAT"},
            {static_cast<int>(ETextureWrapMode::MIRROR_CLAMP_TO_EDGE), "MIRROR_CLAMP_TO_EDGE"}
        };

        OvCore::Helpers::GUIDrawer::CreateTitle(p_columns, kMinFilter);
        auto& minFilter = p_columns.CreateWidget<Selection::ComboBox>(m_assetMetadata->Get<int>(kMinFilter));
        minFilter.choices = filteringModes;
        minFilter.ValueChangedEvent += [this, kMinFilter](int choice) { if (m_assetMetadata) m_assetMetadata->Set(kMinFilter, choice); };

        OvCore::Helpers::GUIDrawer::CreateTitle(p_columns, kMagFilter);
        auto& magFilter = p_columns.CreateWidget<Selection::ComboBox>(m_assetMetadata->Get<int>(kMagFilter));
        magFilter.choices = filteringModes;
        magFilter.ValueChangedEvent += [this, kMagFilter](int choice) { if (m_assetMetadata) m_assetMetadata->Set(kMagFilter, choice); };

        OvCore::Helpers::GUIDrawer::CreateTitle(p_columns, kHorizontalWrap);
        auto& horizontalWrap = p_columns.CreateWidget<Selection::ComboBox>(m_assetMetadata->Get<int>(kHorizontalWrap));
        horizontalWrap.choices = wrapModes;
        horizontalWrap.ValueChangedEvent += [this, kHorizontalWrap](int choice) { if (m_assetMetadata) m_assetMetadata->Set(kHorizontalWrap, choice); };

        OvCore::Helpers::GUIDrawer::CreateTitle(p_columns, kVerticalWrap);
        auto& verticalWrap = p_columns.CreateWidget<Selection::ComboBox>(m_assetMetadata->Get<int>(kVerticalWrap));
        verticalWrap.choices = wrapModes;
        verticalWrap.ValueChangedEvent += [this, kVerticalWrap](int choice) { if (m_assetMetadata) m_assetMetadata->Set(kVerticalWrap, choice); };

        OvCore::Helpers::GUIDrawer::DrawBoolean(p_columns, kEnableMipmapping,
            [this, kEnableMipmapping] { return m_assetMetadata ? m_assetMetadata->Get<bool>(kEnableMipmapping) : true; },
            [this, kEnableMipmapping](bool value) { if (m_assetMetadata) m_assetMetadata->Set<bool>(kEnableMipmapping, value); });
    }
}

void OvEditor::Panels::Inspector::ApplyAssetSettings()
{
    if (m_targetAsset.empty() || !m_assetMetadata) return;
    m_assetMetadata->Rewrite();
    const auto fileType = OvTools::Utils::PathParser::GetFileType(m_targetAsset);
    if (fileType == OvTools::Utils::PathParser::EFileType::MODEL)
    {
        if (auto* mm = OvCore::Global::ServiceLocator::TryGet<OvCore::ResourceManagement::ModelManager>())
            if (mm->IsResourceRegistered(m_targetAsset))
            {
                mm->AResourceManager::ReloadResource(m_targetAsset);
                if (auto* actions = OvCore::Global::ServiceLocator::TryGet<OvEditor::Core::EditorActions>())
                    actions->GetPanelsManager().GetPanelAs<OvEditor::Panels::MaterialEditor>("Material Editor").Refresh();
            }
    }
    else if (fileType == OvTools::Utils::PathParser::EFileType::TEXTURE)
    {
        if (auto* tm = OvCore::Global::ServiceLocator::TryGet<OvCore::ResourceManagement::TextureManager>())
            if (tm->IsResourceRegistered(m_targetAsset))
                tm->AResourceManager::ReloadResource(m_targetAsset);
    }
    Refresh();
}

void OvEditor::Panels::Inspector::RevertAssetSettings()
{
    if (m_targetAsset.empty()) return;
    Refresh();
}

void OvEditor::Panels::Inspector::ResetAssetSettings()
{
    if (m_targetAsset.empty() || !m_assetMetadata) return;
    m_assetMetadata->RemoveAll();
    const auto fileType = OvTools::Utils::PathParser::GetFileType(m_targetAsset);
    Layout::Columns<2> dummy;
    _PopulateAssetSettings(dummy, fileType);
    m_assetMetadata->Rewrite();
    Refresh();
}

void OvEditor::Panels::Inspector::PreviewAsset()
{
    if (m_targetAsset.empty()) return;
    auto* actions = OvCore::Global::ServiceLocator::TryGet<OvEditor::Core::EditorActions>();
    if (!actions) return;
    auto& assetView = actions->GetPanelsManager().GetPanelAs<OvEditor::Panels::AssetView>("Asset View");
    const auto fileType = OvTools::Utils::PathParser::GetFileType(m_targetAsset);
    if (fileType == OvTools::Utils::PathParser::EFileType::MODEL)
    {
        if (auto* resource = OVSERVICE(OvCore::ResourceManagement::ModelManager).GetResource(m_targetAsset))
            assetView.SetResource(resource);
    }
    else if (fileType == OvTools::Utils::PathParser::EFileType::TEXTURE)
    {
        if (auto* resource = OVSERVICE(OvCore::ResourceManagement::TextureManager).GetResource(m_targetAsset))
            assetView.SetResource(resource);
    }
    else if (fileType == OvTools::Utils::PathParser::EFileType::MATERIAL)
    {
        if (auto* resource = OVSERVICE(OvCore::ResourceManagement::MaterialManager).GetResource(m_targetAsset))
            assetView.SetResource(resource);
    }
    assetView.Open();
    assetView.Focus();
}

void OvEditor::Panels::Inspector::Refresh()
{
    _InvalidateView();
    m_refreshPending = false;
	if (m_targetActor)
	{
		m_content->RemoveAllWidgets();
        m_batchControls.clear();
        m_sections.clear();
		_Populate();
	}
    else if (!m_targetAsset.empty())
    {
        m_content->RemoveAllWidgets();
        m_batchControls.clear();
        m_sections.clear();
        _PopulateAsset();
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
