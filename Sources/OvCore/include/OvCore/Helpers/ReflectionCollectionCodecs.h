#pragma once
#include <OvCore/Helpers/ReflectionProperties.h>
#include <array>
#include <vector>

namespace OvCore::Helpers::ReflectionProperties
{
    template<class T> struct CollectionElement
    {
        static Error Read(const MainThread&, const T& input, T& output) { output = input; return Error::None; }
        static Error Write(const MainThread&, T& output, const T& input) { output = input; return Error::None; }
        static std::any Encode(const T& value)
        {
            if constexpr (std::is_enum_v<T>) return std::to_string(static_cast<std::underlying_type_t<T>>(value));
            else return value;
        }
        static bool Decode(const std::any& input, T& output)
        {
            if constexpr (std::is_enum_v<T>)
            {
                auto* text = std::any_cast<std::string>(&input); if (!text) return false;
                std::underlying_type_t<T> value{};
                auto result = std::from_chars(text->data(), text->data()+text->size(), value);
                if (result.ec != std::errc{} || result.ptr != text->data()+text->size()) return false;
                output = static_cast<T>(value); return true;
            }
            else { auto* value = std::any_cast<T>(&input); if (!value) return false; output = *value; return true; }
        }
    };
    template<class C> ValueCodec MakeCollectionCodec(ValueType elementType)
    {
        using T = typename C::value_type;
        using E = CollectionElement<T>;
        constexpr bool dynamic = requires(C& value) { value.resize(size_t{}); };
        static const PropertyDesc element = PropertyDesc::Make<T,T,&E::Read,&E::Write>(PropertyInfo{
            "$collection.element", "Value", "$collection.element", "field", "public", elementType, false, Thread::Main, {}, {}, {}});
        static const CollectionOps ops{&element, dynamic,
            [](const std::any& value) { auto* c = std::any_cast<C>(&value); return c ? c->size() : size_t{}; },
            [](const std::any& value, size_t index, std::any& output) { auto* c = std::any_cast<C>(&value); if (!c || index >= c->size()) return false; output = E::Encode(T((*c)[index])); return true; },
            [](std::any& value, size_t index, const std::any& input) { auto* c = std::any_cast<C>(&value); T v{}; if (!c || index >= c->size() || !E::Decode(input,v)) return false; (*c)[index] = std::move(v); return true; },
            [](std::any& value, size_t size) { if constexpr (dynamic) { auto* c = std::any_cast<C>(&value); if (!c || size > 4096) return false; c->resize(size); return true; } else return false; },
            [](std::any& value, size_t index) { if constexpr (dynamic) { auto* c = std::any_cast<C>(&value); if (!c || index >= c->size()) return false; c->erase(c->begin()+index); return true; } else return false; },
            [](std::any& value, size_t from, size_t to) { auto* c = std::any_cast<C>(&value); if (!c || from >= c->size() || to >= c->size()) return false; T saved = (*c)[from];
                if (from < to) for (size_t i=from;i<to;++i) (*c)[i] = T((*c)[i+1]);
                else for (size_t i=from;i>to;--i) (*c)[i] = T((*c)[i-1]);
                (*c)[to] = std::move(saved); return true; }};
        return {Key<C>(), [](std::string_view,std::any&) { return false; },
            [](const MainThread& main, const PropertyDesc& property, const tinyxml2::XMLNode& node, std::any& output, std::string_view)
            {
                if (!main.IsCurrent() || !std::any_cast<C>(&output)) return false;
                const auto name = Meta(property.Info().metadata,"ItemName"); if (name.empty()) return false;
                std::vector<const tinyxml2::XMLElement*> items;
                for (auto* child=node.FirstChild();child;child=child->NextSibling())
                {
                    if (auto* item=child->ToElement()) { if (name != item->Name() || items.size() >= CollectionLimit(property)) return false; items.push_back(item); }
                    else if (auto* text=child->ToText(); text && std::string_view(text->Value()).find_first_not_of(" \t\r\n") != std::string_view::npos) return false;
                }
                C copy = *std::any_cast<C>(&output);
                if constexpr (dynamic) copy.resize(items.size());
                else if (items.size() != copy.size()) return false;
                std::any staged = std::move(copy);
                for (size_t i=0;i<items.size();++i) { std::any value; if (!ops.get(staged,i,value) || !ReadValueXML(main,element,*items[i],value) || !ops.set(staged,i,value)) return false; }
                output = std::move(staged); return true;
            },
            [](const MainThread& main,const PropertyDesc& property,tinyxml2::XMLDocument& doc,tinyxml2::XMLNode& node,const std::any& input,std::string_view)
            {
                if (!main.IsCurrent() || !std::any_cast<C>(&input) || ops.size(input) > CollectionLimit(property)) return false;
                const auto name=Meta(property.Info().metadata,"ItemName"); if (name.empty()) return false;
                for (size_t i=0;i<ops.size(input);++i) { auto* item=doc.NewElement(std::string(name).c_str()); node.InsertEndChild(item); std::any value;
                    if (!ops.get(input,i,value) || !WriteValueXML(main,element,doc,*item,value)) return false; }
                return true;
            },
            [](const MainThread& main,std::any& value) { if (!main.IsCurrent() || !std::any_cast<C>(&value)) return false;
                if (auto* codec=FindValueCodec(element)) for (size_t i=0;i<ops.size(value);++i) { std::any child; if (!ops.get(value,i,child) || !codec->resolve(main,child) || !ops.set(value,i,child)) return false; }
                return true; }, nullptr,nullptr,&ops};
    }
}
