#include <OvCore/UI/Layout.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace OvCore::UI
{
    bool IsFinite(OvMaths::FVector2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }

    bool Rect::Contains(OvMaths::FVector2 p) const
    {
        return !Empty() && p.x >= min.x && p.y >= min.y &&
            p.x < min.x + size.x && p.y < min.y + size.y;
    }

    Rect Rect::Intersect(const Rect& a, const Rect& b)
    {
        const OvMaths::FVector2 low{std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y)};
        return {low, {std::max(0.0f, std::min(a.min.x + a.size.x, b.min.x + b.size.x) - low.x),
            std::max(0.0f, std::min(a.min.y + a.size.y, b.min.y + b.size.y) - low.y)}};
    }

    Rect ResolveRect(const RectLayout& l, const Rect& parent)
    {
        if (!IsFinite(l.anchorMin) || !IsFinite(l.anchorMax) || !IsFinite(l.pivot) ||
            !IsFinite(l.anchoredPosition) || !IsFinite(l.sizeDelta) || !IsFinite(parent.min) ||
            !IsFinite(parent.size) || parent.size.x < 0 || parent.size.y < 0)
            throw std::invalid_argument("Invalid UI rectangle");
        Rect result;
        const auto axis = [](float origin, float length, float low, float high, float pivot,
            float position, float delta, float& outMin, float& outSize) {
            outSize = (high - low) * length + delta;
            const float anchor = origin + (low + (high - low) * pivot) * length;
            outMin = anchor + position - outSize * pivot;
            // Inverted rectangles have no drawable or hittable area.
            outSize = std::max(0.0f, outSize);
        };
        axis(parent.min.x, parent.size.x, l.anchorMin.x, l.anchorMax.x, l.pivot.x,
            l.anchoredPosition.x, l.sizeDelta.x, result.min.x, result.size.x);
        axis(parent.min.y, parent.size.y, l.anchorMin.y, l.anchorMax.y, l.pivot.y,
            l.anchoredPosition.y, l.sizeDelta.y, result.min.y, result.size.y);
        if (!IsFinite(result.min) || !IsFinite(result.size)) throw std::overflow_error("UI rectangle overflow");
        return result;
    }

}
