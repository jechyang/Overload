/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <algorithm>

#include <tinyxml2.h>

#include <OvCore/ECS/Actor.h>
#include <OvCore/ECS/ComponentRegistry.h>

#include <iostream>
#include <stdexcept>

OvTools::Eventing::Event<OvCore::ECS::Actor&> OvCore::ECS::Actor::DestroyedEvent;
OvTools::Eventing::Event<OvCore::ECS::Actor&> OvCore::ECS::Actor::CreatedEvent;
OvTools::Eventing::Event<OvCore::ECS::Actor&, OvCore::ECS::Actor&> OvCore::ECS::Actor::AttachEvent;
OvTools::Eventing::Event<OvCore::ECS::Actor&> OvCore::ECS::Actor::DettachEvent;

OvCore::ECS::Actor::Actor(int64_t p_actorID, const std::string & p_name, const std::string & p_tag, bool& p_playing) :
	m_actorID(p_actorID),
	m_guid(OvTools::Utils::GenerateGUID()),
	m_name(p_name),
	m_tag(p_tag),
	m_playing(p_playing),
	transform(AddComponent<Components::CTransform>())
{
	GetComponentRegistry();
	CreatedEvent.Invoke(*this);
}

OvCore::ECS::Actor::~Actor()
{
	if (!m_sleeping)
	{
		if (IsActive())
			OnDisable();

		if (m_awaked && m_started)
			OnDestroy();
	}

	for (auto& component : m_components) component->m_inspectorCallbacks.Invalidate();
	DestroyedEvent.Invoke(*this);

	std::vector<Actor*> toDetach = m_children;

	for (auto child : toDetach)
		child->DetachFromParent();

	toDetach.clear();

	DetachFromParent();

	std::for_each(m_components.begin(), m_components.end(), [&](std::shared_ptr<Components::AComponent> p_component) { ComponentRemovedEvent.Invoke(*p_component); });
	std::for_each(m_behaviours.begin(), m_behaviours.end(), [&](auto& p_behaviour) { BehaviourRemovedEvent.Invoke(std::ref(p_behaviour.second)); });
	std::for_each(m_children.begin(), m_children.end(),		[](Actor* p_element) { delete p_element; });
}

const std::string & OvCore::ECS::Actor::GetName() const
{
	return m_name;
}

const std::string & OvCore::ECS::Actor::GetTag() const
{
	return m_tag;
}

void OvCore::ECS::Actor::SetName(const std::string & p_name)
{
	m_name = p_name;
}

void OvCore::ECS::Actor::SetTag(const std::string & p_tag)
{
	m_tag = p_tag;
}

void OvCore::ECS::Actor::SetActive(bool p_active)
{
	if (p_active != m_active)
	{
		RecursiveWasActiveUpdate();
		m_active = p_active;
		RecursiveActiveUpdate();
	}
}

bool OvCore::ECS::Actor::IsSelfActive() const
{
	return m_active;
}

bool OvCore::ECS::Actor::IsActive() const
{
	return m_active && (m_parent ? m_parent->IsActive() : true);
}

void OvCore::ECS::Actor::SetID(int64_t p_id)
{
	m_actorID = p_id;
}

void OvCore::ECS::Actor::SetGUID(uint64_t p_guid)
{
	m_guid = p_guid;
}

int64_t OvCore::ECS::Actor::GetID() const
{
	return m_actorID;
}

uint64_t OvCore::ECS::Actor::GetGUID() const
{
	return m_guid;
}

void OvCore::ECS::Actor::SetPrefabSource(const std::string& p_prefabSource)
{
	if (p_prefabSource == "?")
	{
		m_prefabSource.clear();
	}
	else
	{
		m_prefabSource = p_prefabSource;
	}
}

const std::string& OvCore::ECS::Actor::GetPrefabSource() const
{
	return m_prefabSource;
}

bool OvCore::ECS::Actor::HasPrefabSource() const
{
	return !m_prefabSource.empty();
}

void OvCore::ECS::Actor::SetPrefabNodeGUID(uint64_t p_prefabNodeGUID)
{
	m_prefabNodeGUID = p_prefabNodeGUID;
}

uint64_t OvCore::ECS::Actor::GetPrefabNodeGUID() const
{
	return m_prefabNodeGUID;
}

bool OvCore::ECS::Actor::HasPrefabNodeGUID() const
{
	return m_prefabNodeGUID != 0;
}

void OvCore::ECS::Actor::SetParent(Actor& p_parent)
{
	DetachFromParent();

	/* Define the given parent as the new parent */
	m_parent = &p_parent;
	m_parentID = p_parent.m_actorID;
	transform.SetParent(p_parent.transform);

	/* Store the actor in the parent children list */
	p_parent.m_children.push_back(this);

	AttachEvent.Invoke(*this, p_parent);
}

void OvCore::ECS::Actor::DetachFromParent()
{
	DettachEvent.Invoke(*this);

	/* Remove the actor from the parent children list */
	if (m_parent)
	{
		m_parent->m_children.erase(std::remove_if(m_parent->m_children.begin(), m_parent->m_children.end(), [this](Actor* p_element)
		{
			return p_element == this;
		}));
	}

	m_parent = nullptr;
	m_parentID = 0;

	transform.RemoveParent();
}

bool OvCore::ECS::Actor::IsDescendantOf(const Actor* p_actor) const
{
	const Actor* currentParentActor = m_parent;

	while (currentParentActor != nullptr)
	{
		if (currentParentActor == p_actor)
		{
			return true;
		}
		currentParentActor = currentParentActor->GetParent();
	}

	return false;
}

bool OvCore::ECS::Actor::HasParent() const
{
	return m_parent;
}

OvCore::ECS::Actor * OvCore::ECS::Actor::GetParent() const
{
	return m_parent;
}

int64_t OvCore::ECS::Actor::GetParentID() const
{
	return m_parentID;
}

std::vector<OvCore::ECS::Actor*>& OvCore::ECS::Actor::GetChildren()
{
	return m_children;
}

OvCore::ECS::Actor* OvCore::ECS::Actor::FindChild(const std::string& p_name, bool p_recursive) const
{
	for (auto child : m_children)
	{
		if (child->GetName() == p_name)
		{
			return child;
		}
	}

	if (p_recursive)
	{
		for (auto child : m_children)
		{
			if (auto found = child->FindChild(p_name, true); found)
			{
				return found;
			}
		}
	}

	return nullptr;
}

void OvCore::ECS::Actor::MarkAsDestroy()
{
	m_destroyed = true;

	for (auto child : m_children)
		child->MarkAsDestroy();
}

bool OvCore::ECS::Actor::IsAlive() const
{
	return !m_destroyed;
}

void OvCore::ECS::Actor::SetSleeping(bool p_sleeping)
{
	m_sleeping = p_sleeping;
}

void OvCore::ECS::Actor::OnAwake()
{
	m_awaked = true;
	std::for_each(m_components.begin(), m_components.end(), [](auto element) { element->OnAwake(); });
	std::for_each(m_behaviours.begin(), m_behaviours.end(), [](auto & element) { element.second.OnAwake(); });
}

void OvCore::ECS::Actor::OnStart()
{
	m_started = true;
	std::for_each(m_components.begin(), m_components.end(), [](auto element) { element->OnStart(); });
	std::for_each(m_behaviours.begin(), m_behaviours.end(), [](auto & element) { element.second.OnStart(); });
}

void OvCore::ECS::Actor::OnEnable()
{
	std::for_each(m_components.begin(), m_components.end(), [](auto element) { element->OnEnable(); });
	std::for_each(m_behaviours.begin(), m_behaviours.end(), [](auto & element) { element.second.OnEnable(); });
}

void OvCore::ECS::Actor::OnDisable()
{
	std::for_each(m_components.begin(), m_components.end(), [](auto element) { element->OnDisable(); });
	std::for_each(m_behaviours.begin(), m_behaviours.end(), [](auto & element) { element.second.OnDisable(); });
}

void OvCore::ECS::Actor::OnDestroy()
{
	std::for_each(m_components.begin(), m_components.end(), [](auto element) { element->OnDestroy(); });
	std::for_each(m_behaviours.begin(), m_behaviours.end(), [](auto & element) { element.second.OnDestroy(); });
}

void OvCore::ECS::Actor::OnUpdate(float p_deltaTime)
{
	if (IsActive())
	{
		std::for_each(m_components.begin(), m_components.end(), [&](auto element) { element->OnUpdate(p_deltaTime); });
		std::for_each(m_behaviours.begin(), m_behaviours.end(), [&](auto & element) { element.second.OnUpdate(p_deltaTime); });
	}
}

void OvCore::ECS::Actor::OnFixedUpdate(float p_deltaTime)
{
	if (IsActive())
	{
		std::for_each(m_components.begin(), m_components.end(), [&](auto element) { element->OnFixedUpdate(p_deltaTime); });
		std::for_each(m_behaviours.begin(), m_behaviours.end(), [&](auto & element) { element.second.OnFixedUpdate(p_deltaTime); });
	}
}

void OvCore::ECS::Actor::OnLateUpdate(float p_deltaTime)
{
	if (IsActive())
	{
		std::for_each(m_components.begin(), m_components.end(), [&](auto element) { element->OnLateUpdate(p_deltaTime); });
		std::for_each(m_behaviours.begin(), m_behaviours.end(), [&](auto & element) { element.second.OnLateUpdate(p_deltaTime); });
	}
}

void OvCore::ECS::Actor::OnCollisionEnter(Components::CPhysicalObject& p_otherObject)
{
	std::for_each(m_components.begin(), m_components.end(), [&](auto element) { element->OnCollisionEnter(p_otherObject); });
	std::for_each(m_behaviours.begin(), m_behaviours.end(), [&](auto & element) { element.second.OnCollisionEnter(p_otherObject); });
}

void OvCore::ECS::Actor::OnCollisionStay(Components::CPhysicalObject& p_otherObject)
{
	std::for_each(m_components.begin(), m_components.end(), [&](auto element) { element->OnCollisionStay(p_otherObject); });
	std::for_each(m_behaviours.begin(), m_behaviours.end(), [&](auto & element) { element.second.OnCollisionStay(p_otherObject); });
}

void OvCore::ECS::Actor::OnCollisionExit(Components::CPhysicalObject& p_otherObject)
{
	std::for_each(m_components.begin(), m_components.end(), [&](auto element) { element->OnCollisionExit(p_otherObject); });
	std::for_each(m_behaviours.begin(), m_behaviours.end(), [&](auto & element) { element.second.OnCollisionExit(p_otherObject); });
}

void OvCore::ECS::Actor::OnTriggerEnter(Components::CPhysicalObject& p_otherObject)
{
	std::for_each(m_components.begin(), m_components.end(), [&](auto element) { element->OnTriggerEnter(p_otherObject); });
	std::for_each(m_behaviours.begin(), m_behaviours.end(), [&](auto & element) { element.second.OnTriggerEnter(p_otherObject); });
}

void OvCore::ECS::Actor::OnTriggerStay(Components::CPhysicalObject& p_otherObject)
{
	std::for_each(m_components.begin(), m_components.end(), [&](auto element) { element->OnTriggerStay(p_otherObject); });
	std::for_each(m_behaviours.begin(), m_behaviours.end(), [&](auto & element) { element.second.OnTriggerStay(p_otherObject); });
}

void OvCore::ECS::Actor::OnTriggerExit(Components::CPhysicalObject& p_otherObject)
{
	std::for_each(m_components.begin(), m_components.end(), [&](auto element) { element->OnTriggerExit(p_otherObject); });
	std::for_each(m_behaviours.begin(), m_behaviours.end(), [&](auto & element) { element.second.OnTriggerExit(p_otherObject); });
}

bool OvCore::ECS::Actor::RemoveComponent(OvCore::ECS::Components::AComponent& p_component)
{
	if (dynamic_cast<Components::CTransform*>(&p_component))
		return false;
	for (auto it = m_components.begin(); it != m_components.end(); ++it)
	{
		if (it->get() == &p_component)
		{
			p_component.m_inspectorCallbacks.Invalidate();
			ComponentRemovedEvent.Invoke(p_component);
			m_components.erase(it);
			return true;
		}
	}

	return false;
}

std::vector<std::shared_ptr<OvCore::ECS::Components::AComponent>>& OvCore::ECS::Actor::GetComponents()
{
	return m_components;
}

OvCore::ECS::Components::Behaviour & OvCore::ECS::Actor::AddBehaviour(const std::string & p_name)
{
	m_behaviours.try_emplace(p_name, *this, p_name);
	m_behavioursOrder.push_back(p_name);
	Components::Behaviour& newInstance = m_behaviours.at(p_name);
	BehaviourAddedEvent.Invoke(newInstance);
	if (m_playing && !m_sleeping && IsActive())
	{
		newInstance.OnAwake();
		newInstance.OnEnable();
		newInstance.OnStart();
	}
	return newInstance;
}

bool OvCore::ECS::Actor::RemoveBehaviour(Components::Behaviour& p_behaviour)
{
	bool found = false;

	for (auto& [name, behaviour] : m_behaviours)
	{
		if (&behaviour == &p_behaviour)
		{
			found = true;
			break;
		}
	}

	if (found)
		return RemoveBehaviour(p_behaviour.name);
	else
		return false;
}

bool OvCore::ECS::Actor::RemoveBehaviour(const std::string & p_name)
{
	Components::Behaviour* found = GetBehaviour(p_name);
	if (found)
	{
		const std::string nameCopy = p_name;
		BehaviourRemovedEvent.Invoke(*found);
		m_behaviours.erase(p_name);
		auto it = std::find(m_behavioursOrder.begin(), m_behavioursOrder.end(), nameCopy);
		if (it != m_behavioursOrder.end())
			m_behavioursOrder.erase(it);
		return true;
	}
	else
	{
		return false;
	}
}

bool OvCore::ECS::Actor::RenameBehaviour(const std::string& p_previousName, const std::string& p_newName)
{
	auto orderIt = std::find(m_behavioursOrder.begin(), m_behavioursOrder.end(), p_previousName);
	if (orderIt == m_behavioursOrder.end())
		return false;

	Components::Behaviour* found = GetBehaviour(p_previousName);
	if (!found)
		return false;

	BehaviourRemovedEvent.Invoke(*found);
	m_behaviours.erase(p_previousName);

	*orderIt = p_newName;

	m_behaviours.try_emplace(p_newName, *this, p_newName);
	Components::Behaviour& newInstance = m_behaviours.at(p_newName);
	BehaviourAddedEvent.Invoke(newInstance);
	if (m_playing && !m_sleeping && IsActive())
	{
		newInstance.OnAwake();
		newInstance.OnEnable();
		newInstance.OnStart();
	}
	return true;
}

OvCore::ECS::Components::Behaviour* OvCore::ECS::Actor::GetBehaviour(const std::string& p_name)
{
	if (auto result = m_behaviours.find(p_name); result != m_behaviours.end())
		return &result->second;
	else
		return nullptr;
}

std::unordered_map<std::string, OvCore::ECS::Components::Behaviour>& OvCore::ECS::Actor::GetBehaviours()
{
	return m_behaviours;
}

std::vector<std::string>& OvCore::ECS::Actor::GetBehavioursOrder()
{
	return m_behavioursOrder;
}

void OvCore::ECS::Actor::OnSerialize(tinyxml2::XMLDocument & p_doc, tinyxml2::XMLNode * p_actorsRoot)
{
	tinyxml2::XMLNode* actorNode = p_doc.NewElement("actor");
	p_actorsRoot->InsertEndChild(actorNode);

	OvCore::Helpers::Serializer::SerializeString(p_doc, actorNode, "name", m_name);
	OvCore::Helpers::Serializer::SerializeString(p_doc, actorNode, "tag", m_tag);
	OvCore::Helpers::Serializer::SerializeBoolean(p_doc, actorNode, "active", m_active);
	OvCore::Helpers::Serializer::SerializeInt64(p_doc, actorNode, "id", m_actorID);
	OvCore::Helpers::Serializer::SerializeUInt64(p_doc, actorNode, "guid", m_guid);
	OvCore::Helpers::Serializer::SerializeString(p_doc, actorNode, "prefab_source", m_prefabSource);
	OvCore::Helpers::Serializer::SerializeUInt64(p_doc, actorNode, "prefab_node_guid", m_prefabNodeGUID);
	OvCore::Helpers::Serializer::SerializeInt64(p_doc, actorNode, "parent", m_parentID);

	tinyxml2::XMLNode* componentsNode = p_doc.NewElement("components");
	actorNode->InsertEndChild(componentsNode);

	for (auto& component : m_components)
	{
		/* Current component root */
		tinyxml2::XMLNode* componentNode = p_doc.NewElement("component");
		componentsNode->InsertEndChild(componentNode);

		/* Component type */
		OvCore::Helpers::Serializer::SerializeString(p_doc, componentNode, "type", component->GetTypeName());

		/* Data node (Will be passed to the component) */
		tinyxml2::XMLElement* data = p_doc.NewElement("data");
		componentNode->InsertEndChild(data);

		/* Data serialization of the component */
		component->OnSerialize(p_doc, data);
	}

	for (const auto& xml : m_unknownComponents)
	{
		tinyxml2::XMLDocument preserved;
		if (preserved.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS || !preserved.RootElement())
			throw std::runtime_error("Cannot restore preserved component XML");
		componentsNode->InsertEndChild(preserved.RootElement()->DeepClone(&p_doc));
	}

	tinyxml2::XMLNode* behavioursNode = p_doc.NewElement("behaviours");
	actorNode->InsertEndChild(behavioursNode);

	for (auto& name : m_behavioursOrder)
	{
		auto it = m_behaviours.find(name);
		if (it == m_behaviours.end()) continue;
		auto& behaviour = it->second;

		/* Current behaviour root */
		tinyxml2::XMLNode* behaviourNode = p_doc.NewElement("behaviour");
		behavioursNode->InsertEndChild(behaviourNode);

		/* Behaviour type */
		OvCore::Helpers::Serializer::SerializeString(p_doc, behaviourNode, "type", name);

		/* Data node (Will be passed to the behaviour) */
		tinyxml2::XMLElement* data = p_doc.NewElement("data");
		behaviourNode->InsertEndChild(data);

		/* Data serialization of the behaviour */
		behaviour.OnSerialize(p_doc, data);
	}
}

void OvCore::ECS::Actor::OnDeserialize(tinyxml2::XMLDocument & p_doc, tinyxml2::XMLNode * p_actorsRoot)
{
	OvCore::Helpers::Serializer::DeserializeString(p_doc, p_actorsRoot, "name", m_name);
	OvCore::Helpers::Serializer::DeserializeString(p_doc, p_actorsRoot, "tag", m_tag);
	OvCore::Helpers::Serializer::DeserializeBoolean(p_doc, p_actorsRoot, "active", m_active);
	OvCore::Helpers::Serializer::DeserializeInt64(p_doc, p_actorsRoot, "id", m_actorID);
	OvCore::Helpers::Serializer::DeserializeUInt64(p_doc, p_actorsRoot, "guid", m_guid);
	std::string prefabSource;
	OvCore::Helpers::Serializer::DeserializeString(p_doc, p_actorsRoot, "prefab_source", prefabSource);
	SetPrefabSource(prefabSource);
	uint64_t prefabNodeGUID = 0;
	OvCore::Helpers::Serializer::DeserializeUInt64(p_doc, p_actorsRoot, "prefab_node_guid", prefabNodeGUID);
	SetPrefabNodeGUID(prefabNodeGUID);
	OvCore::Helpers::Serializer::DeserializeInt64(p_doc, p_actorsRoot, "parent", m_parentID);

	m_unknownComponents.clear();
	{
		tinyxml2::XMLNode* componentsRoot = p_actorsRoot->FirstChildElement("components");
		if (componentsRoot)
		{
			tinyxml2::XMLElement* currentComponent = componentsRoot->FirstChildElement("component");

			while (currentComponent)
			{
				const auto* typeNode = currentComponent->FirstChildElement("type");
				const char* identity = typeNode ? typeNode->GetText() : nullptr;
				const auto* descriptor = identity ? GetComponentRegistry().Find(identity) : nullptr;
				auto* data = currentComponent->FirstChildElement("data");
				auto* component = descriptor && data ? descriptor->DeserializeComponent(*this) : nullptr;
				if (component)
				{
					component->OnDeserialize(p_doc, data);
				}
				else
				{
					// Keep the complete node (including unknown attributes/children),
					// independent of the caller's document lifetime.
					tinyxml2::XMLPrinter printer;
					currentComponent->Accept(&printer);
					m_unknownComponents.emplace_back(printer.CStr());
				}

				currentComponent = currentComponent->NextSiblingElement("component");
			}
		}
	}

	{
		tinyxml2::XMLNode* behavioursRoot = p_actorsRoot->FirstChildElement("behaviours");

		if (behavioursRoot)
		{
			tinyxml2::XMLElement* currentBehaviour = behavioursRoot->FirstChildElement("behaviour");

			while (currentBehaviour)
			{
				std::string behaviourType = currentBehaviour->FirstChildElement("type")->GetText();

				auto& behaviour = AddBehaviour(behaviourType);
				behaviour.OnDeserialize(p_doc, currentBehaviour->FirstChildElement("data"));

				currentBehaviour = currentBehaviour->NextSiblingElement("behaviour");
			}
		}
	}
}

void OvCore::ECS::Actor::RecursiveActiveUpdate()
{
	bool isActive = IsActive();

	if (!m_sleeping)
	{
		if (!m_wasActive && isActive)
		{
			if (!m_awaked)
				OnAwake();

			OnEnable();

			if (!m_started)
				OnStart();
		}

		if (m_wasActive && !isActive)
			OnDisable();
	}

	for (auto child : m_children)
		child->RecursiveActiveUpdate();
}

void OvCore::ECS::Actor::RecursiveWasActiveUpdate()
{
	m_wasActive = IsActive();
	for (auto child : m_children)
		child->RecursiveWasActiveUpdate();
}
