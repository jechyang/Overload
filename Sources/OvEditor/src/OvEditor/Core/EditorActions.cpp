/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <algorithm>
#include <charconv>
#include <cmath>
#include "OvDebug/Assertion.h"
#include "OvTools/Utils/OptRef.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <tinyxml2.h>
#include <imgui.h>

#include <OvDebug/Logger.h>
#include <OvUI/Styling/Style.h>

#include <OvCore/ECS/Components/CAudioSource.h>
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/ECS/Components/CPhysicalBox.h>
#include <OvCore/ECS/Components/CPhysicalCapsule.h>
#include <OvCore/ECS/Components/CPhysicalSphere.h>
#include <OvCore/Resources/Loaders/MaterialLoader.h>

#include <OvCore/Helpers/GUIDrawer.h>
#include <OvCore/Helpers/GUIHelpers.h>

#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Core/GizmoBehaviour.h>
#include <OvEditor/Helpers/PickerHelpers.h>
#include <OvEditor/Panels/AssetBrowser.h>
#include <OvEditor/Panels/AssetView.h>
#include <OvEditor/Panels/GameView.h>
#include <OvEditor/Panels/Inspector.h>
#include <OvEditor/Panels/MaterialEditor.h>
#include <OvEditor/Panels/ProjectSettings.h>
#include <OvEditor/Panels/SceneView.h>
#include <OvEditor/Settings/EditorSettings.h>
#include <OvEditor/Utils/FileSystem.h>
#include <OvCore/SceneSystem/PrefabOperations.h>

#include <OvTools/Utils/PathParser.h>
#include <OvTools/Utils/String.h>
#include <OvTools/Utils/SystemCalls.h>
#include <OvRendering/Data/Image.h>
#include <OvRendering/Resources/Parsers/EmbeddedAssetPath.h>

#include <OvWindowing/Dialogs/OpenFileDialog.h>
#include <OvWindowing/Dialogs/MessageBox.h>
#include <OvWindowing/Dialogs/SaveFileDialog.h>

namespace
{
	constexpr std::string_view kDefaultMaterialPath = ":Materials\\Default.ovmat";

	OvCore::ECS::Actor* ResolvePrefabInstanceRoot(OvCore::ECS::Actor& p_actor)
	{
		auto* resolvedRoot = &p_actor;

		while (resolvedRoot && !resolvedRoot->HasPrefabSource())
		{
			resolvedRoot = resolvedRoot->GetParent();
		}

		if (!resolvedRoot)
		{
			OVLOG_ERROR("Failed to resolve prefab instance root for actor \"" + p_actor.GetName() + "\": no prefab source found in actor hierarchy.");
			return nullptr;
		}

		return resolvedRoot;
	}

	bool TryParseUInt64(const char* p_text, uint64_t& p_value)
	{
		if (!p_text)
		{
			return false;
		}

		const std::string_view text{ p_text };
		const auto [parseEnd, error] = std::from_chars(text.data(), text.data() + text.size(), p_value);
		return error == std::errc{} && parseEnd == text.data() + text.size();
	}

	void RemapActorReferenceGuidsInActorNode(
		tinyxml2::XMLElement& p_actorNode,
		const std::unordered_map<uint64_t, uint64_t>& p_guidRemap)
	{
		auto* behavioursNode = p_actorNode.FirstChildElement("behaviours");

		if (!behavioursNode)
		{
			return;
		}

		for (auto* behaviourNode = behavioursNode->FirstChildElement("behaviour");
			behaviourNode;
			behaviourNode = behaviourNode->NextSiblingElement("behaviour"))
		{
			auto* dataNode = behaviourNode->FirstChildElement("data");
			auto* scriptPropertiesNode = dataNode ? dataNode->FirstChildElement("script_properties") : nullptr;

			if (!scriptPropertiesNode)
			{
				continue;
			}

			for (auto* propertyNode = scriptPropertiesNode->FirstChildElement();
				propertyNode;
				propertyNode = propertyNode->NextSiblingElement())
			{
				const char* typeAttribute = propertyNode->Attribute("type");

				if (!typeAttribute || std::string_view{ typeAttribute } != "actor")
				{
					continue;
				}

				uint64_t sourceGuid = 0;

				if (!TryParseUInt64(propertyNode->GetText(), sourceGuid))
				{
					continue;
				}

				if (const auto it = p_guidRemap.find(sourceGuid); it != p_guidRemap.end())
				{
					propertyNode->SetText(std::to_string(it->second).c_str());
				}
			}
		}
	}

	struct ActorHierarchyEntry
	{
		OvCore::ECS::Actor* actor = nullptr;
		OvCore::ECS::Actor* parent = nullptr;
	};

	void CollectActorHierarchy(
		OvCore::ECS::Actor& p_actor,
		OvCore::ECS::Actor* p_parent,
		std::vector<ActorHierarchyEntry>& p_outEntries)
	{
		p_outEntries.push_back({ &p_actor, p_parent });

		for (auto* child : p_actor.GetChildren())
		{
			CollectActorHierarchy(*child, &p_actor, p_outEntries);
		}
	}

	uint64_t GetPrefabNodeGUIDOrFallback(OvCore::ECS::Actor& p_actor)
	{
		if (p_actor.HasPrefabNodeGUID())
		{
			return p_actor.GetPrefabNodeGUID();
		}

		const uint64_t fallbackGUID = p_actor.GetGUID();
		p_actor.SetPrefabNodeGUID(fallbackGUID);
		return fallbackGUID;
	}

	OvCore::ECS::Actor* FindUnusedActorWithPrefabNodeGUID(
		const uint64_t p_prefabNodeGUID,
		const std::unordered_map<uint64_t, std::vector<OvCore::ECS::Actor*>>& p_actorsByPrefabNodeGUID,
		const std::unordered_set<OvCore::ECS::Actor*>& p_usedActors)
	{
		if (const auto found = p_actorsByPrefabNodeGUID.find(p_prefabNodeGUID); found != p_actorsByPrefabNodeGUID.end())
		{
			for (auto* actor : found->second)
			{
				if (!p_usedActors.contains(actor))
				{
					return actor;
				}
			}
		}

		return nullptr;
	}

	std::unordered_map<uint64_t, uint64_t> GatherSerializedGuidsByPrefabNodeGUID(const std::filesystem::path& p_prefabPath)
	{
		std::unordered_map<uint64_t, uint64_t> serializedGuidsByPrefabNodeGUID;

		tinyxml2::XMLDocument doc;
		if (doc.LoadFile(p_prefabPath.string().c_str()) != tinyxml2::XML_SUCCESS)
		{
			return serializedGuidsByPrefabNodeGUID;
		}

		auto* rootNode = doc.FirstChildElement("root");
		auto* prefabNode = rootNode ? rootNode->FirstChildElement("prefab") : nullptr;
		auto* actorsNode = prefabNode ? prefabNode->FirstChildElement("actors") : nullptr;
		if (!actorsNode)
		{
			return serializedGuidsByPrefabNodeGUID;
		}

		for (auto* actorNode = actorsNode->FirstChildElement("actor");
			actorNode;
			actorNode = actorNode->NextSiblingElement("actor"))
		{
			const char* serializedGuidText = actorNode->FirstChildElement("guid")
				? actorNode->FirstChildElement("guid")->GetText()
				: nullptr;
			uint64_t serializedGUID = 0;
			if (!TryParseUInt64(serializedGuidText, serializedGUID))
			{
				continue;
			}

			const char* prefabNodeGuidText = actorNode->FirstChildElement("prefab_node_guid")
				? actorNode->FirstChildElement("prefab_node_guid")->GetText()
				: nullptr;
			uint64_t prefabNodeGUID = 0;
			if (!TryParseUInt64(prefabNodeGuidText, prefabNodeGUID))
			{
				prefabNodeGUID = serializedGUID;
			}

			serializedGuidsByPrefabNodeGUID[prefabNodeGUID] = serializedGUID;
		}

		return serializedGuidsByPrefabNodeGUID;
	}

	void SetActorNodeValue(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLElement& p_actorNode, const char* p_fieldName, const std::string& p_value)
	{
		auto* field = p_actorNode.FirstChildElement(p_fieldName);

		if (!field)
		{
			field = p_doc.NewElement(p_fieldName);
			p_actorNode.InsertEndChild(field);
		}

		field->SetText(p_value.c_str());
	}

	void RemoveActorComponentsAndBehaviours(OvCore::ECS::Actor& p_actor)
	{
		const auto components = p_actor.GetComponents();

		for (const auto& component : components)
		{
			if (!dynamic_cast<OvCore::ECS::Components::CTransform*>(component.get()))
			{
				p_actor.RemoveComponent(*component);
			}
		}

		const auto behaviourNames = p_actor.GetBehavioursOrder();

		for (const auto& behaviourName : behaviourNames)
		{
			p_actor.RemoveBehaviour(behaviourName);
		}
	}

	void OverwriteActorFromPrefabTemplate(
		OvCore::ECS::Actor& p_targetActor,
		OvCore::ECS::Actor& p_templateActor,
		const std::unordered_map<uint64_t, uint64_t>& p_guidRemap,
		bool p_preserveName,
		bool p_preserveLocalTransform)
	{
		tinyxml2::XMLDocument doc;
		auto* actorsNode = doc.NewElement("actors");
		doc.InsertFirstChild(actorsNode);
		p_templateActor.OnSerialize(doc, actorsNode);

		auto* actorNode = actorsNode->FirstChildElement("actor");
		if (!actorNode)
		{
			return;
		}

		RemapActorReferenceGuidsInActorNode(*actorNode, p_guidRemap);

		SetActorNodeValue(doc, *actorNode, "id", std::to_string(p_targetActor.GetID()));
		SetActorNodeValue(doc, *actorNode, "guid", std::to_string(p_targetActor.GetGUID()));
		SetActorNodeValue(doc, *actorNode, "parent", std::to_string(p_targetActor.GetParentID()));

		const std::string previousName = p_targetActor.GetName();
		const auto previousLocalPosition = p_targetActor.transform.GetLocalPosition();
		const auto previousLocalRotation = p_targetActor.transform.GetLocalRotation();
		const auto previousLocalScale = p_targetActor.transform.GetLocalScale();

		RemoveActorComponentsAndBehaviours(p_targetActor);
		p_targetActor.OnDeserialize(doc, actorNode);

		if (p_preserveName)
		{
			p_targetActor.SetName(previousName);
		}

		if (p_preserveLocalTransform)
		{
			p_targetActor.transform.SetLocalPosition(previousLocalPosition);
			p_targetActor.transform.SetLocalRotation(previousLocalRotation);
			p_targetActor.transform.SetLocalScale(previousLocalScale);
		}
	}

	void RefreshMaterialsUsingShader(
		OvCore::ResourceManagement::MaterialManager& p_materialManager,
		OvRendering::Resources::Shader& p_shader
	)
	{
		for (const auto material : p_materialManager.GetResources() | std::views::values)
		{
			if (material && material->GetShader() == &p_shader)
			{
				material->UpdateProperties();
			}
		}
	}

	void RefreshMaterialEditorIfTargetUsesShader(OvRendering::Resources::Shader& p_shader)
	{
		auto& materialEditor = EDITOR_PANEL(OvEditor::Panels::MaterialEditor, "Material Editor");

		if (auto* targetMaterial = materialEditor.GetTarget(); targetMaterial && targetMaterial->GetShader() == &p_shader)
		{
			materialEditor.Refresh();
		}
	}

	template<typename TResourceManager, typename TAssetNameValidator>
	void MoveEmbeddedResourcesForRenamedModel(
		TResourceManager& p_resourceManager,
		const std::string& p_previousModelPath,
		const std::string& p_newModelPath,
		TAssetNameValidator p_validateAssetName
	)
	{
		std::vector<std::pair<std::string, std::string>> moves;

		for (const auto& [resourcePath, resource] : p_resourceManager.GetResources())
		{
			(void)resource;

			const auto embeddedPath = OvRendering::Resources::Parsers::ParseEmbeddedAssetPath(resourcePath.string());
			if (!embeddedPath || embeddedPath->modelPath != p_previousModelPath)
			{
				continue;
			}

			if (!p_validateAssetName(embeddedPath->assetName))
			{
				continue;
			}

			moves.emplace_back(resourcePath.string(), p_newModelPath + ":" + embeddedPath->assetName);
		}

		for (const auto& [oldPath, newPath] : moves)
		{
			if (!p_resourceManager.MoveResource(oldPath, newPath))
			{
				continue;
			}

			if (auto* resource = p_resourceManager.GetResource(newPath, false))
			{
				const_cast<std::string&>(resource->path) = newPath;
			}
		}
	}

	void MoveAllEmbeddedResourcesForRenamedModel(const std::string& p_previousModelPath, const std::string& p_newModelPath)
	{
		MoveEmbeddedResourcesForRenamedModel(
			OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::MaterialManager>(),
			p_previousModelPath,
			p_newModelPath,
			[](const std::string& p_assetName)
			{
				return OvRendering::Resources::Parsers::ParseEmbeddedMaterialIndex(p_assetName).has_value();
			}
		);

		MoveEmbeddedResourcesForRenamedModel(
			OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::TextureManager>(),
			p_previousModelPath,
			p_newModelPath,
			[](const std::string& p_assetName)
			{
				return OvRendering::Resources::Parsers::ParseEmbeddedTextureIndex(p_assetName).has_value();
			}
		);
	}

}

std::string OvEditor::Core::GetBuildTypeName(OvEditor::Core::EBuildType p_buildType)
{
	switch (p_buildType)
	{
		case OvEditor::Core::EBuildType::Debug: return "Debug";
		case OvEditor::Core::EBuildType::Release: return "Release";
		case OvEditor::Core::EBuildType::Publish: return "Publish";
	}

	OVASSERT(false, "Unknown build type used.");
	return "";
}

OvEditor::Core::EditorActions::EditorActions(Context& p_context, PanelsManager& p_panelsManager) :
	m_context(p_context), 
	m_panelsManager(p_panelsManager)
{
	OvCore::Global::ServiceLocator::Provide<OvEditor::Core::EditorActions>(*this);
    m_historyUnloadListener = m_context.sceneManager.SceneUnloadEvent += [this] { if (!m_restoringHistory) ClearHistory(); };

	OvCore::Helpers::GUIHelpers::SetFileItemBuilder(
		[](OvTools::Utils::PathParser::EFileType p_type, std::function<void(std::string)> p_callback, bool p_searchProject, bool p_searchEngine) {
			OvCore::Helpers::GUIHelpers::PickerItemList items;
			OvEditor::Helpers::PickerHelpers::AddFileItems(items, p_type, std::move(p_callback), p_searchProject, p_searchEngine);
			return items;
		}
	);

	m_context.sceneManager.CurrentSceneSourcePathChangedEvent += [this](const std::string&) { RefreshWindowTitle(); };
}

OvEditor::Core::EditorActions::~EditorActions()
{
    m_context.sceneManager.SceneUnloadEvent -= m_historyUnloadListener;
}

OvEditor::Core::EditorActions::SceneSnapshot OvEditor::Core::EditorActions::CaptureScene() const
{
    SceneSnapshot result;
    auto* scene = m_context.sceneManager.GetCurrentScene();
    if (!scene) return result;
    tinyxml2::XMLDocument doc;
    auto* root = doc.NewElement("root"); doc.InsertEndChild(root);
    auto* sceneNode = doc.NewElement("scene"); root->InsertEndChild(sceneNode);
    auto* actors = doc.NewElement("actors"); sceneNode->InsertEndChild(actors);
    for (auto* actor : scene->GetActors()) if (actor->IsAlive()) actor->OnSerialize(doc, actors);
    tinyxml2::XMLPrinter printer(nullptr, true); doc.Print(&printer);
    result.xml = printer.CStr();
    for (auto* actor : GetSelectedActors()) result.selection.push_back(actor->GetGUID());
    return result;
}

void OvEditor::Core::EditorActions::BeginSceneEdit(const std::string& label)
{
    if (m_editorMode != EEditorMode::EDIT || m_restoringHistory || !m_context.sceneManager.GetCurrentScene()) return;
    if (!m_pendingEdit) m_pendingEdit = HistoryEntry{label, CaptureScene(), {}};
    else if (m_pendingEdit->label == "Edit properties") m_pendingEdit->label = label;
}

void OvEditor::Core::EditorActions::CommitSceneEdit(bool force)
{
    if (!m_pendingEdit || m_restoringHistory) return;
    if (m_editorMode != EEditorMode::EDIT) { m_pendingEdit.reset(); return; }
    if (!force && ImGui::GetCurrentContext() &&
        (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(0) ||
         m_panelsManager.GetPanelAs<Panels::SceneView>("Scene View").IsTransformEditing())) return;
    auto edit = std::move(*m_pendingEdit); m_pendingEdit.reset();
    edit.after = CaptureScene();
    if (edit.before.xml == edit.after.xml) return;
    m_redo.clear();
    edit.revision = ++m_revisionCounter;
    m_undo.push_back(std::move(edit));
    size_t bytes = 0;
    for (const auto& entry : m_undo) bytes += entry.before.xml.size() + entry.after.xml.size();
    // Keep the newest operation even when a single large scene exceeds the budget.
    while (m_undo.size() > 1 && (m_undo.size() > 100 || bytes > 64 * 1024 * 1024))
    {
        bytes -= m_undo.front().before.xml.size() + m_undo.front().after.xml.size();
        m_baseRevision = m_undo.front().revision;
        m_undo.erase(m_undo.begin());
    }
}

void OvEditor::Core::EditorActions::ClearHistory()
{
    m_pendingEdit.reset(); m_undo.clear(); m_redo.clear(); m_transformOriginals.clear();
    // A freshly loaded scene matches its file.
    m_baseRevision = m_savedRevision = ++m_revisionCounter;
}

uint64_t OvEditor::Core::EditorActions::CurrentRevision() const
{
    return m_undo.empty() ? m_baseRevision : m_undo.back().revision;
}

bool OvEditor::Core::EditorActions::IsSceneModified() const
{
    return CurrentRevision() != m_savedRevision;
}

bool OvEditor::Core::EditorActions::ConfirmDiscardSceneChanges()
{
    if (m_editorMode != EEditorMode::EDIT) StopPlaying();
    CommitSceneEdit(true);
    if (!IsSceneModified()) return true;

    using namespace OvWindowing::Dialogs;
    const auto source = m_context.sceneManager.GetCurrentSceneSourcePath();
    const auto sceneName = source.empty() ? std::string{ "Untitled" } : std::filesystem::path{ source }.stem().string();
    MessageBox message(
        "Scene Has Been Modified",
        "Do you want to save the changes you made in the scene \"" + sceneName + "\"?\n\nYour changes will be lost if you don't save them.",
        MessageBox::EMessageType::WARNING,
        MessageBox::EButtonLayout::YES_NO_CANCEL,
        true
    );

    switch (message.GetUserAction())
    {
    case MessageBox::EUserAction::YES: return SaveSceneChanges();
    case MessageBox::EUserAction::NO: return true;
    default: return false;
    }
}

void OvEditor::Core::EditorActions::NewScene()
{
    if (ConfirmDiscardSceneChanges()) LoadEmptyScene();
}

void OvEditor::Core::EditorActions::OpenScene()
{
    OvWindowing::Dialogs::OpenFileDialog dialog("Open Scene");
    dialog.SetInitialDirectory(m_context.projectAssetsPath.string());
    dialog.AddFileType("Overload Scene (.ovscene)", "*.ovscene;");
    dialog.Show();

    if (dialog.HasSucceeded() && ConfirmDiscardSceneChanges())
        LoadSceneFromDisk(dialog.GetSelectedFilePath(), true);
}

void OvEditor::Core::EditorActions::OpenScene(const std::string& p_path)
{
    if (ConfirmDiscardSceneChanges()) LoadSceneFromDisk(p_path);
}

void OvEditor::Core::EditorActions::RefreshWindowTitle()
{
    const auto source = m_context.sceneManager.GetCurrentSceneSourcePath();
    const auto title = m_context.windowSettings.title + " - " + m_context.projectFile.stem().string() + " - " +
        (source.empty() ? std::string{ "Untitled Scene" } : GetResourcePath(source)) + (IsSceneModified() ? "*" : "");

    if (title != m_windowTitle)
    {
        m_windowTitle = title;
        m_context.window->SetTitle(title);
    }
}

bool OvEditor::Core::EditorActions::CanUndo() const { return m_editorMode == EEditorMode::EDIT && !m_undo.empty(); }
bool OvEditor::Core::EditorActions::CanRedo() const { return m_editorMode == EEditorMode::EDIT && !m_redo.empty(); }
std::string OvEditor::Core::EditorActions::UndoLabel() const { return CanUndo() ? m_undo.back().label : ""; }
std::string OvEditor::Core::EditorActions::RedoLabel() const { return CanRedo() ? m_redo.back().label : ""; }

bool OvEditor::Core::EditorActions::RestoreScene(const SceneSnapshot& snapshot)
{
    tinyxml2::XMLDocument doc;
    if (doc.Parse(snapshot.xml.c_str()) != tinyxml2::XML_SUCCESS || !doc.RootElement() || !doc.RootElement()->FirstChildElement("scene")) return false;
    const auto source = m_context.sceneManager.GetCurrentSceneSourcePath();
    auto& inspector = m_panelsManager.GetPanelAs<Panels::Inspector>("Inspector");
    const auto lockedTargets = inspector.GetLockedTargetGUIDs();
    m_restoringHistory = true;
    bool restored = false;
    try
    {
        // Drop all property bindings once before reconstructing a large selection.
        // Otherwise each destructor would rebuild the remaining selection.
        UnselectActor();
        inspector.SetLocked(false);
        restored = m_context.sceneManager.LoadSceneFromMemory(doc);
        if (!source.empty()) m_context.sceneManager.StoreCurrentSceneSourcePath(source);
        if (restored) { RestoreSelection(snapshot.selection); inspector.RestoreLockedTargets(lockedTargets); }
    }
    catch (...) { m_restoringHistory = false; throw; }
    m_restoringHistory = false;
    return restored;
}

bool OvEditor::Core::EditorActions::Undo()
{
    if (m_editorMode != EEditorMode::EDIT) return false;
    CommitSceneEdit(true);
    if (!CanUndo() || !RestoreScene(m_undo.back().before)) return false;
    m_redo.push_back(std::move(m_undo.back())); m_undo.pop_back();
    return true;
}

bool OvEditor::Core::EditorActions::Redo()
{
    if (m_editorMode != EEditorMode::EDIT) return false;
    CommitSceneEdit(true);
    if (!CanRedo() || !RestoreScene(m_redo.back().after)) return false;
    m_undo.push_back(std::move(m_redo.back())); m_redo.pop_back();
    return true;
}

std::vector<OvCore::ECS::Actor*> OvEditor::Core::EditorActions::GetSelectedActors() const
{
    auto targets = m_panelsManager.GetPanelAs<Panels::Inspector>("Inspector").GetSelectedActors();
    std::erase_if(targets, [](auto* actor) { return !actor->IsAlive(); });
    return targets;
}

void OvEditor::Core::EditorActions::SetSelection(const std::vector<OvCore::ECS::Actor*>& actors)
{
    m_panelsManager.GetPanelAs<Panels::Inspector>("Inspector").FocusActors(actors);
}

void OvEditor::Core::EditorActions::RestoreSelection(const std::vector<uint64_t>& selection)
{
    std::vector<OvCore::ECS::Actor*> actors;
    if (auto* scene = m_context.sceneManager.GetCurrentScene())
        for (auto guid : selection) if (auto* actor = scene->FindActorByGUID(guid)) actors.push_back(actor);
    SetSelection(actors);
}

bool OvEditor::Core::EditorActions::IsActorSelected(const OvCore::ECS::Actor& actor) const
{
    const auto actors = GetSelectedActors();
    return std::find(actors.begin(), actors.end(), &actor) != actors.end();
}

void OvEditor::Core::EditorActions::ToggleActorSelection(OvCore::ECS::Actor& actor)
{
    auto actors = GetSelectedActors();
    if (std::find(actors.begin(), actors.end(), &actor) != actors.end()) std::erase(actors, &actor);
    else actors.insert(actors.begin(), &actor);
    SetSelection(actors);
}

void OvEditor::Core::EditorActions::DestroySelectedActors()
{
    const auto actors = GetSelectedActors();
    if (actors.empty()) return;
    BeginSceneEdit("Delete selection");
    for (auto* actor : actors) actor->MarkAsDestroy();
    UnselectActor();
}

void OvEditor::Core::EditorActions::DuplicateSelectedActors()
{
    const auto actors = GetSelectedActors();
    if (actors.empty()) return;
    BeginSceneEdit("Duplicate selection");
    std::vector<OvCore::ECS::Actor*> duplicates;
    for (auto* actor : actors)
    {
        if (std::any_of(actors.begin(), actors.end(), [actor](auto* other) { return actor->IsDescendantOf(other); })) continue;
        DuplicateActor(*actor);
        if (IsAnyActorSelected()) duplicates.push_back(&GetSelectedActor());
    }
    SetSelection(duplicates);
}

void OvEditor::Core::EditorActions::ReparentSelection(OvCore::ECS::Actor& dragged, OvCore::ECS::Actor* parent)
{
    auto actors = IsActorSelected(dragged) ? GetSelectedActors() : std::vector<OvCore::ECS::Actor*>{&dragged};
    for (auto* actor : actors) if (parent && (parent == actor || parent->IsDescendantOf(actor))) return;
    BeginSceneEdit("Reparent selection");
    for (auto* actor : actors)
    {
        if (std::any_of(actors.begin(), actors.end(), [actor](auto* other) { return actor->IsDescendantOf(other); })) continue;
        const auto position = actor->transform.GetWorldPosition();
        const auto rotation = actor->transform.GetWorldRotation();
        const auto scale = actor->transform.GetWorldScale();
        if (parent) actor->SetParent(*parent); else actor->DetachFromParent();
        actor->transform.SetWorldPosition(position);
        actor->transform.SetWorldRotation(rotation);
        actor->transform.SetWorldScale(scale);
    }
}

void OvEditor::Core::EditorActions::BeginTransformEdit()
{
    BeginSceneEdit("Transform selection");
    m_transformOriginals.clear();
    for (auto* actor : GetSelectedActors())
        m_transformOriginals.push_back({actor->GetGUID(), actor->transform.GetLocalPosition(), actor->transform.GetLocalScale(), actor->transform.GetLocalRotation()});
}

void OvEditor::Core::EditorActions::TransformSelection(const std::function<void()>& applyPrimary, std::optional<OvMaths::FVector3> framePivot, OvMaths::FQuaternion basis)
{
    const auto actors = GetSelectedActors();
    if (actors.empty()) return;
    auto& primary = *actors.front();
    const auto primaryPosition = primary.transform.GetWorldPosition();
    const auto pivot = framePivot.value_or(primaryPosition);
    const auto rotation = primary.transform.GetWorldRotation();
    const auto scale = primary.transform.GetWorldScale();
    const auto localPosition = primary.transform.GetLocalPosition();
    const auto localRotation = primary.transform.GetLocalRotation();
    const auto localScale = primary.transform.GetLocalScale();
    applyPrimary();
    const auto rotationDelta = primary.transform.GetWorldRotation() * OvMaths::FQuaternion::Inverse(rotation);
    const auto newScale = primary.transform.GetWorldScale();
    const OvMaths::FVector3 scaleDelta{
        std::abs(scale.x) > 1e-6f ? newScale.x / scale.x : 1.f,
        std::abs(scale.y) > 1e-6f ? newScale.y / scale.y : 1.f,
        std::abs(scale.z) > 1e-6f ? newScale.z / scale.z : 1.f};
    auto scaleOffset = [&](OvMaths::FVector3 offset) {
        offset = OvMaths::FQuaternion::Inverse(basis) * offset;
        return basis * OvMaths::FVector3{offset.x * scaleDelta.x,offset.y * scaleDelta.y,offset.z * scaleDelta.z};
    };
    const auto translation = primary.transform.GetWorldPosition() - (pivot + rotationDelta * scaleOffset(primaryPosition - pivot));
    const bool parentSelected = std::any_of(actors.begin(), actors.end(), [&](auto* actor) { return primary.IsDescendantOf(actor); });
    if (parentSelected)
    {
        // Move the selected ancestor once; the primary child follows naturally.
        primary.transform.SetLocalPosition(localPosition);
        primary.transform.SetLocalRotation(localRotation);
        primary.transform.SetLocalScale(localScale);
    }
    for (auto* actor : actors)
    {
        if (actor == &primary && !parentSelected) continue;
        if (std::any_of(actors.begin(), actors.end(), [actor](auto* other) { return actor->IsDescendantOf(other); })) continue;
        const auto offset = actor->transform.GetWorldPosition() - pivot;
        const auto scaled = scaleOffset(offset);
        const auto oldScale = actor->transform.GetWorldScale();
        actor->transform.SetWorldPosition(pivot + translation + rotationDelta * scaled);
        actor->transform.SetWorldRotation(rotationDelta * actor->transform.GetWorldRotation());
        actor->transform.SetWorldScale({oldScale.x * scaleDelta.x, oldScale.y * scaleDelta.y, oldScale.z * scaleDelta.z});
    }
}

void OvEditor::Core::EditorActions::EndTransformEdit(bool cancel)
{
    if (cancel)
        if (auto* scene = m_context.sceneManager.GetCurrentScene())
            for (const auto& original : m_transformOriginals)
                if (auto* actor = scene->FindActorByGUID(original.guid))
                {
                    actor->transform.SetLocalPosition(original.position);
                    actor->transform.SetLocalRotation(original.rotation);
                    actor->transform.SetLocalScale(original.scale);
                }
    m_transformOriginals.clear();
}

void OvEditor::Core::EditorActions::LoadEmptyScene()
{
	if (GetCurrentEditorMode() != EEditorMode::EDIT)
		StopPlaying();

	m_context.sceneManager.LoadDefaultScene();

	OVLOG_INFO("New scene created");
}

bool OvEditor::Core::EditorActions::SaveSceneToDisk(OvCore::SceneSystem::Scene& p_scene, const std::string& p_path)
{
	tinyxml2::XMLDocument doc;
	tinyxml2::XMLNode* node = doc.NewElement("root");
	doc.InsertFirstChild(node);
	// Other scenes (e.g. a new scene asset) are plain files and must not
	// redirect where the open scene gets saved.
	const bool isCurrentScene = &p_scene == m_context.sceneManager.GetCurrentScene();
	if (isCurrentScene) m_context.sceneManager.StoreCurrentSceneSourcePath(p_path);
	p_scene.OnSerialize(doc, node);

	if (doc.SaveFile(p_path.c_str()) != tinyxml2::XML_SUCCESS)
	{
		OVLOG_ERROR("Failed to save scene to: " + p_path);
		return false;
	}

	if (isCurrentScene) m_savedRevision = CurrentRevision();
	return true;
}

void OvEditor::Core::EditorActions::LoadSceneFromDisk(const std::string& p_path, bool p_absolute)
{
	if (GetCurrentEditorMode() != EEditorMode::EDIT)
	{
		StopPlaying();
	}

	if (!m_context.sceneManager.LoadScene(p_path, p_absolute))
	{
		OVLOG_ERROR("Failed to load scene from disk: " + p_path);
		return;
	}

	OVLOG_INFO("Scene loaded from disk: " + m_context.sceneManager.GetCurrentSceneSourcePath());
	m_panelsManager.GetPanelAs<OvEditor::Panels::SceneView>("Scene View").Focus();
}

bool OvEditor::Core::EditorActions::IsCurrentSceneLoadedFromDisk() const
{
	return m_context.sceneManager.IsCurrentSceneLoadedFromDisk();
}

bool OvEditor::Core::EditorActions::SaveSceneChanges()
{
	if (m_editorMode != EEditorMode::EDIT)
	{
		OVLOG_WARNING("Exit play mode to save the scene.");
		return false;
	}

	CommitSceneEdit(true);

	if (IsCurrentSceneLoadedFromDisk())
	{
		auto currentScene = m_context.sceneManager.GetCurrentScene();
		OVASSERT(currentScene, "Current scene is null");

		const std::string currentScenePath = m_context.sceneManager.GetCurrentSceneSourcePath();
		if (!SaveSceneToDisk(*currentScene, currentScenePath)) return false;
		OVLOG_INFO("Current scene saved to: " + currentScenePath);
		return true;
	}

	return SaveAs();
}

bool OvEditor::Core::EditorActions::SaveAs()
{
	if (m_editorMode != EEditorMode::EDIT)
	{
		OVLOG_WARNING("Exit play mode to save the scene.");
		return false;
	}

	CommitSceneEdit(true);

	OvWindowing::Dialogs::SaveFileDialog dialog("New Scene");
	const auto initialPath = m_context.projectAssetsPath / "New Scene";
	dialog.SetInitialDirectory(initialPath.string());
	dialog.DefineExtension("Overload Scene", ".ovscene");
	dialog.Show();

	if (dialog.HasSucceeded())
	{
		if (dialog.IsFileExisting())
		{
			OvWindowing::Dialogs::MessageBox message("File already exists!", "The file \"" + dialog.GetSelectedFileName() + "\" already exists.\n\nUsing this file as the new home for your scene will erase any content stored in this file.\n\nAre you ok with that?", OvWindowing::Dialogs::MessageBox::EMessageType::WARNING, OvWindowing::Dialogs::MessageBox::EButtonLayout::YES_NO, true);
			switch (message.GetUserAction())
			{
			case OvWindowing::Dialogs::MessageBox::EUserAction::YES: break;
			default: return false;
			}
		}
		
		auto currentScene = m_context.sceneManager.GetCurrentScene();
		if (!SaveSceneToDisk(*currentScene, dialog.GetSelectedFilePath())) return false;
		OVLOG_INFO("Current scene saved to: " + dialog.GetSelectedFilePath());
		return true;
	}

	return false;
}

void OvEditor::Core::EditorActions::RefreshScripts()
{
	m_context.scriptEngine->Reload();
	m_panelsManager.GetPanelAs<Panels::Inspector>("Inspector").Refresh();
	if (m_context.scriptEngine->IsOk())
		OVLOG_INFO("Scripts interpretation succeeded!");
}

std::optional<std::string> OvEditor::Core::EditorActions::SelectBuildFolder()
{
	OvWindowing::Dialogs::SaveFileDialog dialog("Build location");
	dialog.DefineExtension("Game Build", "..");
	dialog.Show();
	if (dialog.HasSucceeded())
	{
		std::string selectedPath = dialog.GetSelectedFilePath();

		if (selectedPath.ends_with(".."))
		{
			selectedPath.erase(selectedPath.size() - 2);
		}

		if (!std::filesystem::exists(selectedPath))
		{
			return selectedPath;
		}
		else
		{
			OvWindowing::Dialogs::MessageBox message(
				"Folder already exists!",
				std::format("The folder \"{}\" already exists.\n\nPlease select another location and try again", selectedPath),
				OvWindowing::Dialogs::MessageBox::EMessageType::WARNING,
				OvWindowing::Dialogs::MessageBox::EButtonLayout::OK,
				true
			);

			return {};
		}
	}
	else
	{
		return {};
	}
}

void OvEditor::Core::EditorActions::Build(bool p_autoRun, bool p_tempFolder)
{
	std::filesystem::path destinationFolder;

	if (p_tempFolder)
	{
		destinationFolder = Utils::FileSystem::kEditorDataPath / "TempBuild";

		try
		{
			std::filesystem::remove_all(destinationFolder);
		}
		catch (std::filesystem::filesystem_error error)
		{
			OvWindowing::Dialogs::MessageBox message(
				"Temporary build failed",
				"The temporary folder is currently being used by another process",
				OvWindowing::Dialogs::MessageBox::EMessageType::ERROR,
				OvWindowing::Dialogs::MessageBox::EButtonLayout::OK,
				true
			);

			return;
		}
	}
	else if (auto res = SelectBuildFolder(); res.has_value())
	{
		destinationFolder = res.value();
	}
	else
	{
		return; // Operation cancelled (No folder selected)
	}

	BuildAtLocation(
		static_cast<EBuildType>(m_context.projectSettings.Get<int>("build_type")),
		destinationFolder,
		p_autoRun
	);
}

void OvEditor::Core::EditorActions::BuildAtLocation(
	EBuildType p_buildType,
	const std::filesystem::path& p_buildPath,
	bool p_autoRun
)
{
	const std::string extension = 
#if defined(_WIN32)
		".exe";
#else
		"";
#endif

	const std::string executableName = m_context.projectSettings.Get<std::string>("executable_name") + extension;

	bool failed = false;

	OVLOG_INFO(std::format("Preparing to build at location: \"{}\"", p_buildPath.string()));

	std::filesystem::remove_all(p_buildPath);

	if (std::filesystem::create_directory(p_buildPath))
	{
		OVLOG_INFO("Build directory created");

		if (std::filesystem::create_directory(p_buildPath / "Data"))
		{
			OVLOG_INFO("Data directory created");

			if (std::filesystem::create_directory(p_buildPath / "Data" / "User"))
			{
				OVLOG_INFO("Data/User directory created");

				std::error_code err;

				std::filesystem::copy(m_context.projectFile, p_buildPath / "Data" / "User" / "Game.ini", err);

				if (!err)
				{
					OVLOG_INFO("Data/User/Game.ini file generated");
		
					std::filesystem::copy(
						m_context.projectAssetsPath,
						p_buildPath / "Data" / "User" / "Assets",
						std::filesystem::copy_options::recursive,
						err
					);

					auto sceneFileName = m_context.projectSettings.Get<std::string>("start_scene");
					sceneFileName = OvTools::Utils::PathParser::MakeNonWindowsStyle(sceneFileName);

					if (!std::filesystem::exists(p_buildPath / "Data" / "User" / "Assets" / sceneFileName))
					{
						OVLOG_ERROR("Failed to find Start Scene at expected path. Verify your Project Setings.");

						OvWindowing::Dialogs::MessageBox message(
							"Build Failure",
							"An error occured during the building of your game.\nCheck the console for more information",
							OvWindowing::Dialogs::MessageBox::EMessageType::ERROR,
							OvWindowing::Dialogs::MessageBox::EButtonLayout::OK,
							true
						);

						std::filesystem::remove_all(p_buildPath);
						return;						
					}

					if (!err)
					{
						OVLOG_INFO("Data/User/Assets/ directory copied");

						std::filesystem::copy(
							m_context.engineAssetsPath,
							p_buildPath / "Data" / "Engine",
							std::filesystem::copy_options::recursive,
							err
						);

						if (!err)
						{
							OVLOG_INFO("Data/Engine/ directory copied");
						}
						else
						{
							OVLOG_ERROR("Data/Engine/ directory failed to copy");
							failed = true;
						}
					}
					else
					{
						OVLOG_ERROR("Data/User/Assets/ directory failed to copy");
						failed = true;
					}
				}
				else
				{
					OVLOG_ERROR("Data/User/Game.ini file failed to generate");
					failed = true;
				}

				const auto builderFolder = std::filesystem::current_path() / "Builder" / GetBuildTypeName(p_buildType);

				const std::string initialExecutableName = "OvGame" + extension;

				if (std::filesystem::exists(builderFolder) && std::filesystem::exists(builderFolder / initialExecutableName))
				{
					std::error_code err;

					std::filesystem::copy(builderFolder, p_buildPath, err);

					if (!err)
					{
						OVLOG_INFO("Builder data (Dlls and executable) copied");
							
						std::filesystem::rename(p_buildPath / initialExecutableName, p_buildPath / executableName, err);

						if (!err)
						{
							OVLOG_INFO("Game executable renamed to " + executableName);

#if defined(_WIN32)
							if (const std::string windowIconPath = m_context.projectSettings.GetOrDefault("window_icon", std::string{}); !windowIconPath.empty())
							{
								const std::filesystem::path windowIconRealPath{ GetRealPath(windowIconPath) };

								if (!std::filesystem::exists(windowIconRealPath))
								{
									OVLOG_WARNING(
										std::format(
											"Window icon \"{}\" was not found. Keeping default executable icon.",
											windowIconRealPath.string()
										)
									);
								}
								else if (OvRendering::Data::Image iconImage{ windowIconRealPath }; !iconImage)
								{
									OVLOG_WARNING(
										std::format(
											"Failed to load window icon \"{}\". Keeping default executable icon.",
											windowIconRealPath.string()
										)
									);
								}
								else if (iconImage.isHDR)
								{
									OVLOG_WARNING(
										std::format(
											"Window icon \"{}\" is HDR and cannot be used for executable icons. Keeping default executable icon.",
											windowIconRealPath.string()
										)
									);
								}
								else
								{
									const size_t iconByteCount = static_cast<size_t>(iconImage.width) * static_cast<size_t>(iconImage.height) * 4u;
									const auto iconBytes = std::span<const uint8_t>{ static_cast<const uint8_t*>(iconImage.data), iconByteCount };
									if (!OvTools::Utils::SystemCalls::SetExecutableIcon(
										p_buildPath / executableName,
										iconBytes,
										static_cast<uint32_t>(iconImage.width),
										static_cast<uint32_t>(iconImage.height)
									))
									{
										OVLOG_WARNING(
											std::format(
												"Failed to apply executable icon from \"{}\". Keeping default executable icon.",
												windowIconRealPath.string()
											)
										);
									}
								}
							}
#endif

							if (p_autoRun)
							{
								const auto exePath = p_buildPath / executableName;
								OVLOG_INFO(std::format("Launching the game at location: \"{}\"", exePath.string()));

								if (std::filesystem::exists(exePath))
								{
									OvTools::Utils::SystemCalls::RunProgram(exePath.string(), p_buildPath.string());
								}
								else
								{
									OVLOG_ERROR("Failed to start the game: Executable not found");
									failed = true;
								}
							}
						}
						else
						{
							OVLOG_ERROR("Game executable failed to rename");
							failed = true;
						}
					}
					else
					{
						OVLOG_ERROR("Builder data (Dlls and executatble) failed to copy");
						failed = true;
					}
				}
				else
				{
					OVLOG_ERROR(std::format(
						"OvGame executable not found for \"{}\" configuration. Build OvGame and OvEditor in that configuration first, then try again.",
						GetBuildTypeName(p_buildType)
					));
					failed = true;
				}
			}
		}
	}
	else
	{
		OVLOG_ERROR("Build directory failed to create");
		failed = true;
	}

	if (failed)
	{
		std::filesystem::remove_all(p_buildPath);
		OvWindowing::Dialogs::MessageBox message(
			"Build Failure",
			"An error occured during the building of your game.\nCheck the console for more information",
			OvWindowing::Dialogs::MessageBox::EMessageType::ERROR,
			OvWindowing::Dialogs::MessageBox::EButtonLayout::OK,
			true
		);
	}
}

void OvEditor::Core::EditorActions::OpenProfiler()
{
	const auto profilerPath = std::filesystem::current_path() / "Tools" / "tracy-profiler.exe";
	OvTools::Utils::SystemCalls::OpenFile(profilerPath.string());
}

void OvEditor::Core::EditorActions::DelayAction(std::function<void()> p_action, uint32_t p_frames)
{
	m_delayedActions.emplace_back(p_frames + 1, p_action);
}

void OvEditor::Core::EditorActions::ExecuteDelayedActions()
{
    std::vector<std::function<void()>> ready;
    for (auto& [frames, action] : m_delayedActions) if (--frames == 0) ready.push_back(std::move(action));
    std::erase_if(m_delayedActions, [](const auto& entry) { return entry.first == 0; });
    // A callback can enqueue Inspector rebuilds. Do not mutate the queue while
    // iterating it, and keep newly queued work for the next frame.
    for (auto& action : ready) action();
    CommitSceneEdit();
}

OvEditor::Core::Context& OvEditor::Core::EditorActions::GetContext()
{
	return m_context;
}

OvEditor::Core::PanelsManager& OvEditor::Core::EditorActions::GetPanelsManager()
{
	return m_panelsManager;
}

void OvEditor::Core::EditorActions::SetActorSpawnAtOrigin(bool p_value)
{
	if (p_value)
		m_actorSpawnMode = EActorSpawnMode::ORIGIN;
	else
		m_actorSpawnMode = EActorSpawnMode::FRONT;
}

void OvEditor::Core::EditorActions::SetActorSpawnMode(EActorSpawnMode p_value)
{
	m_actorSpawnMode = p_value;
}

void OvEditor::Core::EditorActions::ResetLayout()
{
	ApplyLayout(EEditorLayout::DEFAULT);
}

void OvEditor::Core::EditorActions::SetSceneViewCameraSpeed(int p_speed)
{
	EDITOR_PANEL(Panels::SceneView, "Scene View").GetCameraController().SetSpeed((float)p_speed);
}

int OvEditor::Core::EditorActions::GetSceneViewCameraSpeed()
{
	return (int)EDITOR_PANEL(Panels::SceneView, "Scene View").GetCameraController().GetSpeed();
}

void OvEditor::Core::EditorActions::SetAssetViewCameraSpeed(int p_speed)
{
	EDITOR_PANEL(Panels::AssetView, "Asset View").GetCameraController().SetSpeed((float)p_speed);
}

int OvEditor::Core::EditorActions::GetAssetViewCameraSpeed()
{
	return (int)EDITOR_PANEL(Panels::AssetView, "Asset View").GetCameraController().GetSpeed();
}

void OvEditor::Core::EditorActions::ResetSceneViewCameraPosition()
{
	EDITOR_PANEL(Panels::SceneView, "Scene View").ResetCameraTransform();
}

void OvEditor::Core::EditorActions::ResetAssetViewCameraPosition()
{
	EDITOR_PANEL(Panels::AssetView, "Asset View").ResetCameraTransform();
}

OvEditor::Core::EditorActions::EEditorMode OvEditor::Core::EditorActions::GetCurrentEditorMode() const
{
	return m_editorMode;
}

void OvEditor::Core::EditorActions::SetEditorMode(EEditorMode p_newEditorMode)
{
	m_editorMode = p_newEditorMode;
	UpdatePlayModeTint();
	EditorModeChangedEvent.Invoke(m_editorMode);
}

void OvEditor::Core::EditorActions::UpdatePlayModeTint()
{
	using Settings::EditorSettings;

	const bool tinted = m_editorMode != EEditorMode::EDIT && EditorSettings::PlayModeTintEnabled.Get();
	auto& style = OvUI::Styling::Style::Get();
	style.backgroundTint = tinted
		? OvUI::Types::Color{ EditorSettings::PlayModeTintR.Get(), EditorSettings::PlayModeTintG.Get(), EditorSettings::PlayModeTintB.Get(), 1.0f }
		: OvUI::Types::Color{ 1.0f, 1.0f, 1.0f, 1.0f };

	if (ImGui::GetCurrentContext())
		style.ApplyColorsToImGui();
}

void OvEditor::Core::EditorActions::StartPlaying()
{
	if (m_editorMode == EEditorMode::EDIT)
	{
        CommitSceneEdit(true);
        m_playSelection.clear();
        for (auto* actor : GetSelectedActors()) m_playSelection.push_back(actor->GetGUID());
		m_context.scriptEngine->Reload();
		EDITOR_PANEL(Panels::Inspector, "Inspector").Refresh();

		if (m_context.scriptEngine->IsOk())
		{
			PlayEvent.Invoke();
			m_sceneBackup = std::make_unique<tinyxml2::XMLDocument>();
			tinyxml2::XMLNode* node = m_sceneBackup->NewElement("root");
			m_sceneBackup->InsertFirstChild(node);
			m_context.sceneManager.GetCurrentScene()->OnSerialize(*m_sceneBackup, node);
			m_panelsManager.GetPanelAs<OvEditor::Panels::GameView>("Game View").Focus();
			m_context.sceneManager.GetCurrentScene()->Play();
			SetEditorMode(EEditorMode::PLAY);
		}
	}
	else
	{
		m_context.audioEngine->Unsuspend();
		SetEditorMode(EEditorMode::PLAY);
	}
}

void OvEditor::Core::EditorActions::PauseGame()
{
	SetEditorMode(EEditorMode::PAUSE);
	m_context.audioEngine->Suspend();
}

void OvEditor::Core::EditorActions::StopPlaying()
{
	if (m_editorMode != EEditorMode::EDIT)
	{
		m_context.window->SetCursorMode(OvWindowing::Cursor::ECursorMode::NORMAL);
		SetEditorMode(EEditorMode::EDIT);
		bool loadedFromDisk = m_context.sceneManager.IsCurrentSceneLoadedFromDisk();
		std::string sceneSourcePath = m_context.sceneManager.GetCurrentSceneSourcePath();
        auto& inspector = m_panelsManager.GetPanelAs<Panels::Inspector>("Inspector");
        const auto lockedTargets = inspector.GetLockedTargetGUIDs();


		OVASSERT(m_sceneBackup.operator bool(), "No scene backup to restore");

		m_restoringHistory = true;
        try { UnselectActor(); inspector.SetLocked(false); m_context.sceneManager.LoadSceneFromMemory(*m_sceneBackup); }
        catch (...) { m_restoringHistory = false; throw; }
        m_restoringHistory = false;
		if (loadedFromDisk)
			m_context.sceneManager.StoreCurrentSceneSourcePath(sceneSourcePath); // To bo able to save or reload the scene whereas the scene is loaded from memory (Supposed to have no path)
		m_sceneBackup.reset();
		EDITOR_PANEL(Panels::SceneView, "Scene View").Focus();
        RestoreSelection(m_playSelection);
        inspector.RestoreLockedTargets(lockedTargets);
        m_playSelection.clear();
	}
}

void OvEditor::Core::EditorActions::NextFrame()
{
	if (m_editorMode == EEditorMode::PLAY || m_editorMode == EEditorMode::PAUSE)
		SetEditorMode(EEditorMode::FRAME_BY_FRAME);
}

void OvEditor::Core::EditorActions::TogglePlay()
{
	if (m_editorMode == EEditorMode::EDIT) StartPlaying();
	else StopPlaying();
}

void OvEditor::Core::EditorActions::TogglePause()
{
	if (m_editorMode == EEditorMode::PLAY || m_editorMode == EEditorMode::FRAME_BY_FRAME) PauseGame();
	else if (m_editorMode == EEditorMode::PAUSE) StartPlaying();
}

void OvEditor::Core::EditorActions::SetGizmoOperation(OvEditor::Core::EGizmoOperation p_operation)
{
	auto& sceneView = m_panelsManager.GetPanelAs<OvEditor::Panels::SceneView>("Scene View");
	sceneView.SetGizmoOperation(p_operation);
}

OvEditor::Core::EGizmoOperation OvEditor::Core::EditorActions::GetGizmoOperation() const
{
	auto& sceneView = m_panelsManager.GetPanelAs<OvEditor::Panels::SceneView>("Scene View");
	return sceneView.GetGizmoOperation();
}

OvMaths::FVector3 OvEditor::Core::EditorActions::CalculateActorSpawnPoint(float p_distanceToCamera)
{
	auto& sceneView = m_panelsManager.GetPanelAs<OvEditor::Panels::SceneView>("Scene View");

	if (auto camera = sceneView.GetCamera())
	{
		return camera->GetPosition() + camera->transform->GetWorldForward() * p_distanceToCamera;
	}

	return OvMaths::FVector3::Zero;
}

OvCore::ECS::Actor & OvEditor::Core::EditorActions::CreateEmptyActor(bool p_focusOnCreation, OvCore::ECS::Actor* p_parent, const std::string& p_name)
{
    BeginSceneEdit("Create entity");
    const auto currentScene = m_context.sceneManager.GetCurrentScene();
	auto& instance = p_name.empty() ? currentScene->CreateActor() : currentScene->CreateActor(p_name);

	if (p_parent)
		instance.SetParent(*p_parent);

	if (m_actorSpawnMode == EActorSpawnMode::FRONT)
		instance.transform.SetLocalPosition(CalculateActorSpawnPoint(10.0f));

	if (p_focusOnCreation)
		SelectActor(instance);

	return instance;
}

OvCore::ECS::Actor & OvEditor::Core::EditorActions::CreateActorWithModel(const std::string& p_path, bool p_focusOnCreation, OvCore::ECS::Actor* p_parent, const std::string& p_name)
{
	const std::string actorName = p_name.empty() ? std::filesystem::path(p_path).stem().string() : p_name;
	auto& instance = CreateEmptyActor(false, p_parent, actorName);

	auto& modelRenderer = instance.AddComponent<OvCore::ECS::Components::CModelRenderer>();

	const auto model = m_context.modelManager[p_path];
	if (model)
	{
		modelRenderer.SetModel(model);
	}

	auto& materialRenderer = instance.AddComponent<OvCore::ECS::Components::CMaterialRenderer>();
	const auto defaultMaterial = m_context.materialManager[std::string{ kDefaultMaterialPath }];
	if (model)
	{
		materialRenderer.FillWithEmbeddedMaterials(true, defaultMaterial);
	}
	else if (defaultMaterial)
	{
		materialRenderer.FillWithMaterial(*defaultMaterial);
	}

	if (p_focusOnCreation)
		SelectActor(instance);

	return instance;
}

bool OvEditor::Core::EditorActions::DestroyActor(OvCore::ECS::Actor & p_actor)
{
    BeginSceneEdit("Delete entity");
	p_actor.MarkAsDestroy();
	return true;
}

std::string FindDuplicatedActorUniqueName(OvCore::ECS::Actor& p_duplicated, OvCore::ECS::Actor& p_newActor, OvCore::SceneSystem::Scene& p_scene)
{
    const auto parent = p_newActor.GetParent();
    const auto adjacentActors = parent ? parent->GetChildren() : p_scene.GetActors();

    auto availabilityChecker = [&parent, &adjacentActors](std::string target) -> bool
    {
        const auto isActorNameTaken = [&target, parent](auto actor) { return (parent || !actor->GetParent()) && actor->GetName() == target; };
        return std::find_if(adjacentActors.begin(), adjacentActors.end(), isActorNameTaken) == adjacentActors.end();
    };

    return OvTools::Utils::String::GenerateUnique(p_duplicated.GetName(), availabilityChecker);
}

void OvEditor::Core::EditorActions::DuplicateActor
(
	OvCore::ECS::Actor& p_toDuplicate,
	OvCore::ECS::Actor* p_forcedParent,
	bool p_focus,
	bool p_keepSourceParentIfNoForcedParent
)
{
	tinyxml2::XMLDocument doc;
	tinyxml2::XMLNode* actorsRoot = doc.NewElement("actors");
	p_toDuplicate.OnSerialize(doc, actorsRoot);
	auto& newActor = CreateEmptyActor(false);
	int64_t idToUse = newActor.GetID();
	uint64_t guidToUse = newActor.GetGUID();
	tinyxml2::XMLElement* currentActor = actorsRoot->FirstChildElement("actor");
	newActor.OnDeserialize(doc, currentActor);
	
	newActor.SetID(idToUse);
	newActor.SetGUID(guidToUse);
	auto currentScene = m_context.sceneManager.GetCurrentScene();

	if (p_forcedParent)
	{
		newActor.SetParent(*p_forcedParent);
	}
	else if (p_keepSourceParentIfNoForcedParent && newActor.GetParentID() > 0)
	{
		if (auto found = currentScene->FindActorByID(newActor.GetParentID()); found)
		{
			newActor.SetParent(*found);
		}
	}

	if (p_focus || !p_forcedParent)
	{
		const auto uniqueName = FindDuplicatedActorUniqueName(p_toDuplicate, newActor, *currentScene);
		newActor.SetName(uniqueName);
	}

	if (p_focus)
	{
		SelectActor(newActor);
	}

	for (auto& child : p_toDuplicate.GetChildren())
		DuplicateActor(*child, &newActor, false);
}

void OvEditor::Core::EditorActions::SaveActorAsPrefab(OvCore::ECS::Actor& p_actor, const std::string& p_path)
{
	if (!OvCore::SceneSystem::PrefabOperations::SaveToFile(p_actor, p_path))
	{
		OVLOG_ERROR("Failed to save prefab to: " + p_path);
		return;
	}

	const std::string prefabSourcePath = GetResourcePath(p_path);
	OvCore::SceneSystem::PrefabOperations::SetRootPrefabSourceAndNormalizeChildren(p_actor, prefabSourcePath);
	m_panelsManager.GetPanelAs<Panels::Inspector>("Inspector").Refresh();

	OVLOG_INFO("Prefab saved to: " + p_path);
}

OvCore::ECS::Actor* OvEditor::Core::EditorActions::InstantiatePrefab(const std::string& p_path, OvCore::ECS::Actor* p_parent)
{
    BeginSceneEdit("Instantiate prefab");
	auto* currentScene = m_context.sceneManager.GetCurrentScene();
	if (!currentScene)
		return nullptr;

	const std::filesystem::path realPath = GetRealPath(p_path);
	const bool isEnginePrefab = !p_path.empty() && p_path.front() == ':';
	const std::string resourcePath = GetResourcePath(realPath.string(), isEnginePrefab);

	if (p_parent)
	{
		return currentScene->InstantiatePrefab(resourcePath, *p_parent);
	}
	else
	{
		return currentScene->InstantiatePrefab(resourcePath);
	}
}

bool OvEditor::Core::EditorActions::ApplyActorToPrefab(OvCore::ECS::Actor& p_actor)
{
	auto* prefabInstanceRoot = ResolvePrefabInstanceRoot(p_actor);
	if (!prefabInstanceRoot)
	{
		OVLOG_ERROR("Cannot apply actor \"" + p_actor.GetName() + "\" to prefab: no source instance.");
		return false;
	}

	const std::string realPath = GetRealPath(prefabInstanceRoot->GetPrefabSource());

	if (!OvCore::SceneSystem::PrefabOperations::SaveToFile(*prefabInstanceRoot, realPath))
	{
		OVLOG_ERROR("Failed to apply actor \"" + p_actor.GetName() + "\" to prefab: " + realPath);
		return false;
	}

	return true;
}

bool OvEditor::Core::EditorActions::RevertActorToPrefab(OvCore::ECS::Actor& p_actor)
{
    BeginSceneEdit("Revert prefab");
	auto* prefabInstanceRoot = ResolvePrefabInstanceRoot(p_actor);
	if (!prefabInstanceRoot)
	{
		OVLOG_ERROR("Cannot revert actor \"" + p_actor.GetName() + "\" to prefab: no source instance.");
		return false;
	}

	const auto prefabSourcePath = prefabInstanceRoot->GetPrefabSource();
	const auto serializedGuidsByPrefabNodeGUID =
		GatherSerializedGuidsByPrefabNodeGUID(GetRealPath(prefabSourcePath));

	auto* prefabTemplateRoot = InstantiatePrefab(prefabSourcePath);
	if (!prefabTemplateRoot)
	{
		return false;
	}

	std::vector<ActorHierarchyEntry> templateHierarchy;
	CollectActorHierarchy(*prefabTemplateRoot, nullptr, templateHierarchy);

	std::vector<ActorHierarchyEntry> targetHierarchy;
	CollectActorHierarchy(*prefabInstanceRoot, nullptr, targetHierarchy);

	std::unordered_map<uint64_t, std::vector<OvCore::ECS::Actor*>> targetActorsByPrefabNodeGUID;
	for (const auto& entry : targetHierarchy)
	{
		const uint64_t prefabNodeGUID = GetPrefabNodeGUIDOrFallback(*entry.actor);
		targetActorsByPrefabNodeGUID[prefabNodeGUID].push_back(entry.actor);
	}

	std::unordered_map<OvCore::ECS::Actor*, OvCore::ECS::Actor*> templateToTarget;
	std::unordered_set<OvCore::ECS::Actor*> usedTargetActors;

	const uint64_t templateRootPrefabNodeGUID = GetPrefabNodeGUIDOrFallback(*prefabTemplateRoot);
	prefabInstanceRoot->SetPrefabNodeGUID(templateRootPrefabNodeGUID);
	templateToTarget[prefabTemplateRoot] = prefabInstanceRoot;
	usedTargetActors.insert(prefabInstanceRoot);

	for (size_t index = 1; index < templateHierarchy.size(); ++index)
	{
		auto* templateActor = templateHierarchy[index].actor;
		const uint64_t prefabNodeGUID = GetPrefabNodeGUIDOrFallback(*templateActor);

		OvCore::ECS::Actor* targetActor = FindUnusedActorWithPrefabNodeGUID(
			prefabNodeGUID,
			targetActorsByPrefabNodeGUID,
			usedTargetActors
		);

		if (!targetActor)
		{
			targetActor = &CreateEmptyActor(false);
		}

		targetActor->SetPrefabNodeGUID(prefabNodeGUID);
		templateToTarget[templateActor] = targetActor;
		usedTargetActors.insert(targetActor);
	}

	std::unordered_map<uint64_t, uint64_t> templateActorGuidToInstanceGuidMap;
	for (const auto& [templateActor, targetActor] : templateToTarget)
	{
		const uint64_t prefabNodeGUID = templateActor->GetPrefabNodeGUID();
		const uint64_t targetGUID = targetActor->GetGUID();
		templateActorGuidToInstanceGuidMap[prefabNodeGUID] = targetGUID;

		if (const auto it = serializedGuidsByPrefabNodeGUID.find(prefabNodeGUID); it != serializedGuidsByPrefabNodeGUID.end())
		{
			templateActorGuidToInstanceGuidMap[it->second] = targetGUID;
		}
	}

	for (const auto& entry : templateHierarchy)
	{
		auto* templateActor = entry.actor;
		auto* targetActor = templateToTarget[templateActor];
		const bool isRoot = templateActor == prefabTemplateRoot;

		OverwriteActorFromPrefabTemplate(
			*targetActor,
			*templateActor,
			templateActorGuidToInstanceGuidMap,
			isRoot, /* keep root name */
			isRoot  /* keep root local transform */
		);
	}

	for (size_t index = 1; index < templateHierarchy.size(); ++index)
	{
		auto* templateActor = templateHierarchy[index].actor;
		auto* templateParent = templateHierarchy[index].parent;
		auto* targetActor = templateToTarget[templateActor];
		auto* expectedParent = templateToTarget[templateParent];

		if (targetActor->GetParent() != expectedParent)
		{
			targetActor->SetParent(*expectedParent);
		}
	}

	for (const auto& entry : targetHierarchy)
	{
		auto* targetActor = entry.actor;
		if (targetActor == prefabInstanceRoot)
		{
			continue;
		}

		if (!usedTargetActors.contains(targetActor))
		{
			targetActor->MarkAsDestroy();
		}
	}

	prefabTemplateRoot->MarkAsDestroy();
	SelectActor(*prefabInstanceRoot);
	m_panelsManager.GetPanelAs<Panels::Inspector>("Inspector").Refresh();

	return true;
}

void OvEditor::Core::EditorActions::CopyActor(OvCore::ECS::Actor& p_actor)
{
	m_context.copyBuffer = Context::ActorCopyBuffer{
		.guid = p_actor.GetGUID()
	};
}

void OvEditor::Core::EditorActions::PasteActor(OvCore::ECS::Actor* p_parent)
{
	const auto actorCopyBuffer = std::get_if<Context::ActorCopyBuffer>(&m_context.copyBuffer);
	if (!actorCopyBuffer)
	{
		return;
	}

	const auto currentScene = m_context.sceneManager.GetCurrentScene();
	if (!currentScene)
	{
		return;
	}

	if (const auto copiedActor = currentScene->FindActorByGUID(actorCopyBuffer->guid))
	{
		auto* destinationParent = p_parent;

		// Pasted actors are always inserted next to the target actor (same parent),
		// never as children.
		if (destinationParent)
		{
			destinationParent = destinationParent->GetParent();
		}

		DuplicateActor(*copiedActor, destinationParent, true, false);
	}
}

void OvEditor::Core::EditorActions::SelectActor(OvCore::ECS::Actor& p_target)
{
	EDITOR_PANEL(Panels::Inspector, "Inspector").FocusActor(p_target);
}

void OvEditor::Core::EditorActions::UnselectActor()
{
	EDITOR_PANEL(Panels::Inspector, "Inspector").UnFocus();
}

bool OvEditor::Core::EditorActions::IsAnyActorSelected() const
{
    return !GetSelectedActors().empty();
}

OvCore::ECS::Actor& OvEditor::Core::EditorActions::GetSelectedActor() const
{
    return *GetSelectedActors().at(0);
}

void OvEditor::Core::EditorActions::MoveToTarget(OvCore::ECS::Actor& p_target)
{
	EDITOR_PANEL(Panels::SceneView, "Scene View").GetCameraController().MoveToTarget(p_target);
}

void OvEditor::Core::EditorActions::CompileShaders()
{
	for (const auto shader : m_context.shaderManager.GetResources() | std::views::values)
	{
		if (shader)
		{
			CompileShader(shader->path);
		}
	}
}

void OvEditor::Core::EditorActions::CompileShader(OvRendering::Resources::Shader& p_shader)
{
	CompileShader(p_shader.path);
}

void OvEditor::Core::EditorActions::CompileShader(const std::filesystem::path& p_shaderPath)
{
	using namespace OvRendering::Resources::Loaders;

	const auto previousLoggingSettings = ShaderLoader::GetLoggingSettings();
	auto newLoggingSettings = previousLoggingSettings;
	newLoggingSettings.summary = true; // Force enable summary logging
	ShaderLoader::SetLoggingSettings(newLoggingSettings);
	OvRendering::Resources::Shader* compiledShader = nullptr;

	if (m_context.shaderManager.IsResourceRegistered(p_shaderPath))
	{
		compiledShader = m_context.shaderManager[p_shaderPath];

		if (compiledShader)
		{
			m_context.shaderManager.ReloadResource(compiledShader, GetRealPath(compiledShader->path));
		}
	}
	else
	{
		compiledShader = m_context.shaderManager.LoadResource(p_shaderPath);
	}

	ShaderLoader::SetLoggingSettings(previousLoggingSettings);

	if (compiledShader)
	{
		RefreshMaterialsUsingShader(m_context.materialManager, *compiledShader);
		RefreshMaterialEditorIfTargetUsesShader(*compiledShader);
	}
}

void OvEditor::Core::EditorActions::SaveMaterials()
{
	for (const auto material : m_context.materialManager.GetResources() | std::views::values)
	{
		if (!material)
		{
			continue;
		}

		if (OvRendering::Resources::Parsers::ParseEmbeddedAssetPath(material->path))
		{
			continue;
		}

		OvCore::Resources::Loaders::MaterialLoader::Save(*material, GetRealPath(material->path));
	}
}

void OvEditor::Core::EditorActions::RegenerateScriptingProjectFiles()
{
	if (m_context.scriptEngine->CreateProjectFiles(m_context.projectFolder, true))
	{
		OVLOG_INFO("Lua symbol regenerated (.luarc.json created)");
	}
	else
	{
		OVLOG_ERROR("Failed to regenerate lua symbols (.luarc.json failed to create)");
	}
}

bool OvEditor::Core::EditorActions::OpenInCodeEditor(const std::filesystem::path& p_path, OvTools::Utils::OptRef<const std::filesystem::path> p_workdir)
{
	std::string command = OvEditor::Settings::EditorSettings::CodeEditorCommand.Get();
	if (!command.empty())
	{
		auto preferredPath = p_path;
		preferredPath.make_preferred();

		if (command.find("{path}") == std::string::npos)
		{
			OVLOG_ERROR("Failed to open in code editor, missing {path} in custom command.");
			return false;
		}

		auto preferredWorkdir = p_workdir ? p_workdir.value() : m_context.projectFolder;
		preferredWorkdir.make_preferred();

		const std::string quotedWorkdir = std::format("\"{}\"", preferredWorkdir.string());
		const std::string quotedPath = std::format("\"{}\"", preferredPath.string());

		OvTools::Utils::String::ReplaceAll(command, "\"{workdir}\"", "{workdir}");
		OvTools::Utils::String::ReplaceAll(command, "\"{path}\"", "{path}");
		OvTools::Utils::String::ReplaceAll(command, "{workdir}", quotedWorkdir);
		OvTools::Utils::String::ReplaceAll(command, "{path}", quotedPath);
		if (!OvTools::Utils::SystemCalls::ExecuteCommand(command))
		{
			OVLOG_ERROR(std::format("Failed to open in code editor using command: \"{}\"", command));
			return false;
		}
	}
	else
	{
		OVLOG_ERROR("No command provided to open in code editor.");
		return false;
	}

	return true;
}

bool OvEditor::Core::EditorActions::ImportAsset(const std::string& p_initialDestinationDirectory)
{
	using namespace OvWindowing::Dialogs;

	std::string modelFormats = "*.fbx;*.obj;";
	std::string textureFormats = "*.png;*.jpeg;*.jpg;*.tga;*.hdr;";
	std::string shaderFormats = "*.ovfx;";
	std::string shaderPartFormats = "*.ovfxh;";
	std::string soundFormats = "*.mp3;*.ogg;*.wav;";
	std::string scriptFormats = "*.lua;*.cs;";

	OpenFileDialog selectAssetDialog("Select an asset to import");
	selectAssetDialog.AddFileType("Any supported format", modelFormats + textureFormats + shaderFormats + shaderPartFormats + soundFormats + scriptFormats);
	selectAssetDialog.AddFileType("Model (.fbx, .obj)", modelFormats);
	selectAssetDialog.AddFileType("Texture (.png, .jpeg, .jpg, .tga, .hdr)", textureFormats);
	selectAssetDialog.AddFileType("Shader (.ovfx)", shaderFormats);
	selectAssetDialog.AddFileType("Shader Parts (.ovfxh)", shaderPartFormats);
	selectAssetDialog.AddFileType("Sound (.mp3, .ogg, .wav)", soundFormats);
	selectAssetDialog.AddFileType("Script (.lua, .cs)", scriptFormats);
	selectAssetDialog.Show();

	if (selectAssetDialog.HasSucceeded())
	{
		std::string source = selectAssetDialog.GetSelectedFilePath();
		std::string extension = '.' + OvTools::Utils::PathParser::GetExtension(source);
		std::string filename = selectAssetDialog.GetSelectedFileName();

		SaveFileDialog saveLocationDialog("Where to import?");
		saveLocationDialog.SetInitialDirectory(p_initialDestinationDirectory);
		saveLocationDialog.SetInitialFilename(filename);
		saveLocationDialog.DefineExtension(extension, extension);
		saveLocationDialog.Show();

		if (saveLocationDialog.HasSucceeded())
		{
			std::string destination = saveLocationDialog.GetSelectedFilePath();

			if (!std::filesystem::exists(destination) || MessageBox("File already exists", "The destination you have selected already exists, importing this file will erase the previous file content, are you sure about that?", MessageBox::EMessageType::WARNING, MessageBox::EButtonLayout::OK_CANCEL).GetUserAction() == MessageBox::EUserAction::OK)
			{
				std::filesystem::copy(source, destination, std::filesystem::copy_options::overwrite_existing);
				OVLOG_INFO("Asset \"" + destination + "\" imported");
				return true;
			}
		}
	}

	return false;
}

std::string OvEditor::Core::EditorActions::CreateScript(const std::string& p_initialDirectory, const std::string& p_initialName, bool p_csharp)
{
	using namespace OvWindowing::Dialogs;

	const std::string extension = p_csharp ? ".cs" : m_context.scriptEngine->GetDefaultExtension();

	SaveFileDialog dialog("Create Script");
	dialog.SetInitialDirectory(p_initialDirectory);
	dialog.SetInitialFilename(p_initialName);
	dialog.DefineExtension("Script", extension);
	dialog.Show(
		EExplorerFlags::DONTADDTORECENT |
		EExplorerFlags::PATHMUSTEXIST   |
		EExplorerFlags::HIDEREADONLY    |
		EExplorerFlags::NOCHANGEDIR
	);

	if (!dialog.HasSucceeded())
		return {};

	const std::string destination = dialog.GetSelectedFilePath();

    if (p_csharp)
    {
        if (!m_context.csharpProject->CreateScript(destination)) return {};
        EDITOR_PANEL(Panels::AssetBrowser, "Asset Browser").Refresh();
        return destination;
    }

	if (!std::filesystem::exists(destination))
	{
		const std::string scriptName = std::filesystem::path{ destination }.stem().string();
		const std::string fileContent = m_context.scriptEngine->GetDefaultScriptContent(scriptName);

		std::ofstream outfile(destination);
		outfile << fileContent << std::endl;

		EDITOR_PANEL(Panels::AssetBrowser, "Asset Browser").Refresh();
	}

	return destination;
}

bool OvEditor::Core::EditorActions::ImportAssetAtLocation(const std::string& p_destination)
{
	using namespace OvWindowing::Dialogs;

	std::string modelFormats = "*.fbx;*.obj;";
	std::string textureFormats = "*.png;*.jpeg;*.jpg;*.tga;*.hdr;";
	std::string shaderFormats = "*.ovfx;";
	std::string shaderPartFormats = "*.ovfxh;";
	std::string soundFormats = "*.mp3;*.ogg;*.wav;";
	std::string scriptFormats = "*.lua;*.cs;";

	OpenFileDialog selectAssetDialog("Select an asset to import");
	selectAssetDialog.AddFileType("Any supported format", modelFormats + textureFormats + shaderFormats + soundFormats + scriptFormats);
	selectAssetDialog.AddFileType("Model (.fbx, .obj)", modelFormats);
	selectAssetDialog.AddFileType("Texture (.png, .jpeg, .jpg, .tga, .hdr)", textureFormats);
	selectAssetDialog.AddFileType("Shader (.ovfx)", shaderFormats);
	selectAssetDialog.AddFileType("Shader Parts (.ovfxh)", shaderPartFormats);
	selectAssetDialog.AddFileType("Sound (.mp3, .ogg, .wav)", soundFormats);
	selectAssetDialog.AddFileType("Script (.lua, .cs)", scriptFormats);
	selectAssetDialog.Show();

	if (selectAssetDialog.HasSucceeded())
	{
		std::string source = selectAssetDialog.GetSelectedFilePath();
		std::string destination = (std::filesystem::path{ p_destination } / selectAssetDialog.GetSelectedFileName()).string();

		if (!std::filesystem::exists(destination) || MessageBox("File already exists", "The destination you have selected already exists, importing this file will erase the previous file content, are you sure about that?", MessageBox::EMessageType::WARNING, MessageBox::EButtonLayout::OK_CANCEL).GetUserAction() == MessageBox::EUserAction::OK)
		{
			std::filesystem::copy(source, destination, std::filesystem::copy_options::overwrite_existing);
			OVLOG_INFO("Asset \"" + destination + "\" imported");
			return true;
		}
	}

	return false;
}

std::string OvEditor::Core::EditorActions::GetRealPath(const std::string& p_path)
{
	return OvTools::Utils::PathParser::GetRealPath(
		std::filesystem::path{ p_path },
		m_context.engineAssetsPath,
		m_context.projectAssetsPath
	).string();
}

std::string OvEditor::Core::EditorActions::GetResourcePath(const std::string& p_path, bool p_isFromEngine)
{
	// Normalize to forward slashes so the comparison works regardless of whether
	// the caller or the stored context paths use POSIX or Windows separators.
	std::string result = std::filesystem::path(p_path).generic_string();
	const std::string contextPath = (p_isFromEngine ? m_context.engineAssetsPath : m_context.projectAssetsPath).generic_string();

	if (OvTools::Utils::String::Replace(result, contextPath, ""))
	{
		if (result.starts_with('/'))
		{
			result = result.substr(1);
		}

		if (p_isFromEngine)
		{
			result = ':' + result;
		}
	}

	return result;
}

std::string OvEditor::Core::EditorActions::GetScriptPath(const std::string& p_path)
{
	// Normalize to forward slashes so the comparison works regardless of separator style.
	std::string result = std::filesystem::path(p_path).generic_string();
	const std::string contextPath = m_context.projectAssetsPath.generic_string();

	OvTools::Utils::String::Replace(result, contextPath, "");

	if (result.starts_with('/'))
	{
		result = result.substr(1);
	}

	return result;
}

void OvEditor::Core::EditorActions::PropagateFolderRename(std::string p_previousName, std::string p_newName)
{
	p_previousName = OvTools::Utils::PathParser::MakeNonWindowsStyle(p_previousName);
	p_newName = OvTools::Utils::PathParser::MakeNonWindowsStyle(p_newName);

	for (auto& p : std::filesystem::recursive_directory_iterator(p_newName))
	{
		if (!p.is_directory())
		{
			std::string newFileName = OvTools::Utils::PathParser::MakeNonWindowsStyle(p.path().string());
			std::string previousFileName;

			for (char c : newFileName)
			{
				previousFileName += c;
				if (previousFileName == p_newName)
					previousFileName = p_previousName;
			}

			PropagateFileRename(
				OvTools::Utils::PathParser::MakeWindowsStyle(previousFileName),
				OvTools::Utils::PathParser::MakeWindowsStyle(newFileName)
			);
		}
	}
}

void OvEditor::Core::EditorActions::PropagateFolderDestruction(std::string p_folderPath)
{
	for (auto& p : std::filesystem::recursive_directory_iterator(p_folderPath))
	{
		if (!p.is_directory())
		{
			PropagateFileRename(OvTools::Utils::PathParser::MakeNonWindowsStyle(p.path().string()), "?");
		}
	}
}

void OvEditor::Core::EditorActions::MigrateScripts()
{
	const auto legacyScriptsPath = m_context.projectFolder / "Scripts";

	if (!std::filesystem::exists(legacyScriptsPath) || !std::filesystem::is_directory(legacyScriptsPath))
	{
		return;
	}

	using namespace OvWindowing::Dialogs;

	MessageBox message(
		"Legacy Scripts/ folder detected",
		"A \"Scripts/\" folder was found in your project directory.\n\n"
		"Scripts are now stored inside \"Assets/\" and support subdirectories.\n\n"
		"Would you like to migrate your scripts to \"Assets/Scripts/\"?\n"
		"All scene files referencing these scripts will be updated automatically.\n\n"
		"Note that any script already present in Assets/Scripts/ with the same name as a script in Scripts/ will be overridden.",
		MessageBox::EMessageType::WARNING,
		MessageBox::EButtonLayout::YES_NO,
		true
	);

	if (message.GetUserAction() != MessageBox::EUserAction::YES)
	{
		return;
	}

	const auto targetPath = m_context.projectAssetsPath / "Scripts";
	std::vector<std::filesystem::path> migratedScriptNames;

	for (const auto& entry : std::filesystem::recursive_directory_iterator(legacyScriptsPath))
	{
		if (!entry.is_directory())
		{
			if (OvTools::Utils::PathParser::GetFileType(entry.path().string()) == OvTools::Utils::PathParser::EFileType::SCRIPT)
			{
				migratedScriptNames.push_back(entry.path().filename());
			}
		}
	}

	std::error_code err;

	if (!std::filesystem::exists(targetPath))
	{
		std::filesystem::rename(legacyScriptsPath, targetPath, err);
	}
	else if (std::filesystem::is_directory(targetPath))
	{
		std::filesystem::create_directories(targetPath, err);

		if (!err)
		{
			for (const auto& entry : std::filesystem::recursive_directory_iterator(legacyScriptsPath))
			{
				const auto destination = targetPath / entry.path().lexically_relative(legacyScriptsPath);

				if (entry.is_directory())
				{
					std::filesystem::create_directories(destination, err);
				}
				else
				{
					std::filesystem::create_directories(destination.parent_path(), err);

					if (!err)
					{
						std::filesystem::copy_file(entry.path(), destination, std::filesystem::copy_options::overwrite_existing, err);
					}
				}

				if (err)
				{
					break;
				}
			}
		}

		if (!err)
		{
			std::filesystem::remove_all(legacyScriptsPath, err);
		}
	}
	else
	{
		OVLOG_ERROR("Failed to migrate Scripts/ folder: Assets/Scripts exists but is not a directory.");
		return;
	}

	if (err)
	{
		OVLOG_ERROR("Failed to migrate Scripts/ folder: " + err.message());
		return;
	}

	OVLOG_INFO("Scripts/ folder migrated to Assets/Scripts/");

	// Update all scene files: replace old behaviour type (just the stem) with the new relative path
	for (const auto& scriptName : migratedScriptNames)
	{
		const auto stem = scriptName.stem().string();
		const auto newRelPath = (std::filesystem::path("Scripts") / scriptName).generic_string();

		PropagateFileRenameThroughSavedFilesOfType(stem, newRelPath, OvTools::Utils::PathParser::EFileType::SCENE);
		PropagateFileRenameThroughSavedFilesOfType(stem, newRelPath, OvTools::Utils::PathParser::EFileType::PREFAB);
	}

	OVLOG_INFO("Scene files updated with new script paths");
}

void OvEditor::Core::EditorActions::PropagateFileRename(std::string p_previousName, std::string p_newName)
{
	p_previousName = GetResourcePath(p_previousName);
	p_newName = GetResourcePath(p_newName);

	if (p_newName != "?")
	{
		/* If not a real rename is asked (Not delete) */

		if (OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::ModelManager>().MoveResource(p_previousName, p_newName))
		{
			OvRendering::Resources::Model* resource = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::ModelManager>()[p_newName];
			const_cast<std::string&>(resource->path) = p_newName;
		}

		if (OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::TextureManager>().MoveResource(p_previousName, p_newName))
		{
			OvRendering::Resources::Texture* resource = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::TextureManager>()[p_newName];
			const_cast<std::string&>(resource->path) = p_newName;
		}

		if (OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::ShaderManager>().MoveResource(p_previousName, p_newName))
		{
			OvRendering::Resources::Shader* resource = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::ShaderManager>()[p_newName];
			const_cast<std::string&>(resource->path) = p_newName;
		}

		if (OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::MaterialManager>().MoveResource(p_previousName, p_newName))
		{
			OvCore::Resources::Material* resource = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::MaterialManager>()[p_newName];
			const_cast<std::string&>(resource->path) = p_newName;
		}

		if (OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::SoundManager>().MoveResource(p_previousName, p_newName))
		{
			OvAudio::Resources::Sound* resource = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::SoundManager>()[p_newName];
			const_cast<std::string&>(resource->path) = p_newName;
		}

		if (OvTools::Utils::PathParser::GetFileType(p_previousName) == OvTools::Utils::PathParser::EFileType::MODEL)
		{
			MoveAllEmbeddedResourcesForRenamedModel(p_previousName, p_newName);
		}
	}
	else
	{
		if (auto texture = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::TextureManager>().GetResource(p_previousName, false))
		{
			for (auto [name, instance] : OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::MaterialManager>().GetResources())
			{
				if (instance)
				{
					for (auto& [name, prop] : instance->GetProperties())
					{
						if (const auto pval = std::get_if<OvRendering::Resources::Texture*>(&prop.value); pval && *pval)
						{
							if (*pval == texture)
							{
								prop.value = static_cast<OvRendering::Resources::Texture*>(nullptr);
							}
						}
					}
				}
			}

			auto& assetView = EDITOR_PANEL(Panels::AssetView, "Asset View");
			auto assetViewRes = assetView.GetResource();
			if (auto pval = std::get_if<OvRendering::Resources::Texture*>(&assetViewRes); pval && *pval)
				assetView.ClearResource();

			OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::TextureManager>().UnloadResource(p_previousName);
		}
		
		if (auto shader = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::ShaderManager>().GetResource(p_previousName, false))
		{
			auto& materialEditor = EDITOR_PANEL(Panels::MaterialEditor, "Material Editor");

			for (auto[name, instance] : OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::MaterialManager>().GetResources())
				if (instance && instance->GetShader() == shader)
					instance->SetShader(nullptr);

			OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::ShaderManager>().UnloadResource(p_previousName);
		}

		if (auto model = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::ModelManager>().GetResource(p_previousName, false))
		{
			auto& assetView = EDITOR_PANEL(Panels::AssetView, "Asset View");
			auto assetViewRes = assetView.GetResource();
			if (auto pval = std::get_if<OvRendering::Resources::Model*>(&assetViewRes); pval && *pval)
				assetView.ClearResource();

			if (auto currentScene = m_context.sceneManager.GetCurrentScene())
				for (auto actor : currentScene->GetActors())
					if (auto modelRenderer = actor->GetComponent<OvCore::ECS::Components::CModelRenderer>(); modelRenderer && modelRenderer->GetModel() == model)
						modelRenderer->SetModel(nullptr);

			OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::ModelManager>().UnloadResource(p_previousName);
		}

		if (auto material = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::MaterialManager>().GetResource(p_previousName, false))
		{
			auto& materialEditor = EDITOR_PANEL(Panels::MaterialEditor, "Material Editor");
			
			if (materialEditor.GetTarget() == material)
				materialEditor.RemoveTarget();

			auto& assetView = EDITOR_PANEL(Panels::AssetView, "Asset View");
			auto assetViewRes = assetView.GetResource();
			if (auto pval = std::get_if<OvCore::Resources::Material*>(&assetViewRes); pval && *pval)
				assetView.ClearResource();

			if (auto currentScene = m_context.sceneManager.GetCurrentScene())
				for (auto actor : currentScene->GetActors())
					if (auto materialRenderer = actor->GetComponent<OvCore::ECS::Components::CMaterialRenderer>(); materialRenderer)
						materialRenderer->RemoveMaterialByInstance(*material);

			OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::MaterialManager>().UnloadResource(p_previousName);
		}

		if (auto sound = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::SoundManager>().GetResource(p_previousName, false))
		{
			if (auto currentScene = m_context.sceneManager.GetCurrentScene())
				for (auto actor : currentScene->GetActors())
					if (auto audioSource = actor->GetComponent<OvCore::ECS::Components::CAudioSource>(); audioSource && audioSource->GetSound() == sound)
						audioSource->SetSound(nullptr);

			OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::SoundManager>().UnloadResource(p_previousName);
		}
	}

	switch (OvTools::Utils::PathParser::GetFileType(p_previousName))
	{
	case OvTools::Utils::PathParser::EFileType::SCRIPT:
	{
		// Normalize to forward slashes (Behaviour::name uses forward slashes as path separator)
		std::string prev = p_previousName;
		std::string next = p_newName;
		std::replace(prev.begin(), prev.end(), '\\', '/');
		if (next != "?") std::replace(next.begin(), next.end(), '\\', '/');

		if (auto currentScene = m_context.sceneManager.GetCurrentScene())
		{
			for (auto actor : currentScene->GetActors())
			{
				if (next != "?")
					actor->RenameBehaviour(prev, next);
				else
					actor->RemoveBehaviour(prev);
			}
		}

		if (next != "?")
		{
			PropagateFileRenameThroughSavedFilesOfType(prev, next, OvTools::Utils::PathParser::EFileType::SCENE);
			PropagateFileRenameThroughSavedFilesOfType(prev, next, OvTools::Utils::PathParser::EFileType::PREFAB);
		}

		EDITOR_PANEL(Panels::Inspector, "Inspector").Refresh();
		break;
	}
	case OvTools::Utils::PathParser::EFileType::MATERIAL:
		PropagateFileRenameThroughSavedFilesOfType(p_previousName, p_newName, OvTools::Utils::PathParser::EFileType::SCENE);
		PropagateFileRenameThroughSavedFilesOfType(p_previousName, p_newName, OvTools::Utils::PathParser::EFileType::PREFAB);
		break;
	case OvTools::Utils::PathParser::EFileType::MODEL:
		PropagateFileRenameThroughSavedFilesOfType(p_previousName, p_newName, OvTools::Utils::PathParser::EFileType::SCENE);
		PropagateFileRenameThroughSavedFilesOfType(p_previousName, p_newName, OvTools::Utils::PathParser::EFileType::PREFAB);
		PropagateFileRenameThroughSavedFilesOfType(p_previousName, p_newName, OvTools::Utils::PathParser::EFileType::MATERIAL);
		break;
	case OvTools::Utils::PathParser::EFileType::SHADER:
		PropagateFileRenameThroughSavedFilesOfType(p_previousName, p_newName, OvTools::Utils::PathParser::EFileType::MATERIAL);
		break;
	case OvTools::Utils::PathParser::EFileType::TEXTURE:
		PropagateFileRenameThroughSavedFilesOfType(p_previousName, p_newName, OvTools::Utils::PathParser::EFileType::MATERIAL);
		break;
	case OvTools::Utils::PathParser::EFileType::SOUND:
		PropagateFileRenameThroughSavedFilesOfType(p_previousName, p_newName, OvTools::Utils::PathParser::EFileType::SCENE);
		PropagateFileRenameThroughSavedFilesOfType(p_previousName, p_newName, OvTools::Utils::PathParser::EFileType::PREFAB);
		break;
	case OvTools::Utils::PathParser::EFileType::PREFAB:
		if (auto currentScene = m_context.sceneManager.GetCurrentScene())
		{
			for (auto actor : currentScene->GetActors())
			{
				if (actor->GetPrefabSource() == p_previousName)
				{
					actor->SetPrefabSource(p_newName);
				}
			}
		}

		PropagateFileRenameThroughSavedFilesOfType(p_previousName, p_newName, OvTools::Utils::PathParser::EFileType::SCENE);
		PropagateFileRenameThroughSavedFilesOfType(p_previousName, p_newName, OvTools::Utils::PathParser::EFileType::PREFAB);
		break;
	default:
		break;
	}

	EDITOR_PANEL(Panels::Inspector, "Inspector").Refresh();
	EDITOR_PANEL(Panels::MaterialEditor, "Material Editor").Refresh();
}

uint64_t ReplaceStringInFile(const std::filesystem::path& p_filePath,
	const std::string_view p_searchStr,
	const std::string_view p_replaceStr
)
{
	uint64_t occurences = 0;

	if (!std::filesystem::exists(p_filePath))
	{
		throw std::runtime_error("File does not exist: " + p_filePath.string());
	}

	std::string content;

	if (auto inFile = std::ifstream{ p_filePath, std::ios::in })
	{
		std::stringstream buffer;
		buffer << inFile.rdbuf();
		content = buffer.str();
	}
	else
	{
		throw std::runtime_error("Cannot open file for reading: " + p_filePath.string());
	}
	 
	size_t pos = 0;

	while ((pos = content.find(p_searchStr, pos)) != std::string::npos)
	{
		content.replace(pos, p_searchStr.length(), p_replaceStr);
		pos += p_replaceStr.length();
		++occurences;
	}

	if (occurences > 0)
	{
		if (auto outFile = std::ofstream{ p_filePath, std::ios::out | std::ios::trunc })
		{
			outFile << content;
		}
		else
		{
			throw std::runtime_error("Cannot open file for writing: " + p_filePath.string());
		}
	}

	return occurences;
}

void OvEditor::Core::EditorActions::PropagateFileRenameThroughSavedFilesOfType(
	const std::string& p_previousName,
	const std::string& p_newName,
	OvTools::Utils::PathParser::EFileType p_fileType
)
{
	const bool renameEmbeddedAssets =
		p_newName != "?" &&
		OvTools::Utils::PathParser::GetFileType(p_previousName) == OvTools::Utils::PathParser::EFileType::MODEL;

	const auto replaceFrom = std::string{ ">" + p_previousName + "<" };
	const auto replaceTo = std::string{ ">" + p_newName + "<" };
	const auto embeddedReplaceFrom = std::string{ ">" + p_previousName + ":" };
	const auto embeddedReplaceTo = std::string{ ">" + p_newName + ":" };

	for (const auto& entry : std::filesystem::recursive_directory_iterator(m_context.projectAssetsPath))
	{
		const std::filesystem::path entryPath = entry.path();

		if (OvTools::Utils::PathParser::GetFileType(entryPath.string()) == p_fileType)
		{
			try
			{
				uint64_t occurences = ReplaceStringInFile(entryPath, replaceFrom, replaceTo);

				if (renameEmbeddedAssets)
				{
					occurences += ReplaceStringInFile(entryPath, embeddedReplaceFrom, embeddedReplaceTo);
				}

				if (occurences > 0)
				{
					OVLOG_INFO("Asset retargeting: \"" + p_previousName + "\" to \"" + p_newName + "\" in \"" + entryPath.string() + "\"");
				}
			}
			catch (const std::exception& e)
			{
				const auto errorMessage = std::string{ e.what() };
				OVLOG_ERROR("Asset retargeting failed: " + errorMessage);
			}
		}
	}
}
