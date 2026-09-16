#include <OvEditor/Panels/CSharpScripts.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvUI/Widgets/Buttons/Button.h>
#include <OvUI/Widgets/Selection/CheckBox.h>
namespace OvEditor::Panels
{
    CSharpScripts::CSharpScripts(const std::string& title,bool opened,const OvUI::Settings::PanelWindowSettings& settings):PanelWindow(title,opened,settings)
    {
        using namespace OvUI::Widgets;
        CreateWidget<Buttons::Button>("New C# script...").ClickedEvent += [] {
            const auto file=EDITOR_EXEC(CreateScript(EDITOR_CONTEXT(projectAssetsPath).string(),"NewBehaviour",true));
            if(!file.empty())EDITOR_EXEC(OpenInCodeEditor(file));
        };
        for(const std::string configuration:{"Debug","Release"})
            CreateWidget<Buttons::Button>("Build / Reload C# ("+configuration+")").ClickedEvent += [configuration]{EDITOR_CONTEXT(csharpProject)->RequestBuild(configuration);};
        CreateWidget<Selection::CheckBox>(true,"Watch source files").ValueChangedEvent += [](bool enabled){EDITOR_CONTEXT(csharpProject)->watch=enabled;};
        CreateWidget<Texts::Text>("Compile errors include source file, line and column. New assemblies apply after Stop.\nAttach a compiled C# type through Inspector > Add Component.");
        m_status=&CreateWidget<Texts::Text>("");m_diagnostics=&CreateWidget<Texts::Text>("");
    }
    void CSharpScripts::_Draw_Impl()
    {
        m_status->content=EDITOR_CONTEXT(csharpProject)->Status();
        m_diagnostics->content=EDITOR_CONTEXT(csharpProject)->Diagnostics();
        PanelWindow::_Draw_Impl();
    }
}
