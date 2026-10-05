#pragma once
#include <OvCore/Helpers/ReflectionProperties.h>
#include <OvCore/Helpers/GUIDrawer.h>
#include <OvCore/ECS/ComponentRegistry.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/ResourceManagement/ModelManager.h>
#include <OvCore/ResourceManagement/MaterialManager.h>
#include <OvCore/ResourceManagement/SoundManager.h>
#include <OvCore/ResourceManagement/TextureManager.h>
#include <OvCore/ResourceManagement/ShaderManager.h>
#include <OvCore/Resources/ResourceReference.h>
#include <array>
#include <algorithm>
#include <memory>

namespace OvCore::Helpers::ReflectionProperties
{
    inline bool ValidAssetIdentity(std::string_view text)
    {
        // Bound input before resource lookup and reject embedded XML control bytes.
        return text.size() <= 4096 && std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32; });
    }
    template<class T> struct ResourceManagerFor;
    template<> struct ResourceManagerFor<OvRendering::Resources::Model> { using Type = ResourceManagement::ModelManager; };
    template<> struct ResourceManagerFor<Resources::Material> { using Type = ResourceManagement::MaterialManager; };
    template<> struct ResourceManagerFor<OvAudio::Resources::Sound> { using Type = ResourceManagement::SoundManager; };
    template<> struct ResourceManagerFor<OvRendering::Resources::Texture> { using Type = ResourceManagement::TextureManager; };
    template<> struct ResourceManagerFor<OvRendering::Resources::Shader> { using Type = ResourceManagement::ShaderManager; };

    template<class T> struct ResourceCodec;
    template<class T> struct ResourceCodec<Resources::ResourceReference<T>>
    {
        using Value = Resources::ResourceReference<T>;
        static bool Parse(std::string_view text, Value& value)
        {
            if (!ValidAssetIdentity(text)) return false;
            value = {nullptr, text.empty() || text == "?" ? std::string{} : std::string(text)};
            return true;
        }
        static bool Read(const PropertyDesc&, const tinyxml2::XMLElement& node, Value& value)
        { return !node.FirstChildElement() && Parse(node.GetText() ? node.GetText() : "", value); }
        static bool Write(const PropertyDesc&, tinyxml2::XMLDocument&, tinyxml2::XMLElement& node, const Value& value)
        {
            auto path = value.Identity();
            if (!ValidAssetIdentity(path)) return false;
            node.SetText(path.empty() ? "?" : path.c_str());
            return true;
        }
        static void Resolve(Value& value)
        {
            if (!value.path.empty()) value.resource = Global::ServiceLocator::Get<typename ResourceManagerFor<T>::Type>().GetResource(value.path);
        }
    };
    template<class T, size_t N> struct ResourceCodec<std::array<Resources::ResourceReference<T>, N>>
    {
        using Value = std::array<Resources::ResourceReference<T>, N>;
        using Item = Resources::ResourceReference<T>;
        static bool Parse(std::string_view, Value&) { return false; }
        static bool Read(const PropertyDesc& property, const tinyxml2::XMLElement& node, Value& value)
        {
            value = {}; size_t index = 0;
            auto itemName = Meta(property.Info().metadata, "ItemName");
            for (auto* child = node.FirstChild(); child; child = child->NextSibling())
                if (auto* text = child->ToText(); text && std::string_view(text->Value()).find_first_not_of(" \t\r\n") != std::string_view::npos) return false;
            for (const auto* child = node.FirstChildElement(); child; child = child->NextSiblingElement())
            {
                if (child->Name() != itemName || index == N || !ResourceCodec<Item>::Read(property, *child, value[index++])) return false;
            }
            return true;
        }
        static bool Write(const PropertyDesc& property, tinyxml2::XMLDocument& doc, tinyxml2::XMLElement& node, const Value& value)
        {
            size_t count = N;
            while (count && value[count - 1].Identity().empty()) --count;
            std::string itemName(Meta(property.Info().metadata, "ItemName"));
            for (size_t i = 0; i < count; ++i)
            {
                auto* child = doc.NewElement(itemName.c_str()); node.InsertEndChild(child);
                if (!ResourceCodec<Item>::Write(property, doc, *child, value[i])) return false;
            }
            return true;
        }
        static void Resolve(Value& value) { for (auto& item : value) ResourceCodec<Item>::Resolve(item); }
    };
    template<class T> ValueCodec MakeResourceCodec()
    {
        return {Key<T>(),
            [](std::string_view text, std::any& output) { T value{}; if (!ResourceCodec<T>::Parse(text, value)) return false; output = std::move(value); return true; },
            [](const MainThread&, const PropertyDesc& p, const tinyxml2::XMLNode& node, std::any& output, std::string_view) { T value{}; auto* element = node.ToElement(); if (!element || !ResourceCodec<T>::Read(p, *element, value)) return false; output = std::move(value); return true; },
            [](const MainThread&, const PropertyDesc& p, tinyxml2::XMLDocument& doc, tinyxml2::XMLNode& node, const std::any& input, std::string_view) { auto* value = std::any_cast<T>(&input); auto* element = node.ToElement(); return value && element && ResourceCodec<T>::Write(p, doc, *element, *value); },
            [](const MainThread& main, std::any& input) { if (!main.IsCurrent()) return false; auto* value = std::any_cast<T>(&input); if (!value) return false; ResourceCodec<T>::Resolve(*value); return true; }};
    }
    // Reuse the editor asset picker while preserving unresolved paths and guarding
    // callbacks that can outlive a component or Inspector rebuild.
    template<class T, class Getter, class Setter>
    OvUI::Widgets::InputFields::AssetField& DrawResourceReference(OvUI::Internal::WidgetContainer& root, const std::string& name,
        std::weak_ptr<const bool> ticket, Getter get, Setter set, OvTools::Utils::PathParser::EFileType kind)
    {
        const auto* main = &ECS::GetComponentRegistry().Reflection().Main();
        auto read = [main, ticket, get]() -> std::string { return !main->IsCurrent() || ticket.expired() ? std::string{} : get().Identity(); };
        auto write = [main, ticket, set](std::string path)
            {
                if (!main->IsCurrent() || ticket.expired()) return;
                Resources::ResourceReference<T> value;
                if (!ResourceCodec<decltype(value)>::Parse(path, value)) return;
                ResourceCodec<decltype(value)>::Resolve(value); set(value);
            };
        auto& widget = GUIDrawer::DrawAsset(root, name, read, write, kind);
        widget.template GetPlugin<OvUI::Plugins::DataDispatcher<std::string>>()->RegisterProvider(write);
        return widget;
    }
}
