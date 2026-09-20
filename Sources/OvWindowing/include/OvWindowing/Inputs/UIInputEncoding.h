#pragma once
#include <OvWindowing/Inputs/UIInputSnapshot.h>
#include <bit>
namespace OvWindowing::Inputs
{
    // Version 1 little-endian 64-bit words; no pointers, padding, or managed layout.
    inline std::vector<uint64_t> EncodeUIInput(const UIInputSnapshot& frame)
    {
        const auto point = [](UIPoint p) { return uint64_t(std::bit_cast<uint32_t>(float(p.x))) |
            (uint64_t(std::bit_cast<uint32_t>(float(p.y))) << 32); };
        std::vector<uint64_t> words{0x3149554f, frame.sequence, std::bit_cast<uint64_t>(frame.time),
            std::bit_cast<uint64_t>(frame.deltaTime), frame.focused ? 1ull : 0ull, point(frame.mousePosition),
            point(frame.viewportSize), frame.events.size(), frame.heldKeys.size(), frame.heldButtons.size(),
            frame.composition.size(), frame.compositionCursor};
        for (const auto& event : frame.events)
        {
            words.push_back(uint64_t(event.kind) | (uint64_t(uint32_t(event.id)) << 32));
            words.push_back(point(event.position)); words.push_back(uint64_t(event.phase));
            words.push_back(event.compositionCursor); words.push_back(event.composition.size());
            for (auto scalar : event.composition) words.push_back(scalar);
        }
        for (auto key : frame.heldKeys) words.push_back(uint32_t(key));
        for (auto button : frame.heldButtons) words.push_back(uint32_t(button));
        for (auto scalar : frame.composition) words.push_back(scalar);
        return words;
    }
}
