#pragma once
#include <OvCore/Helpers/ReflectionProperties.h>
#include <OvCore/ECS/ComponentRegistry.h>
#include <OvUI/Internal/WidgetContainer.h>

namespace OvCore::Helpers::ReflectionProperties
{
    struct Binding
    {
        const PropertyDesc* property;
        std::function<Result(std::any&)> read;
        std::function<Result(const std::any&)> write;
        std::string identity;
        bool readOnly = false;
    };
    // Caller supplies an epoch that expires before target destruction or detachment.
    // Every invocation checks application Main before touching either target or ticket.
    template<class T> Binding Bind(T& target, const PropertyDesc& property, const MainThread& main, std::weak_ptr<const bool> ticket)
    {
        return {&property,
            [target = &target, p = &property, main = &main, ticket](std::any& value) -> Result
            {
                if (!main->IsCurrent()) return {std::string(p->Info().id), Status::AccessDenied, Error::WrongThread};
                if (ticket.expired()) return {std::string(p->Info().id), Status::Expired};
                auto error = p->ReadValue(*main, ObjectRef::From(*target), value);
                return {std::string(p->Info().id), error == Error::None ? Status::Applied : Status::AccessDenied, error};
            },
            [target = &target, p = &property, main = &main, ticket](const std::any& value) -> Result
            {
                if (!main->IsCurrent()) return {std::string(p->Info().id), Status::AccessDenied, Error::WrongThread};
                if (ticket.expired()) return {std::string(p->Info().id), Status::Expired};
                auto error = p->WriteValue(*main, ObjectRef::From(*target), value);
                return {std::string(p->Info().id), error == Error::None ? Status::Applied : error == Error::ReadOnly ? Status::ReadOnly : Status::AccessDenied, error};
            }};
    }
    using CustomDrawer = Result (*)(OvUI::Internal::WidgetContainer&, const Binding&, const Registry&);
    struct DrawerHook { std::string_view id; CustomDrawer draw; };
    Result Draw(OvUI::Internal::WidgetContainer& root, const Binding& binding, const Registry& registry, std::span<const DrawerHook> hooks = {});
    std::span<const DrawerHook> ComponentDrawers();
    Report DrawComponent(OvUI::Internal::WidgetContainer& root, ECS::Components::AComponent& component);
    template<class T> Report DrawComponent(OvUI::Internal::WidgetContainer& root, T& component)
    {
        const auto& registry = ECS::GetComponentRegistry().Reflection(); Report report;
        for (auto* property : Properties(TypeOf<T>(), "Inspect"))
            report.fields.push_back(Draw(root, Bind(component, *property, registry.Main(), component.InspectorTicket()), registry, ComponentDrawers()));
        return report;
    }
}
