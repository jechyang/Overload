/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include "OvCore/ECS/Components/AComponent.h"
#include "OvCore/ECS/Actor.h"
#include <OvCore/ECS/ComponentRegistry.h>
#include <OvCore/Helpers/ReflectionProperties.h>
#include <OvCore/Helpers/ReflectionDrawer.h>
#include <stdexcept>

OvCore::ECS::Components::AComponent::AComponent(ECS::Actor& p_owner) : owner(p_owner)
{
}

void OvCore::ECS::Components::AComponent::OnSerialize(tinyxml2::XMLDocument& doc, tinyxml2::XMLNode* node)
{
    const auto& registry = ECS::GetComponentRegistry();
    if (!registry.Reflection().Main().IsCurrent()) throw std::runtime_error("Component serialization requires Main");
    const auto* description = registry.Find(GetTypeName());
    auto object = description ? description->Borrow(*this) : std::nullopt;
    if (!node || !object) throw std::runtime_error("Cannot serialize an unregistered component");
    Helpers::ReflectionProperties::Serialize(registry.Reflection().Main(), *object, *description->type, doc, *node,
        Helpers::ReflectionProperties::ComponentHooks()).Require();
}

void OvCore::ECS::Components::AComponent::OnDeserialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode* node)
{
    const auto& registry = ECS::GetComponentRegistry();
    if (!registry.Reflection().Main().IsCurrent()) throw std::runtime_error("Component deserialization requires Main");
    const auto* description = registry.Find(GetTypeName());
    auto object = description ? description->Borrow(*this) : std::nullopt;
    if (!node || !object) throw std::runtime_error("Cannot deserialize an unregistered component");
    Helpers::ReflectionProperties::Deserialize(registry.Reflection().Main(), *object, *description->type, *node,
        Helpers::ReflectionProperties::ComponentHooks()).Require();
}

void OvCore::ECS::Components::AComponent::OnInspector(OvUI::Internal::WidgetContainer& root)
{
    Helpers::ReflectionProperties::DrawComponent(root, *this).Require();
}

OvCore::ECS::Components::AComponent::~AComponent()
{
	if (owner.IsActive())
	{
		OnDisable();
		OnDestroy();
	}
}
