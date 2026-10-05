#pragma once
#include <OvCore/Helpers/ReflectionProperties.h>

namespace OvCore::Helpers::ReflectionProperties
{
    // Record descriptors use public fields on owned aggregate copies. Parsing
    // never invokes the component setter or resolves manager-owned resources.
    template<class T> ValueCodec MakeRecordCodec()
    {
        static_assert(std::is_default_constructible_v<T> && std::is_copy_constructible_v<T>);
        return {Key<T>(),
            [](std::string_view, std::any&) { return false; },
            [](const MainThread& main, const PropertyDesc&, const tinyxml2::XMLNode& node, std::any& output, std::string_view prefix)
            {
                auto* previous = std::any_cast<T>(&output); if (!previous) return false;
                T copy = *previous; auto object = ObjectRef::From(copy);
                std::vector<Pending> pending;
                if (!Stage(main, object, TypeOf<T>(), node, pending, prefix).Ok()) return false;
                for (const auto& entry : pending) if (entry.property->WriteValue(main, object, entry.value) != Error::None) return false;
                output = std::move(copy); return true;
            },
            [](const MainThread& main, const PropertyDesc&, tinyxml2::XMLDocument& doc, tinyxml2::XMLNode& node, const std::any& input, std::string_view prefix)
            {
                auto* value = std::any_cast<T>(&input);
                return value && Serialize(main, ObjectRef::From(*value), TypeOf<T>(), doc, node, {}, prefix).Ok();
            },
            [](const MainThread& main, std::any& input)
            {
                if (!main.IsCurrent()) return false;
                auto* value = std::any_cast<T>(&input); if (!value) return false;
                auto object = ObjectRef::From(*value);
                for (auto* property : Properties(TypeOf<T>(), "Serialize")) if (auto* codec = FindValueCodec(*property))
                {
                    std::any child;
                    if (property->ReadValue(main, object, child) != Error::None || !codec->resolve(main, child) ||
                        property->WriteValue(main, object, child) != Error::None) return false;
                }
                return true;
            },
            &TypeOf<T>(),
            [](std::any& value) -> std::optional<ObjectRef>
            { auto* record = std::any_cast<T>(&value); return record ? std::optional{ObjectRef::From(*record)} : std::nullopt; }};
    }
}
