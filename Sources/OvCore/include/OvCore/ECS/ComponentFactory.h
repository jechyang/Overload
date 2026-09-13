#pragma once

#include <OvCore/ECS/Actor.h>
#include <OvCore/ECS/ComponentRegistry.h>
#include <OvCore/ECS/Components/CPhysicalObject.h>

namespace OvCore::ECS
{
    template<class T, bool Existing>
    ComponentDesc MakeComponentDesc(const OvReflection::TypeDesc& type)
    {
        static_assert(std::is_base_of_v<Components::AComponent, T>);
        static_assert(Existing || std::is_constructible_v<T, Actor&>, "Component factory requires an Actor& constructor");
        return {&type,
            [](Actor& actor) -> Components::AComponent*
            {
                // Abstract base descriptions support inheritance queries, never archives.
                if constexpr (std::is_abstract_v<T>) return nullptr;
                else if constexpr (Existing) return actor.GetComponent<T>();
                else return &actor.AddComponent<T>();
            },
            [](const Actor& actor)
            {
                if constexpr (Existing || std::is_abstract_v<T>) return false;
                else if constexpr (std::is_base_of_v<Components::CPhysicalObject, T>)
                    return !actor.GetComponent<Components::CPhysicalObject>();
                else return !actor.GetComponent<T>();
            }};
    }
}
