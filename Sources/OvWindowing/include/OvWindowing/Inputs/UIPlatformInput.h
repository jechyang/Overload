#pragma once
#include <memory>
namespace OvWindowing { class Window; }
namespace OvWindowing::Inputs
{
    // Desktop IME composition and multi-touch; no text editing policy.
    class UIPlatformInput
    {
    public:
        explicit UIPlatformInput(Window& window);
        ~UIPlatformInput();
        UIPlatformInput(const UIPlatformInput&) = delete;
        UIPlatformInput& operator=(const UIPlatformInput&) = delete;
    private:
        struct State;
        std::unique_ptr<State> state;
    };
}
