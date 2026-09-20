#include <OvWindowing/Inputs/UIPlatformInput.h>
#include <OvWindowing/Window.h>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <imm.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "imm32.lib")

namespace OvWindowing::Inputs
{
    struct UIPlatformInput::State
    {
        Window& window;
        HWND handle;
        static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data)
        {
            auto& self = *reinterpret_cast<State*>(data);
            // Never unwind C++ exceptions through the OS callback stack.
            try
            {
                if (message == WM_IME_COMPOSITION || message == WM_IME_ENDCOMPOSITION)
                {
                    UIInputEvent event{UIInputKind::Composition};
                    if (message == WM_IME_COMPOSITION)
                    {
                        HIMC context = ImmGetContext(hwnd);
                        if (context)
                        {
                            const LONG bytes = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
                            if (bytes >= 0 && bytes <= 8192)
                            {
                                std::wstring text(static_cast<size_t>(bytes)/sizeof(wchar_t), L'\0');
                                ImmGetCompositionStringW(context, GCS_COMPSTR, text.data(), bytes);
                                const LONG cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
                                for (size_t i = 0; i < text.size(); ++i)
                                {
                                    if (static_cast<LONG>(i) < cursor) ++event.compositionCursor;
                                    uint32_t c = text[i];
                                    if (c >= 0xd800 && c <= 0xdbff && i+1 < text.size() && text[i+1] >= 0xdc00 && text[i+1] <= 0xdfff)
                                        c = 0x10000 + ((c-0xd800)<<10) + (text[++i]-0xdc00);
                                    else if (c >= 0xd800 && c <= 0xdfff) c = 0xfffd;
                                    event.composition.push_back(static_cast<char32_t>(c));
                                }
                            }
                            ImmReleaseContext(hwnd, context);
                        }
                    }
                    self.window.UIInputEvent.Invoke(std::move(event));
                }
                if (message == WM_TOUCH)
                {
                    struct TouchLease { HTOUCHINPUT value; ~TouchLease() { CloseTouchInputHandle(value); } } lease{reinterpret_cast<HTOUCHINPUT>(lparam)};
                    std::vector<TOUCHINPUT> touches(LOWORD(wparam));
                    if (touches.size() <= 256 && GetTouchInputInfo(lease.value, static_cast<UINT>(touches.size()), touches.data(), sizeof(TOUCHINPUT)))
                    {
                        POINT origin{}; ClientToScreen(hwnd, &origin);
                        for (const auto& touch : touches)
                        {
                            const auto phase = (touch.dwFlags & TOUCHEVENTF_DOWN) ? UITouchPhase::Began :
                                (touch.dwFlags & TOUCHEVENTF_UP) ? UITouchPhase::Ended : UITouchPhase::Moved;
                            self.window.UIInputEvent.Invoke({UIInputKind::Touch, static_cast<int32_t>(touch.dwID & 0x7fffffff),
                                {touch.x/100.0-origin.x, touch.y/100.0-origin.y}, phase});
                        }
                    }
                    return 0;
                }
            }
            catch (...) { OutputDebugStringA("Overload: rejected platform UI input\n"); }
            return DefSubclassProc(hwnd, message, wparam, lparam);
        }
        explicit State(Window& owner) : window(owner), handle(glfwGetWin32Window(owner.GetGlfwWindow()))
        {
            if (!SetWindowSubclass(handle, Procedure, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this)))
                throw std::runtime_error("Cannot install desktop UI input callback");
            RegisterTouchWindow(handle, 0);
        }
        ~State() { UnregisterTouchWindow(handle); RemoveWindowSubclass(handle, Procedure, reinterpret_cast<UINT_PTR>(this)); }
    };
    UIPlatformInput::UIPlatformInput(Window& window) : state(std::make_unique<State>(window)) { }
    UIPlatformInput::~UIPlatformInput() = default;
}
#else
namespace OvWindowing::Inputs
{
    struct UIPlatformInput::State { };
    UIPlatformInput::UIPlatformInput(Window&) : state(std::make_unique<State>()) { }
    UIPlatformInput::~UIPlatformInput() = default;
}
#endif
