#pragma once
#include <OvCore/ECS/Components/AComponent.h>
#include <OvReflection/ScriptBridge.h>
namespace OvCore::Scripting
{
    // Include the module's generated type declarations at the call site.
    // Actor invalidates this epoch before component removal/destruction notifications.
    template<class T> uint64_t PublishComponent(OvReflection::Script::Context& context, T& component)
    {
        static_assert(std::is_base_of_v<ECS::Components::AComponent, std::remove_const_t<T>>);
        return context.Publish(component, component.InspectorTicket());
    }
}
