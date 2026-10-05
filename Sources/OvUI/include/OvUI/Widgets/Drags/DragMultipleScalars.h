/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <array>

#include <OvTools/Eventing/Event.h>

#include "OvUI/Widgets/DataWidget.h"
#include "OvUI/Widgets/Drags/NumericField.h"
#include <OvUI/Styling/Style.h>

namespace OvUI::Widgets::Drags
{
	/**
	* Drag widget of multiple generic type
	*/
	template <typename T, size_t _Size>
	class DragMultipleScalars : public DataWidget<std::array<T, _Size>>
	{
		static_assert(_Size > 1, "Invalid DragMultipleScalars _Size (2 or more requiered)");

	public:
		/**
		* Constructor
		* @param p_dataType
		* @param p_min
		* @param p_max
		* @param p_value
		* @param p_speed
		* @param p_label
		* @param p_format
		*/
		DragMultipleScalars
		(
			ImGuiDataType_ p_dataType,
			T p_min,
			T p_max,
			T p_value,
			float p_speed,
			const std::string& p_label,
			const std::string& p_format
		) : DataWidget<std::array<T, _Size>>(values), m_dataType(p_dataType), min(p_min), max(p_max), speed(p_speed), label(p_label), format(p_format)
		{
			values.fill(p_value);
		}

        void Draw() override
        {
            editedAxes.fill(false); proportionalEdit = false;
            DataWidget<std::array<T,_Size>>::Draw();
            editedAxes.fill(false); proportionalEdit = false;
        }
        std::array<T,_Size> ProportionalValue(std::array<T,_Size> current, size_t axis, T edited) const
        {
            if (current[axis] != T{})
            {
                const auto ratio = static_cast<long double>(edited)/current[axis];
                for (auto& value : current) value = ClampNumeric(value*ratio,min,max);
            }
            current[axis] = edited; return current;
        }

	protected:
		void _Draw_Impl() override
		{
			if (max < min)
				max = min;

            const float width = ImGui::CalcItemWidth(), gap = ImGui::GetStyle().ItemInnerSpacing.x;
            const float axisWidth = 20*OVUI_SCALE, linkWidth = showProportionalLock ? 26*OVUI_SCALE+gap : 0;
            const bool stacked = (width-linkWidth-gap*(_Size-1))/_Size < 58*OVUI_SCALE;
            const float componentWidth = stacked ? width-linkWidth : (width-linkWidth-gap*(_Size-1))/_Size;
            const bool linked = proportionalLink ? *proportionalLink : proportional;
            bool changed = false;
            ImGui::BeginGroup(); ImGui::PushID(this->m_widgetID.c_str());
            const char* axes[]{"X","Y","Z","W"};
            const ImVec4 colors[]{{.48f,.16f,.16f,1},{.18f,.38f,.18f,1},{.16f,.28f,.50f,1},{.35f,.25f,.42f,1}};
            for (size_t i=0;i<_Size;++i)
            {
                if (i && !stacked) ImGui::SameLine(0,gap);
                ImGui::PushID(static_cast<int>(i));
                if (axisLabels)
                {
                    ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);
                    ImGui::PushStyleColor(ImGuiCol_Button,colors[i%4]);
                    const bool reset = ImGui::Button(axes[i%4],{axisWidth,0});
                    ImGui::PopStyleColor(); ImGui::PopItemFlag();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag %s to adjust. Click to reset to %g. Shift: faster. Alt: finer.",axes[i%4],double(axisResetValue));
                    if (ImGui::IsItemActivated()) { m_axisStart[i] = values[i]; m_axisDistance[i] = 0; m_axisDragged[i] = false; }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
                    {
                        m_axisDragged[i] = true;
                        m_axisDistance[i] += NumericDragDelta();
                        ApplyAxis(i,ClampNumeric(static_cast<long double>(m_axisStart[i])+m_axisDistance[i]*speed,min,max),linked);
                        changed = true;
                    }
                    if (reset && !m_axisDragged[i]) { ApplyAxis(i,std::clamp(axisResetValue,min,max),false); changed = true; }
                    ImGui::SameLine(0,2*OVUI_SCALE);
                }
                ImGui::SetNextItemWidth(std::max(24*OVUI_SCALE,componentWidth-(axisLabels ? axisWidth+2*OVUI_SCALE : 0)));
                T edited = values[i];
                if (m_text[i].Draw("##value",m_dataType,edited,speed,min,max,format.c_str(),mixedAxes[i]))
                { ApplyAxis(i,edited,linked); changed = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s. Click to type; Enter/Tab confirms, Escape cancels. Drag to adjust; Shift: faster, Alt: finer.",
                    axisLabels ? axes[i%4] : "Value",mixedAxes[i] ? " has mixed values" : "");
                ImGui::PopID();
            }
            if (showProportionalLock)
            {
                ImGui::SameLine(0,gap); ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);
                if (ImGui::Button("##Link",{26*OVUI_SCALE,0}))
                { if (proportionalLink) *proportionalLink = !linked; else proportional = !linked; }
                ImGui::PopItemFlag();
                const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
                const ImVec2 center{(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f};
                const auto color = ImGui::GetColorU32(linked ? ImVec4{.95f,.75f,.25f,1} : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                const float s = OVUI_SCALE, end = linked ? s : -s;
                auto* list = ImGui::GetWindowDrawList();
                list->AddRect({center.x-8*s,center.y-3*s},{center.x+end,center.y+3*s},color,3*s,0,1.5f*s);
                list->AddRect({center.x-end,center.y-3*s},{center.x+8*s,center.y+3*s},color,3*s,0,1.5f*s);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(linked ? "Proportions locked. Each selected entity keeps its own scale ratio." : "Lock scale proportions");
            }
            ImGui::PopID(); ImGui::EndGroup();
			if (changed)
			{
				ValueChangedEvent.Invoke(values);
				this->NotifyChange();
			}
		}

	public:
		T min;
		T max;
		float speed;
		std::array<T, _Size> values;
		std::string label;
		std::string format;
        bool axisLabels = false;
        T axisResetValue{};
        bool showProportionalLock = false, proportional = false;
        bool* proportionalLink = nullptr;
        std::array<bool,_Size> mixedAxes{}, editedAxes{};
        bool proportionalEdit = false;
		OvTools::Eventing::Event<std::array<T, _Size>&> ValueChangedEvent;

	protected:
		ImGuiDataType_ m_dataType;
        std::array<NumericTextState<T>,_Size> m_text;
        std::array<T,_Size> m_axisStart{};
        std::array<float,_Size> m_axisDistance{};
        std::array<bool,_Size> m_axisDragged{};
        void ApplyAxis(size_t axis, T edited, bool linked)
        {
            if (linked) values = ProportionalValue(values,axis,edited);
            else values[axis] = edited;
            editedAxes[axis] = true; proportionalEdit = linked;
        }
	};
}
