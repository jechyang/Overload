#pragma once
#include <OvEditor/Panels/InspectorPropertyContext.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvUI/Widgets/Drags/DragSingleScalar.h>
#include <OvUI/Widgets/Drags/DragMultipleScalars.h>
#include <OvUI/Widgets/Selection/ComboBox.h>
#include <OvUI/Widgets/Texts/TextColored.h>
#include <OvUI/Widgets/Layout/GroupCollapsable.h>
#include <OvUI/Types/Color.h>
#include <imgui_internal.h>
#include <optional>

namespace OvEditor::Panels::InspectorBindings
{
    inline std::string ChildPropertyPath(OvUI::Widgets::AWidget& widget, const std::string& key)
    {
        const auto* group = dynamic_cast<OvUI::Widgets::Layout::GroupCollapsable*>(&widget);
        return key+"/"+(group ? group->name+"/" : "");
    }

    struct PropertyActions
    {
        std::string title;
        std::function<bool()> copy, writable, canPaste, canReset, paste, reset, modified;
        bool hasDefault = false;
    };

    class PropertyMenu final : public OvUI::Plugins::IPlugin
    {
    public:
        PropertyMenu(std::shared_ptr<PropertyActions> actions, PropertyContext context, bool label)
            : actions(std::move(actions)), m_context(std::move(context)), m_label(label) {}
        std::shared_ptr<PropertyActions> actions;
        void Execute(OvUI::Plugins::EPluginExecutionContext) override
        {
            const auto item = GImGui->LastItemData;
            if (m_label && actions->modified())
            {
                ImGui::GetWindowDrawList()->AddCircleFilled({item.Rect.Min.x-6*OVUI_SCALE,
                    (item.Rect.Min.y+item.Rect.Max.y)*.5f},2*OVUI_SCALE,IM_COL32(105,170,240,255));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Differs from component defaults. Right-click to copy, paste or reset this property.");
            }
            ImGui::PushID(this);
            if (ImGui::BeginPopupContextItem("property"))
            {
                ImGui::TextDisabled("%s",actions->title.c_str()); ImGui::Separator();
                if (ImGui::MenuItem("Copy value")) actions->copy();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy the value from the primary inspected entity");
                if (ImGui::MenuItem("Paste value",nullptr,false,actions->canPaste()))
                    m_context.defer([actions = actions] { actions->paste(); });
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !actions->canPaste())
                    ImGui::SetTooltip(actions->writable() ? "Copy a compatible property value first" : "This property is read-only");
                ImGui::Separator();
                if (ImGui::MenuItem("Reset to default",nullptr,false,actions->canReset()))
                    m_context.defer([actions = actions] { actions->reset(); });
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(!actions->writable() ? "This property is read-only" : !actions->hasDefault ? "No default value available" :
                        !actions->modified() ? "Already at default" : "Restore only this property on all inspected entities");
                ImGui::EndPopup();
            }
            ImGui::PopID(); GImGui->LastItemData = item;
        }
    private:
        PropertyContext m_context;
        bool m_label;
    };

    template<class T> T PropertyBound(T value, OvUI::Widgets::AWidget& widget)
    {
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T,bool>)
            if (auto* field = dynamic_cast<OvUI::Widgets::Drags::DragSingleScalar<T>*>(&widget))
                return std::clamp(value,field->min,field->max);
        return value;
    }
    template<class T, size_t N> std::array<T,N> PropertyBound(std::array<T,N> value, OvUI::Widgets::AWidget& widget)
    {
        if (auto* field = dynamic_cast<OvUI::Widgets::Drags::DragMultipleScalars<T,N>*>(&widget))
            for (auto& axis : value) axis = std::clamp(axis,field->min,field->max);
        return value;
    }
    template<class T> std::map<int,std::string> PropertyChoices(OvUI::Widgets::AWidget& widget)
    {
        if constexpr (std::is_same_v<T,int>)
            if (auto* field = dynamic_cast<OvUI::Widgets::Selection::ComboBox*>(&widget)) return field->choices;
        return {};
    }

    template<class T> void AttachPropertyMenu(const std::vector<OvUI::Widgets::AWidget*>& widgets,
        std::shared_ptr<std::vector<OvUI::Plugins::DataDispatcher<T>>> bindings,
        OvUI::Widgets::Texts::Text* label, std::weak_ptr<const bool> ticket,
        PropertyContext context, const std::string& path)
    {
        if (!context.clipboard || !context.defer) return;
        auto* widget = widgets.front();
        auto actions = std::make_shared<PropertyActions>(); actions->title = label ? label->content : "Property";
        std::optional<T> initial;
        if (context.defaults)
            if (auto it = context.defaults->find(path+"/"+(label ? label->content : "")); it != context.defaults->end())
                if (const auto* value = std::any_cast<T>(&it->second)) initial = *value;
        const auto usable = [ticket,widget] { return !ticket.expired() && widget->enabled && widget->visible && !widget->disabled; };
        actions->writable = usable;
        actions->hasDefault = initial.has_value();
        actions->copy = [bindings,ticket,widget,clipboard = context.clipboard] {
            if (ticket.expired()) return false;
            clipboard->value = bindings->front().Gather(); clipboard->choices = PropertyChoices<T>(*widget); return true;
        };
        actions->canPaste = [usable,widget,clipboard = context.clipboard] {
            return usable() && clipboard->value.type() == typeid(T) && clipboard->choices == PropertyChoices<T>(*widget);
        };
        actions->modified = [bindings,ticket,initial] {
            if (ticket.expired() || !initial) return false;
            for (auto& binding : *bindings) { auto value = binding.Gather(); if (!(value == *initial)) return true; }
            return false;
        };
        actions->canReset = [usable,modified = actions->modified] { return usable() && modified(); };
        const auto apply = [bindings,widgets,usable,refresh = context.refresh,title = actions->title](T value, const char* verb) {
            if (!usable()) return false;
            EDITOR_EXEC(CommitSceneEdit(true)); EDITOR_EXEC(BeginSceneEdit(std::string(verb)+" "+title));
            for (size_t i=0;i<bindings->size();++i)
            {
                if (!usable()) break;
                auto edited = PropertyBound(value,*widgets[i]);
                if constexpr (std::is_same_v<T,int>)
                    if (auto* combo = dynamic_cast<OvUI::Widgets::Selection::ComboBox*>(widgets[i])) combo->ValueChangedEvent.Invoke(edited);
                auto& binding = (*bindings)[i]; binding.NotifyChange(); binding.Provide(edited);
            }
            if (refresh) refresh();
            EDITOR_EXEC(CommitSceneEdit(true)); return true;
        };
        actions->paste = [canPaste = actions->canPaste,clipboard = context.clipboard,apply] {
            return canPaste() && apply(std::any_cast<T>(clipboard->value),"Paste");
        };
        actions->reset = [initial,apply] { return initial && apply(*initial,"Reset"); };
        if (label) label->AddPlugin<PropertyMenu>(actions,context,true);
        widget->AddPlugin<PropertyMenu>(actions,std::move(context),false);
    }

    template<class T> bool CapturePropertyDefault(OvUI::Widgets::AWidget& widget, PropertyDefaults& defaults, const std::string& path)
    {
        auto* binding = widget.GetPlugin<OvUI::Plugins::DataDispatcher<T>>();
        if (!binding || !binding->IsWritable()) return false;
        defaults[path] = binding->Gather(); return true;
    }
    inline void CapturePropertyDefaults(OvUI::Internal::WidgetContainer& root, PropertyDefaults& defaults,
        const std::string& path = "", std::string title = "")
    {
        size_t i = 0;
        for (auto [widget,mode] : root.GetWidgets())
        {
            const auto key = path+std::to_string(i++);
            if (auto* label = dynamic_cast<OvUI::Widgets::Texts::TextColored*>(widget)) { title = label->content; continue; }
            if (auto* children = dynamic_cast<OvUI::Internal::WidgetContainer*>(widget))
            { CapturePropertyDefaults(*children,defaults,ChildPropertyPath(*widget,key),title); continue; }
            const auto identity = key+"/"+title;
            (void)(CapturePropertyDefault<bool>(*widget,defaults,identity) || CapturePropertyDefault<int>(*widget,defaults,identity) ||
                CapturePropertyDefault<unsigned int>(*widget,defaults,identity) || CapturePropertyDefault<int64_t>(*widget,defaults,identity) ||
                CapturePropertyDefault<uint64_t>(*widget,defaults,identity) || CapturePropertyDefault<float>(*widget,defaults,identity) ||
                CapturePropertyDefault<double>(*widget,defaults,identity) || CapturePropertyDefault<std::string>(*widget,defaults,identity) ||
                CapturePropertyDefault<OvUI::Types::Color>(*widget,defaults,identity) || CapturePropertyDefault<std::array<float,2>>(*widget,defaults,identity) ||
                CapturePropertyDefault<std::array<float,3>>(*widget,defaults,identity) || CapturePropertyDefault<std::array<float,4>>(*widget,defaults,identity));
        }
    }
}
