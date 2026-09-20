#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace OvWindowing::Inputs
{
    struct UIPoint { double x = 0, y = 0; };
    // Offset/extent are window client logical units; output is Game framebuffer
    // pixels, bottom-left origin. Coordinates outside the viewport stay outside.
    struct UIInputViewport
    {
        UIPoint offset, extent, framebufferScale{1, 1};
        UIPoint Convert(UIPoint point) const
        {
            if (!std::isfinite(offset.x) || !std::isfinite(offset.y) ||
                !std::isfinite(extent.x) || !std::isfinite(extent.y) || extent.x <= 0 || extent.y <= 0 ||
                !std::isfinite(framebufferScale.x) || !std::isfinite(framebufferScale.y) ||
                framebufferScale.x <= 0 || framebufferScale.y <= 0 ||
                !std::isfinite(point.x) || !std::isfinite(point.y))
                throw std::invalid_argument("Invalid UI viewport/position");
            return {(point.x-offset.x)*framebufferScale.x, (extent.y-point.y+offset.y)*framebufferScale.y};
        }
    };
    enum class UIInputKind : uint32_t { PointerMove, PointerDown, PointerUp, Scroll, KeyDown, KeyUp, KeyRepeat, Character, Touch, Focus, Composition };
    enum class UITouchPhase : uint32_t { Began, Moved, Stationary, Ended, Canceled };
    struct UIInputEvent
    {
        UIInputKind kind{};
        int32_t id = 0; // Mouse button / GLFW key / nonnegative touch ID / Unicode scalar / focus boolean.
        UIPoint position;
        UITouchPhase phase{};
        std::u32string composition;
        uint32_t compositionCursor = 0;
    };
    struct UIInputSnapshot
    {
        uint64_t sequence = 0;
        double time = 0, deltaTime = 0;
        bool focused = true;
        UIPoint mousePosition, viewportSize;
        std::vector<UIInputEvent> events;
        std::vector<int32_t> heldKeys, heldButtons;
        std::u32string composition;
        uint32_t compositionCursor = 0;
    };
    // One collector per Window/InputManager owner; never shared mutable global state.
    // Platform callbacks and deterministic injection pass through exactly this queue.
    class UIInputCollector
    {
    public:
        void Push(UIInputEvent event)
        {
            CheckThread();
            if (pending.size() >= 16384 || event.composition.size() > 4096 || pendingScalars + event.composition.size() > 65536)
                throw std::length_error("UI input queue limit");
            if (static_cast<uint32_t>(event.kind) > static_cast<uint32_t>(UIInputKind::Composition) ||
                !std::isfinite(event.position.x) || !std::isfinite(event.position.y))
                throw std::invalid_argument("Invalid UI input event");
            auto scalar = [](uint32_t c) { return c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff); };
            if (event.kind == UIInputKind::Character && !scalar(static_cast<uint32_t>(event.id)))
                throw std::invalid_argument("Invalid Unicode scalar");
            for (auto c : event.composition) if (!scalar(c)) throw std::invalid_argument("Invalid composition scalar");
            if (event.compositionCursor > event.composition.size() ||
                (event.kind == UIInputKind::Touch && (event.id < 0 || static_cast<uint32_t>(event.phase) > 4)))
                throw std::invalid_argument("Invalid touch/composition");
            if ((event.kind == UIInputKind::KeyDown || event.kind == UIInputKind::KeyUp || event.kind == UIInputKind::KeyRepeat) &&
                (event.id < 0 || event.id >= 1024)) throw std::invalid_argument("Invalid key code");
            if ((event.kind == UIInputKind::PointerDown || event.kind == UIInputKind::PointerUp) &&
                (event.id < 0 || event.id >= 32)) throw std::invalid_argument("Invalid mouse button");
            if (event.kind == UIInputKind::Focus && event.id != 0 && event.id != 1) throw std::invalid_argument("Invalid focus state");
            pendingScalars += event.composition.size();
            pending.push_back(std::move(event));
        }
        std::shared_ptr<const UIInputSnapshot> Capture(const UIInputViewport& viewport, double time, double delta)
        {
            CheckThread(); viewport.Convert({});
            if (!std::isfinite(time) || !std::isfinite(delta) || delta < 0 || time < lastTime)
                throw std::invalid_argument("UI input requires monotonic time and nonnegative delta");
            auto frame = std::make_shared<UIInputSnapshot>();
            frame->sequence = ++sequence; frame->time = time; frame->deltaTime = delta;
            frame->viewportSize = {viewport.extent.x*viewport.framebufferScale.x, viewport.extent.y*viewport.framebufferScale.y};
            for (auto event : pending)
            {
                if (event.kind == UIInputKind::Focus)
                {
                    focused = event.id != 0;
                    if (!focused) { keys.clear(); buttons.clear(); composition.clear(); cursor = 0; }
                }
                else if (!focused) continue;
                if (event.kind == UIInputKind::KeyDown) keys.insert(event.id);
                if (event.kind == UIInputKind::KeyUp) keys.erase(event.id);
                if (event.kind == UIInputKind::PointerDown) buttons.insert(event.id);
                if (event.kind == UIInputKind::PointerUp) buttons.erase(event.id);
                if (event.kind == UIInputKind::PointerMove || event.kind == UIInputKind::PointerDown || event.kind == UIInputKind::PointerUp)
                    mouse = event.position;
                if (event.kind == UIInputKind::PointerMove || event.kind == UIInputKind::PointerDown ||
                    event.kind == UIInputKind::PointerUp || event.kind == UIInputKind::Touch)
                    event.position = viewport.Convert(event.position);
                if (event.kind == UIInputKind::Composition) { composition = event.composition; cursor = event.compositionCursor; }
                frame->events.push_back(std::move(event));
            }
            pending.clear(); pendingScalars = 0; lastTime = time;
            frame->focused = focused; frame->mousePosition = viewport.Convert(mouse);
            frame->heldKeys.assign(keys.begin(), keys.end()); frame->heldButtons.assign(buttons.begin(), buttons.end());
            frame->composition = composition; frame->compositionCursor = cursor;
            return frame;
        }
        void Reset()
        {
            CheckThread(); pending.clear(); pendingScalars = 0; keys.clear(); buttons.clear(); composition.clear(); cursor = 0;
        }
    private:
        void CheckThread() const { if (thread != std::this_thread::get_id()) throw std::logic_error("UI input requires its owner thread"); }
        const std::thread::id thread = std::this_thread::get_id();
        std::vector<UIInputEvent> pending;
        size_t pendingScalars = 0;
        std::set<int32_t> keys, buttons;
        UIPoint mouse;
        bool focused = true;
        uint64_t sequence = 0;
        double lastTime = 0;
        std::u32string composition;
        uint32_t cursor = 0;
    };
}
