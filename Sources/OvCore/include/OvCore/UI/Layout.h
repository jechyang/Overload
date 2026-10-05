#pragma once
#include <OvReflection/Annotations.h>

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

    struct OV_TYPE(Record) RectLayout
    {
        OV_FIELD(Serialize, Inspect, Name = "anchor_min", DisplayName = "Anchor Min", Step = 0.01, Order = 0)
        OvMaths::FVector2 anchorMin{0.5f, 0.5f};
        OV_FIELD(Serialize, Inspect, Name = "anchor_max", DisplayName = "Anchor Max", Step = 0.01, Order = 1)
        OvMaths::FVector2 anchorMax{0.5f, 0.5f};
        OV_FIELD(Serialize, Inspect, Name = "pivot", DisplayName = "Pivot", Step = 0.01, Order = 2)
        OvMaths::FVector2 pivot{0.5f, 0.5f};
        OV_FIELD(Serialize, Inspect, Name = "anchored_position", DisplayName = "Anchored Position", Step = 0.01, Order = 3)
        OvMaths::FVector2 anchoredPosition{0, 0};
        OV_FIELD(Serialize, Inspect, Name = "size_delta", DisplayName = "Size Delta", Step = 0.01, Order = 4)
        OvMaths::FVector2 sizeDelta{100, 100};
    };

    // Stateless layout: the same values feed rendering and pointer hit testing.
    Rect ResolveRect(const RectLayout& layout, const Rect& parent);
    bool IsFinite(OvMaths::FVector2 value);
}
