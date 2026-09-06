#pragma once
#undef APIENTRY
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

template<class Product,class Context>
void WindowMinimizeValidation(Product& editor,Context& context)
{
    auto* window=context.window->GetGlfwWindow();
    const auto native=glfwGetWin32Window(window);
    const auto style=GetWindowLongPtrW(native,GWL_EXSTYLE);
    const auto foreground=GetForegroundWindow();
    // Keep the test window off screen, off the taskbar and non-activating.
    SetWindowLongPtrW(native,GWL_EXSTYLE,style|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW);
    context.window->SetPosition(-30000,-30000);
    struct RestoreHidden{OvWindowing::Window& window;HWND native;LONG_PTR style;~RestoreHidden(){window.Hide();SetWindowLongPtrW(native,GWL_EXSTYLE,style);}} restore{*context.window,native,style};
    Require(!context.window->IsMinimized(),"normal hidden window reported minimized");
    for(int cycle=0;cycle<6;++cycle)
    {
        editor.Update(.02f);editor.PostUpdate();
        context.window->Minimize();context.device->PollEvents();
        Require(context.window->IsMinimized()&&glfwGetWindowAttrib(window,GLFW_ICONIFIED),"actual minimize state was not published");
        for(int frame=0;frame<3;++frame){editor.PreUpdate();editor.Update(.02f);editor.PostUpdate();}
        context.window->Restore();context.window->Hide();context.device->PollEvents();
        Require(!context.window->IsMinimized()&&!glfwGetWindowAttrib(window,GLFW_ICONIFIED),"actual restore state remained minimized");
        for(int frame=0;frame<3;++frame){editor.PreUpdate();editor.Update(.02f);editor.PostUpdate();}
    }
    editor.DrainRenderFrames();
    Require(GetForegroundWindow()==foreground,"hidden minimize test changed foreground window");
    std::puts("PASS: six actual off-screen non-activating window minimize/restore cycles with in-flight product frames");
}
