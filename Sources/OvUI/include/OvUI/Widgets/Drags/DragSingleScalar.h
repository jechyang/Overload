/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <imgui.h>

#include <OvTools/Eventing/Event.h>

#include <OvUI/Widgets/DataWidget.h>
#include <OvUI/Widgets/Drags/NumericField.h>

namespace OvUI::Widgets::Drags
{
	/**
	* Drag widget of generic type
	*/
	template <typename T>
	class DragSingleScalar : public DataWidget<T>, public INumericField
	{
		static_assert(std::is_scalar<T>::value, "Invalid DragSingleScalar T (Scalar expected)");

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
		DragSingleScalar
		(
			ImGuiDataType p_dataType,
			T p_min,
			T p_max,
			T p_value,
			float p_speed,
			const std::string& p_label,
			const std::string& p_format
		) : DataWidget<T>(value), m_dataType(p_dataType), min(p_min), max(p_max), value(p_value), speed(p_speed) , label(p_label), format(p_format) {}

        void SetMixed(bool mixed) override { m_mixed = mixed; }
        void BeginLabelEdit() override
        {
            auto* data = this->template GetPlugin<OvUI::Plugins::DataDispatcher<T>>();
            if (data && m_text.editing && m_text.dirty && m_text.valid)
            { data->NotifyChange(); data->Provide(m_text.draft); }
            m_text.Discard(); m_labelStart = data ? data->Gather() : value;
        }
        void ScrubLabel(float distance) override
        {
            auto edited = ClampNumeric(static_cast<long double>(m_labelStart)+distance*speed,min,max);
            if (auto* data = this->template GetPlugin<OvUI::Plugins::DataDispatcher<T>>())
            { data->NotifyChange(); data->Provide(edited); }
        }

	protected:
		void _Draw_Impl() override
		{
			if (max < min)
				max = min;

			if (value < min)
				value = min;
			else if (value > max)
				value = max;

			if (m_text.Draw((label + this->m_widgetID).c_str(),m_dataType,value,speed,min,max,format.c_str(),m_mixed))
			{
				ValueChangedEvent.Invoke(value);
				this->NotifyChange();
			}
            if (ImGui::IsItemHovered() && this->tooltip.empty())
                ImGui::SetTooltip("Click to type; Enter/Tab confirms, Escape cancels. Drag to adjust; Shift: faster, Alt: finer.");
		}

	public:
		T min;
		T max;
		T value;
		float speed;
		std::string label;
		std::string format;
		OvTools::Eventing::Event<T> ValueChangedEvent;

	private:
		ImGuiDataType m_dataType;
        NumericTextState<T> m_text;
        T m_labelStart{};
        bool m_mixed = false;
	};
}
