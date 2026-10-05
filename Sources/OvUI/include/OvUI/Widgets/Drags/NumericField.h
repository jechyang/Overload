#pragma once
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <imgui_internal.h>
#include <OvUI/Widgets/AWidget.h>

namespace OvUI::Widgets::Drags
{
    inline float NumericDragDelta()
    {
        const auto& io = ImGui::GetIO();
        return io.MouseDelta.x * (io.KeyShift ? 10.f : 1.f) * (io.KeyAlt ? .01f : 1.f);
    }

    template<class T> T ClampNumeric(long double value, T min, T max)
    {
        if (value <= static_cast<long double>(min)) return min;
        if (value >= static_cast<long double>(max)) return max;
        return static_cast<T>(value);
    }

    // Typed edits are drafts until Enter, Tab or a focus change. Escape never
    // writes a representative value back onto a mixed selection.
    template<class T> struct NumericTextState
    {
        T draft{};
        bool editing = false, dirty = false, valid = true;
        int lastFrame = -1;
        void Discard() { editing = dirty = false; }
        bool Draw(const char* label, ImGuiDataType type, T& value, float speed,
            T min, T max, const char* format, bool mixed = false)
        {
            const auto id = ImGui::GetID(label);
            if (lastFrame != ImGui::GetFrameCount()-1) Discard();
            lastFrame = ImGui::GetFrameCount();
            const bool wasInput = ImGui::TempInputIsActive(id);
            const bool cancel = (wasInput || editing) && ImGui::IsKeyPressed(ImGuiKey_Escape);
            const bool disabled = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
            if (disabled) Discard();
            T candidate = editing ? draft : value;
            auto& io = ImGui::GetIO(); const bool clickToInput = io.ConfigDragClickToInputText;
            io.ConfigDragClickToInputText = true;
            const bool changed = ImGui::DragScalar(label,type,&candidate,speed,&min,&max,
                mixed && GImGui->ActiveId != id ? "--" : format,ImGuiSliderFlags_AlwaysClamp);
            io.ConfigDragClickToInputText = clickToInput;
            const bool input = ImGui::TempInputIsActive(id);
            if (wasInput || input || editing)
            {
                if (!editing) { dirty = false; valid = true; }
                editing = true; draft = candidate;
                if (auto* text = ImGui::GetInputTextState(id))
                {
                    dirty |= text->EditedBefore;
                    char* end = nullptr;
                    const auto number = std::strtold(text->GetText(),&end);
                    while (end && std::isspace(static_cast<unsigned char>(*end))) ++end;
                    valid = end && end != text->GetText() && !*end && std::isfinite(number);
                    if (valid && (number < min || number > max)) draft = ClampNumeric(number,min,max);
                }
                if (input) return false;
                editing = false;
                if (disabled || cancel || !dirty || !valid) { dirty = false; return false; }
                value = draft; dirty = false; return true;
            }
            if (changed && !disabled && std::isfinite(static_cast<long double>(candidate)))
            { value = candidate; return true; }
            return false;
        }
    };

    class INumericField
    {
    public:
        virtual ~INumericField() = default;
        virtual void BeginLabelEdit() = 0;
        virtual void ScrubLabel(float distance) = 0;
        virtual void SetMixed(bool mixed) = 0;
    };
}

namespace OvUI::Plugins
{
    class NumericLabelDrag final : public IPlugin
    {
    public:
        NumericLabelDrag(Widgets::Drags::INumericField& field, Widgets::AWidget& widget) : m_field(field),m_widget(widget) {}
        void Execute(EPluginExecutionContext) override
        {
            if (!m_widget.enabled || !m_widget.visible || m_widget.disabled) return;
            const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax(), cursor = ImGui::GetCursorPos();
            ImGui::SetCursorScreenPos(min); ImGui::PushID(this);
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);
            ImGui::InvisibleButton("##NumericLabel",{max.x-min.x,max.y-min.y});
            ImGui::PopItemFlag();
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to adjust. Shift: faster. Alt: finer. Click the number to type.");
            if (ImGui::IsItemActivated()) { m_distance = 0; m_field.BeginLabelEdit(); }
            if (ImGui::IsItemActive())
            {
                m_distance += Widgets::Drags::NumericDragDelta();
                if (ImGui::IsMouseDragging(0)) m_field.ScrubLabel(m_distance);
            }
            const auto labelItem = GImGui->LastItemData;
            ImGui::PopID(); ImGui::SetCursorPos(cursor);
            ImGui::Dummy({0,0}); // Register the restored cell boundary with ImGui.
            GImGui->LastItemData = labelItem;
        }
    private:
        Widgets::Drags::INumericField& m_field;
        Widgets::AWidget& m_widget;
        float m_distance = 0;
    };
}
