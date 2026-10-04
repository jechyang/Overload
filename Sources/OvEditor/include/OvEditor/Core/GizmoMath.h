#pragma once
#include <cmath>
#include <algorithm>
#include <OvMaths/FMatrix4.h>
#include <OvMaths/FVector2.h>
#include <OvMaths/FVector3.h>
#include <OvMaths/FVector4.h>

namespace OvEditor::Core::GizmoMath
{
    inline float Snap(float value, float step)
    {
        return std::isfinite(step) && step > 0.0f ? std::round(value / step) * step : value;
    }

    // Convert pixels to world units at the pivot, for perspective and orthographic views.
    inline float WorldSize(const OvMaths::FVector3& pivot, const OvMaths::FMatrix4& view,
        const OvMaths::FMatrix4& projection, float height, float pixels)
    {
        const auto clip = projection * (view * OvMaths::FVector4{pivot.x, pivot.y, pivot.z, 1});
        return 2.0f * std::abs(clip.w) * pixels /
            std::max(0.0001f, std::abs(projection.data[5]) * height);
    }

    inline OvMaths::FVector2 Project(const OvMaths::FVector3& position,
        const OvMaths::FMatrix4& view, const OvMaths::FMatrix4& projection, const OvMaths::FVector2& size)
    {
        const auto clip = projection * (view * OvMaths::FVector4{position.x, position.y, position.z, 1});
        if (std::abs(clip.w) < 0.0001f) return {};
        return {(clip.x / clip.w + 1) * size.x * 0.5f, (1 - clip.y / clip.w) * size.y * 0.5f};
    }
}
