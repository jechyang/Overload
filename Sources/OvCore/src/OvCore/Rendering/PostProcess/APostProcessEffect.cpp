#include <OvCore/Rendering/PostProcess/AEffect.h>
bool OvCore::Rendering::PostProcess::AEffect::IsApplicable(const EffectSettings& settings) const { return settings.enabled; }
