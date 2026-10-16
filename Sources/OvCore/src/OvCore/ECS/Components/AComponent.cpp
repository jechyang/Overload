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
#include <OvCore/ECS/Components/CTransform.h>
#include <OvCore/Scripting/DispatchScope.h>
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

bool OvCore::ECS::Components::AComponent::IsEnabled() const
{
    return m_enabled && owner.IsActive();
}

void OvCore::ECS::Components::AComponent::SetEnabled(bool p_enabled)
{
    if (dynamic_cast<CTransform*>(this))
        return;

    if (m_enabled == p_enabled)
        return;

    m_enabled = p_enabled;

    if (!owner.IsSleeping() && owner.IsActive())
    {
        Scripting::DispatchScope scope;
        if (m_enabled)
            OnEnable();
        else
            OnDisable();
        Scripting::DispatchScope::Flush();
    }
}

OvCore::ECS::Components::AComponent::~AComponent()
{
	if (owner.IsActive())
	{
		if (m_enabled) OnDisable();
		OnDestroy();
	}
}
