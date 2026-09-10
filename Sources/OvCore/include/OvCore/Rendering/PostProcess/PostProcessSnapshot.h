#pragma once
#include <optional>
#include <OvCore/Rendering/PostProcess/AutoExposureEffect.h>
#include <OvCore/Rendering/PostProcess/BloomEffect.h>
#include <OvCore/Rendering/PostProcess/TonemappingEffect.h>
#include <OvCore/Rendering/PostProcess/FXAAEffect.h>
namespace OvCore::Rendering::PostProcess
{
    class PostProcessStack;
    struct PostProcessSnapshot
    {
        AutoExposureSettings exposure;
        BloomSettings bloom;
        TonemappingSettings tonemapping;
        FXAASettings fxaa;
        static PostProcessSnapshot Capture(const PostProcessStack& stack);
    };
}
