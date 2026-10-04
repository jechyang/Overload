#pragma once
#include <OvEditor/Panels/Inspector.h>
#include <OvUI/Modules/Canvas.h>
#include <fstream>

static void EditorInspectorChecks(OvEditor::Core::Context& context, FieldBehaviour& behaviour)
{
    auto& ui=*context.uiManager;
    OvUI::Settings::PanelWindowSettings settings;
    settings.dockable=false;
    OvEditor::Panels::Inspector inspector("Inspector",true,settings,
        {[](auto action){action();},[](auto&){},[](auto&){}});
    inspector.autoSize=false;inspector.SetPosition({0,0});inspector.SetSize({500,900});
    inspector.FocusActor(behaviour.owner);
    OvUI::Modules::Canvas canvas;canvas.AddPanel(inspector);ui.SetCanvas(canvas);
    ui.EnableEditorLayoutSave(false);
    auto clear=[] {glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);glClearColor(.08f,.08f,.08f,1);glClear(GL_COLOR_BUFFER_BIT);};
    for(int i=0;i<3;++i) {
        if(context.renderThread) {
            auto commands=ui.RecordRenderCommands();
            context.renderThread->Invoke([&](auto& driver){clear();commands.Execute(driver);});
        } else {clear();ui.Render();}
    }
    auto capture=[&] {
        const auto [width,height]=context.window->GetFramebufferSize();
        std::vector<uint8_t> pixels(size_t(width)*height*4);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        const auto file=context.renderThread?"inspector-thread.ppm":"inspector.ppm";
        std::ofstream image(file,std::ios::binary);image<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for(int y=height-1;y>=0;--y)for(int x=0;x<width;++x)image.write(reinterpret_cast<const char*>(pixels.data()+(size_t(y)*width+x)*4),3);
        Require(image.good(),"typed Inspector renders and captures with real ImGui controls");
    };
    if(context.renderThread)context.renderThread->Invoke([&](auto&){capture();});else capture();
    ui.RemoveCanvas();
}
