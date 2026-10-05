#pragma once

#include <OvEditor/Core/EditorActions.h>
#include <OvUI/Widgets/Texts/TextColored.h>
#include <OvUI/Widgets/InputFields/InputText.h>
#include <OvUI/Widgets/InputFields/AssetField.h>
#include <OvUI/Widgets/InputFields/ActorField.h>
#include <OvUI/Types/Color.h>
#include <OvUI/Widgets/Drags/DragMultipleScalars.h>
#include <array>
#include <typeinfo>

namespace OvEditor::Panels::InspectorBindings
{
    // Binding replicas retain each component's native setter/validation logic.
    // Unsupported custom controls stay read-only in a multi-selection.
    template<class T> struct Value
    {
        static bool Equal(T a, T b) { return a == b; }
        static T Merge(T, T, T edited, OvUI::Widgets::AWidget*) { return edited; }
        static void MixedAxes(OvUI::Widgets::AWidget*, const std::vector<T>&) {}
    };
    template<class T, size_t N> struct Value<std::array<T, N>>
    {
        static bool Equal(const std::array<T, N>& a, const std::array<T, N>& b) { return a == b; }
        static std::array<T, N> Merge(std::array<T, N> current, const std::array<T, N>& before,
            const std::array<T, N>& edited, OvUI::Widgets::AWidget* widget)
        {
            if (auto* field = dynamic_cast<OvUI::Widgets::Drags::DragMultipleScalars<T,N>*>(widget))
                for (size_t i=0;i<N;++i) if (field->editedAxes[i])
                {
                    if (field->proportionalEdit) return field->ProportionalValue(current,i,edited[i]);
                    // An explicit axis reset/type applies even when the primary
                    // actor already has this value and secondary actors differ.
                    current[i] = edited[i];
                }
            // Changing X must preserve each target's unrelated Y/Z values.
            for (size_t i = 0; i < N; ++i) if (before[i] != edited[i]) current[i] = edited[i];
            return current;
        }
        static void MixedAxes(OvUI::Widgets::AWidget* widget, const std::vector<std::array<T,N>>& values)
        {
            if (auto* field = dynamic_cast<OvUI::Widgets::Drags::DragMultipleScalars<T,N>*>(widget))
            {
                field->mixedAxes.fill(false);
                for (const auto& value : values) for (size_t i=0;i<N;++i)
                    field->mixedAxes[i] = field->mixedAxes[i] || value[i] != values.front()[i];
            }
        }
    };

    inline void Disable(OvUI::Widgets::AWidget& widget)
    {
        widget.disabled = true;
        widget.tooltip = "This custom control must be edited on an individual entity.";
        if (auto* children = dynamic_cast<OvUI::Internal::WidgetContainer*>(&widget))
            for (auto [child, mode] : children->GetWidgets()) Disable(*child);
    }

    template<class T> bool Bind(const std::vector<OvUI::Widgets::AWidget*>& widgets,
        OvUI::Widgets::Texts::Text* label, std::weak_ptr<const bool> ticket)
    {
        using Dispatcher = OvUI::Plugins::DataDispatcher<T>;
        auto* primary = widgets.front()->GetPlugin<Dispatcher>();
        if (!primary || !primary->IsWritable()) return false;
        auto copies = std::make_shared<std::vector<Dispatcher>>();
        for (auto* widget : widgets)
        {
            auto* dispatcher = widget->GetPlugin<Dispatcher>();
            if (!dispatcher || !dispatcher->IsWritable()) return false;
            copies->push_back(*dispatcher);
        }
        const auto title = label ? label->content : std::string{};
        auto before = std::make_shared<T>(primary->Gather());
        auto* widget = widgets.front();
        if (auto* numeric = dynamic_cast<OvUI::Widgets::Drags::INumericField*>(widget); numeric && label)
            label->AddPlugin<OvUI::Plugins::NumericLabelDrag>(*numeric,*widget);
        primary->ReplaceBinding(
            [copies, before, label, title, ticket, widget]() -> T
            {
                if (ticket.expired()) return {};
                std::vector<T> values;
                for (auto& binding : *copies) values.push_back(binding.Gather());
                *before = values.front();
                bool mixed = false;
                for (const auto& value : values) mixed |= !Value<T>::Equal(*before,value);
                Value<T>::MixedAxes(widget,values);
                if (auto* numeric = dynamic_cast<OvUI::Widgets::Drags::INumericField*>(widget)) numeric->SetMixed(mixed);
                if (label) label->content = title + (mixed ? " (Mixed)" : "");
                if (copies->size() > 1) widget->tooltip = mixed
                    ? "Mixed values. Editing applies to all selected entities. Unedited vector axes are preserved."
                    : "Editing applies to all selected entities.";
                return *before;
            },
            [copies, before, ticket, widget](T edited)
            {
                if (ticket.expired()) return;
                EDITOR_EXEC(BeginSceneEdit(copies->size() > 1 ? "Edit selection properties" : "Edit properties"));
                for (auto& binding : *copies)
                {
                    if (ticket.expired()) break;
                    auto value = copies->size() > 1 ? Value<T>::Merge(binding.Gather(), *before, edited,widget) : edited;
                    binding.NotifyChange(); binding.Provide(value);
                }
            });
        return true;
    }

    inline bool SameShape(const std::vector<OvUI::Internal::WidgetContainer*>& roots)
    {
        auto& first = roots.front()->GetWidgets();
        for (auto* root : roots)
        {
            if (root->GetWidgets().size() != first.size()) return false;
            for (size_t i = 0; i < first.size(); ++i)
            {
                auto* left = first[i].first; auto* right = root->GetWidgets()[i].first;
                if (typeid(*left) != typeid(*right)) return false;
                if (auto* title = dynamic_cast<OvUI::Widgets::Texts::TextColored*>(left))
                    if (title->content != static_cast<OvUI::Widgets::Texts::TextColored*>(right)->content) return false;
            }
        }
        return true;
    }

    inline void Connect(const std::vector<OvUI::Internal::WidgetContainer*>& roots,
        std::weak_ptr<const bool> ticket, OvUI::Widgets::Texts::Text* inheritedLabel = nullptr)
    {
        if (!SameShape(roots))
        {
            for (auto [widget, mode] : roots.front()->GetWidgets()) Disable(*widget);
            return;
        }
        auto* label = inheritedLabel;
        for (size_t i = 0; i < roots.front()->GetWidgets().size(); ++i)
        {
            std::vector<OvUI::Widgets::AWidget*> widgets;
            for (auto* root : roots) widgets.push_back(root->GetWidgets()[i].first);
            auto* first = widgets.front();
            if (auto* text = dynamic_cast<OvUI::Widgets::Texts::TextColored*>(first)) { label = text; continue; }
            if (dynamic_cast<OvUI::Internal::WidgetContainer*>(first))
            {
                std::vector<OvUI::Internal::WidgetContainer*> children;
                for (auto* widget : widgets) children.push_back(dynamic_cast<OvUI::Internal::WidgetContainer*>(widget));
                Connect(children, ticket, label); continue;
            }
            // Asset pickers and custom action buttons can bypass dispatchers;
            // never silently apply these to only the primary target.
            if (dynamic_cast<OvUI::Widgets::InputFields::AssetField*>(first) ||
                dynamic_cast<OvUI::Widgets::InputFields::ActorField*>(first))
            {
                if (roots.size() > 1) Disable(*first);
                continue;
            }
            const bool bound = Bind<bool>(widgets, label, ticket) || Bind<int>(widgets, label, ticket) ||
                Bind<unsigned int>(widgets, label, ticket) || Bind<int64_t>(widgets, label, ticket) ||
                Bind<uint64_t>(widgets, label, ticket) || Bind<float>(widgets, label, ticket) || Bind<double>(widgets, label, ticket) ||
                Bind<std::string>(widgets, label, ticket) || Bind<OvUI::Types::Color>(widgets, label, ticket) ||
                Bind<std::array<float, 2>>(widgets, label, ticket) || Bind<std::array<float, 3>>(widgets, label, ticket) ||
                Bind<std::array<float, 4>>(widgets, label, ticket);
            if (!bound && roots.size() > 1 && !dynamic_cast<OvUI::Widgets::Texts::Text*>(first)) Disable(*first);
        }
    }
}
