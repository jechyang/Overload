#include <OvCore/Rendering/PostProcess/PostProcessSnapshot.h>
#include <OvCore/Rendering/PostProcess/PostProcessStack.h>
using namespace OvCore::Rendering::PostProcess;
PostProcessSnapshot PostProcessSnapshot::Capture(const PostProcessStack& stack)
{
    return {stack.Get<AutoExposureEffect,AutoExposureSettings>(), stack.Get<BloomEffect,BloomSettings>(),
        stack.Get<TonemappingEffect,TonemappingSettings>(), stack.Get<FXAAEffect,FXAASettings>()};
}
