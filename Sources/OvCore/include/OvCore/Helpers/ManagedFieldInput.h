#pragma once
#include <imgui.h>
#include <OvCore/Scripting/Common/ManagedField.h>
#include <OvUI/Widgets/AWidget.h>
#include <OvTools/Eventing/Event.h>
#include <functional>

namespace OvCore::Helpers
{
    // Typed controls keep integers in their native domain, including all 64 bits.
    class ManagedFieldInput final : public OvUI::Widgets::AWidget
    {
    public:
        std::string fieldId;
        std::function<std::optional<Scripting::ManagedFieldValue>()> read;
        std::function<void()> reset, pick;
        std::function<std::string(const Scripting::ManagedFieldValue&)> referenceLabel;
        OvTools::Eventing::Event<Scripting::ManagedFieldValue> ValueChangedEvent;
    protected:
        void _Draw_Impl() override;
    private:
        std::string m_text;
        bool m_textActive = false;
        static int ResizeText(ImGuiInputTextCallbackData* data);
    };
}
