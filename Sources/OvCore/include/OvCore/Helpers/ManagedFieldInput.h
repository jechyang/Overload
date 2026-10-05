#pragma once
#include <imgui.h>
#include <OvCore/Scripting/Common/ManagedField.h>
#include <OvUI/Widgets/AWidget.h>
#include <OvTools/Eventing/Event.h>
#include <functional>
#include <map>
#include <OvUI/Widgets/Layout/ReorderableList.h>

namespace OvCore::Helpers
{
    // Typed controls keep integers in their native domain, including all 64 bits.
    class ManagedFieldInput final : public OvUI::Widgets::AWidget
    {
    public:
        std::string fieldId;
        std::string displayName;
        bool composite=false;
        std::function<bool()> overridden;
        std::function<std::optional<Scripting::ManagedFieldValue>()> read;
        std::function<void()> reset, pick;
        std::function<std::string(const Scripting::ManagedFieldValue&)> referenceLabel;
        OvTools::Eventing::Event<Scripting::ManagedFieldValue> ValueChangedEvent;
        const OvUI::Widgets::Layout::ReorderableList* CollectionUI(const std::string& path="root") const
        { auto it=m_lists.find(path); return it==m_lists.end()?nullptr:&it->second; }
    protected:
        void _Draw_Impl() override;
    private:
        std::string m_text;
        bool m_textActive = false;
        std::map<std::string,OvUI::Widgets::Layout::ReorderableList> m_lists;
        std::string m_compositeSeen,m_compositeError;
        ImGuiID m_activeCompositeInput=0;
        bool DrawComposite(Scripting::ManagedFieldValue&,const std::string&,const std::string&,int=0);
        bool DrawCompositeScalar(Scripting::ManagedFieldValue&);
        static int ResizeText(ImGuiInputTextCallbackData* data);
    };
}
