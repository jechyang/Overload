#pragma once

#include <OvReflection/Reflection.h>
#include <optional>

namespace OvCore::ECS
{
    class Actor;
    namespace Components { class AComponent; }

    // Immutable descriptions own no actors/components. Operations borrow an actor on Main.
    class ComponentDesc final
    {
    public:
        const OvReflection::TypeDesc* const type;

        std::string_view GetName() const;
        bool IsAddable(const Actor& actor) const;
        Components::AComponent* AddComponent(Actor& actor) const;
        // Archive creation deliberately retains Actor::AddComponent<T> semantics,
        // including archives with more than one kind of physical component.
        Components::AComponent* DeserializeComponent(Actor& actor) const;
        std::optional<OvReflection::ObjectRef> Borrow(Components::AComponent& component) const;
    private:
        template<class T, bool Existing> friend ComponentDesc MakeComponentDesc(const OvReflection::TypeDesc&);
        using Acquire = Components::AComponent* (*)(Actor&);
        using CanAdd = bool (*)(const Actor&);
        using BorrowObject = std::optional<OvReflection::ObjectRef> (*)(Components::AComponent&);
        ComponentDesc(const OvReflection::TypeDesc* description, Acquire acquire, CanAdd canAdd, BorrowObject borrow)
            : type(description), m_acquire(acquire), m_canAdd(canAdd), m_borrow(borrow) {}
        Acquire m_acquire;
        CanAdd m_canAdd;
        BorrowObject m_borrow;
    };

    class ComponentRegistry final
    {
    public:
        ComponentRegistry();
        const ComponentDesc* Find(std::string_view identity) const;
        std::span<const ComponentDesc> Components() const { return m_components; }
        const OvReflection::Registry& Reflection() const { return m_reflection; }
    private:
        OvReflection::Registry m_reflection;
        std::span<const ComponentDesc> m_components;
    };

    // First initialized by Actor on the application's Main thread, before CreatedEvent.
    const ComponentRegistry& GetComponentRegistry();
    std::span<const ComponentDesc> GetOvCoreComponents(); // generated explicit linkage root
}
