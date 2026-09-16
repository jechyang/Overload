#pragma once
#include <OvUI/Panels/PanelWindow.h>
#include <OvUI/Widgets/Texts/Text.h>
namespace OvEditor::Panels
{
    class CSharpScripts : public OvUI::Panels::PanelWindow
    {
    public:
        CSharpScripts(const std::string& title,bool opened,const OvUI::Settings::PanelWindowSettings& settings);
    protected:
        void _Draw_Impl() override;
    private:
        OvUI::Widgets::Texts::Text* m_status;
        OvUI::Widgets::Texts::Text* m_diagnostics;
    };
}
