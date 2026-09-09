#pragma once
#include <string_view>
#include <stdexcept>

namespace OvRendering::Data
{
    enum class RenderStage { Opaque, Transparent, ShadowCaster, ReflectionCapture, SceneUI, Picking };
    enum class DrawSorting { MaterialFrontToBack, BackToFront, Collection };
    enum class TechniqueFallback { DefaultEmptyFeatures, ShadowMaterial, PickingMaterial };
    struct StageDescription
    {
        RenderStage stage;
        std::string_view technique; // Empty explicitly means the default technique.
        DrawSorting sorting;
        TechniqueFallback fallback;
        bool staticSceneSupported;
        bool shadowReceiver, reflectionReceiver;
    };
    inline StageDescription DescribeStage(RenderStage stage)
    {
        using enum RenderStage;
        using enum DrawSorting;
        using enum TechniqueFallback;
        switch (stage)
        {
        case Opaque: return {stage, "", MaterialFrontToBack, DefaultEmptyFeatures, true, true, true};
        case Transparent: return {stage, "", BackToFront, DefaultEmptyFeatures, true, true, true};
        case SceneUI: return {stage, "", BackToFront, DefaultEmptyFeatures, true, true, false};
        case ShadowCaster: return {stage, "SHADOW_PASS", Collection, ShadowMaterial, false, false, false};
        case ReflectionCapture: return {stage, "REFLECTION_PASS", MaterialFrontToBack, DefaultEmptyFeatures, false, true, false};
        case Picking: return {stage, "PICKING_PASS", Collection, PickingMaterial, false, false, false};
        }
        throw std::invalid_argument("Unknown render stage");
    }
}
