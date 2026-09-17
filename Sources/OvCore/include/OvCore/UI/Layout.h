#pragma once

#include <OvMaths/FVector2.h>

namespace OvCore::UI
{
    // Canvas-local units, origin at the bottom left, positive Y upwards.
    struct Rect
    {
        OvMaths::FVector2 min{0, 0};
        OvMaths::FVector2 size{0, 0};
        bool Contains(OvMaths::FVector2 point) const;
        bool Empty() const { return size.x <= 0 || size.y <= 0; }
        static Rect Intersect(const Rect& a, const Rect& b);
    };

    struct RectLayout
    {
        OvMaths::FVector2 anchorMin{0.5f, 0.5f};
        OvMaths::FVector2 anchorMax{0.5f, 0.5f};
        OvMaths::FVector2 pivot{0.5f, 0.5f};
        OvMaths::FVector2 anchoredPosition{0, 0};
        OvMaths::FVector2 sizeDelta{100, 100};
    };

    // Stateless layout: the same values feed rendering and pointer hit testing.
    Rect ResolveRect(const RectLayout& layout, const Rect& parent);
    bool IsFinite(OvMaths::FVector2 value);
}
