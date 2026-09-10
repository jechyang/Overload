#pragma once
#include <string_view>
#include <OvRendering/Data/FeatureSet.h>
namespace OvCore::ECS::Components { class CSkinnedMeshRenderer; }
namespace OvCore::Rendering { struct ToolDrawInput; }
namespace OvCore::Rendering::SkinningUtils
{
    inline constexpr std::string_view kFeatureName = "SKINNING";
    bool IsSkinningActive(const ECS::Components::CSkinnedMeshRenderer* renderer);
    OvRendering::Data::FeatureSet BuildFeatureSet(const OvRendering::Data::FeatureSet* base = nullptr);
    // Called only during the stable scene-read interval.
    void CaptureToolSkinning(ToolDrawInput& input, const ECS::Components::CSkinnedMeshRenderer& renderer,
        const OvRendering::Data::FeatureSet* base = nullptr);
}
