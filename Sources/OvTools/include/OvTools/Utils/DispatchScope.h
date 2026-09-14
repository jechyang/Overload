#pragma once
#include <deque>
#include <functional>

namespace OvTools::Utils
{
    // Main-only callback barrier shared by Lua, C# and component callbacks.
    // Actor deletion is collected by Scene after the phase, never by this queue.
    class DispatchScope final
    {
    public:
        DispatchScope() { ++depth; }
        ~DispatchScope() { --depth; }
        static bool Active() { return depth != 0; }
        static void Defer(std::function<void()> action) { pending.push_back(std::move(action)); }
        static void Flush()
        {
            if (Active() || flushing) return;
            flushing = true;
            try {
                while (!pending.empty()) {
                    auto action = std::move(pending.front()); pending.pop_front(); action();
                }
            } catch (...) { flushing = false; throw; }
            flushing = false;
        }
    private:
        static inline thread_local unsigned depth = 0;
        static inline thread_local bool flushing = false;
        static inline thread_local std::deque<std::function<void()>> pending;
    };
}
