#include <OvCore/ECS/ComponentRegistry.h>
#include "OvCore/Reflection.generated.h"
#include <stdexcept>

namespace OvCore::ECS
{
    ComponentRegistry::ComponentRegistry()
    {
        using namespace OvReflection;
        if (RegisterBuiltinTypes(m_reflection) != Error::None ||
            RegisterOvMathsTypes(m_reflection) != Error::None ||
            RegisterOvCoreTypes(m_reflection) != Error::None ||
            m_reflection.Freeze() != Error::None)
            throw std::logic_error("Native reflection registration failed");
        m_components = GetOvCoreComponents();
    }

    const ComponentRegistry& GetComponentRegistry()
    {
        static const ComponentRegistry registry;
        return registry;
    }

    const ComponentDesc* ComponentRegistry::Find(std::string_view identity) const
    {
        const auto* type = m_reflection.Find(identity);
        for (const auto& component : m_components)
            if (component.type == type) return &component;
        return nullptr;
    }

    std::string_view ComponentDesc::GetName() const
    {
        for (const auto& item : type->Info().metadata)
            if (item.key == "DisplayName") return item.value;
        return type->Info().cppName;
    }

    bool ComponentDesc::IsAddable(const Actor& actor) const
    {
        return GetComponentRegistry().Reflection().Main().Check(OvReflection::Thread::Main) == OvReflection::Error::None && m_canAdd(actor);
    }

    Components::AComponent* ComponentDesc::AddComponent(Actor& actor) const
    {
        return IsAddable(actor) ? m_acquire(actor) : nullptr;
    }

    Components::AComponent* ComponentDesc::DeserializeComponent(Actor& actor) const
    {
        return GetComponentRegistry().Reflection().Main().Check(OvReflection::Thread::Main) == OvReflection::Error::None ? m_acquire(actor) : nullptr;
    }
}
