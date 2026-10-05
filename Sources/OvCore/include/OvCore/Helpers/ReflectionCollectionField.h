#pragma once
#include <OvCore/Helpers/ReflectionDrawer.h>
#include <OvTools/Utils/CallbackScope.h>
#include <OvUI/Widgets/DataWidget.h>

namespace OvCore::Helpers::ReflectionProperties
{
    // Owned snapshots keep clipboard/default/history data independent of widget lifetime.
    struct CollectionValue
    {
        std::any data;
        const PropertyDesc* property = nullptr;
        const CollectionOps* ops = nullptr;
        std::string canonical;
        std::function<bool(std::any&)> patch;
        bool operator==(const CollectionValue& other) const
        { return property && other.property && property->Info().type.key == other.property->Info().type.key && canonical == other.canonical; }
        static CollectionValue From(const MainThread&, const PropertyDesc&, std::any);
        CollectionValue Merge(const MainThread&, const CollectionValue& current) const;
    };
    class CollectionField final : public OvUI::Widgets::DataWidget<CollectionValue>, public OvUI::Internal::WidgetContainer
    {
    public:
        CollectionField(Binding, const Registry&, std::span<const DrawerHook>);
        void SetMixedShape(bool value) { m_mixedShape = value; }
        bool MixedShape() const { return m_mixedShape; }
        void SetMixedValues(bool value) { m_mixedValues=value; }
        bool MixedValues() const { return m_mixedValues; }
        const std::string& Name() const { return m_name; }
        bool expandForSearch = false;
        // The same guarded operations back UI controls and automation.
        bool Resize(size_t);
        bool Erase(size_t);
        bool Move(size_t,size_t);
        bool Duplicate(size_t);
    protected:
        void _Draw_Impl() override;
    private:
        bool Edit(std::function<bool(std::any&)>, bool structural);
        void Rebuild();
        Binding m_binding;
        const Registry& m_registry;
        std::vector<DrawerHook> m_hooks;
        CollectionValue m_value;
        std::string m_name, m_seen;
        OvTools::Utils::CallbackScope m_scope, m_items;
        size_t m_page = 0;
        std::array<char,32> m_sizeText{};
        size_t m_lastSize = size_t(-1);
        bool m_rebuild = false, m_mixedShape = false;
        bool m_mixedValues = false, m_sizeInvalid = false;
    };
}
