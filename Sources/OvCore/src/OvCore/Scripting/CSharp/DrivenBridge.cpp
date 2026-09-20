#include <OvCore/Scripting/CSharp/DrivenBridge.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/ECS/Actor.h>
#include <OvCore/UI/DrivenProperties.h>
#include <charconv>

namespace OvCore::Scripting::CSharp
{
    namespace {
        uint64_t Number(const std::string& text) {
            uint64_t value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            OvReflection::Script::Require(error == std::errc{} && end == text.data() + text.size());
            return value;
        }
    }
    void QueryDriven(OvReflection::Script::State& state, uint64_t managedSession, uint64_t handle, uint32_t operation,
        uint64_t argument, const std::string& text, uint64_t& output)
    {
        using namespace OvReflection::Script;
        auto& rect = *state.Get<ECS::Components::CRectTransform>(handle);
        auto& driven = rect.GetDrivenProperties();
        Require(managedSession != 0);
        const auto session = managedSession;
        try {
            if (operation == 60) {
                Require(rect.owner.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
                Require(argument > 0 && argument <= UI::DrivenProperties::All);
                const auto token = text.empty() ? NextIdentity() : Number(text);
                Require(text.empty() || driven.Owns(token, session));
                output = driven.Acquire(rect.GetLayout(), uint32_t(argument), session, token);
            } else if (operation == 61) {
                const auto mask = text.empty() ? UI::DrivenProperties::All : Number(text);
                Require(mask <= UI::DrivenProperties::All);
                auto layout = rect.GetLayout();
                driven.Release(layout, argument, uint32_t(mask), session);
                rect.SetAnchorMin(layout.anchorMin); rect.SetAnchorMax(layout.anchorMax);
                rect.SetPivot(layout.pivot); rect.SetSizeDelta(layout.sizeDelta);
                rect.SetAnchoredPosition(layout.anchoredPosition);
                output = driven.Mask();
            } else if (operation == 62) { output = driven.Mask(); }
            else Require(false);
        } catch (const std::invalid_argument&) { throw Failure{OV_SCRIPT_INVALID_ARGUMENT}; }
    }
}
