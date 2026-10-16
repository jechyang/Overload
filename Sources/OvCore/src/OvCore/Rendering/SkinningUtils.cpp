/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <cstdint>
#include <string>

#include <OvCore/ECS/Components/CSkinnedMeshRenderer.h>
#include <OvCore/Rendering/SkinningUtils.h>
#include <OvCore/Rendering/ToolDrawInput.h>

bool OvCore::Rendering::SkinningUtils::IsSkinningActive(const OvCore::ECS::Components::CSkinnedMeshRenderer* p_renderer)
{
	return p_renderer && p_renderer->IsEnabled() && p_renderer->HasSkinningData();
}

OvRendering::Data::FeatureSet OvCore::Rendering::SkinningUtils::BuildFeatureSet(const OvRendering::Data::FeatureSet* p_baseFeatures)
{
	OvRendering::Data::FeatureSet features = p_baseFeatures ? *p_baseFeatures : OvRendering::Data::FeatureSet{};
	features.insert(std::string{ kFeatureName });
	return features;
}

void OvCore::Rendering::SkinningUtils::CaptureToolSkinning(
    ToolDrawInput& input, const ECS::Components::CSkinnedMeshRenderer& renderer,
    const OvRendering::Data::FeatureSet* base)
{
    input.featureSetOverride=BuildFeatureSet(base);
    input.palette=renderer.GetBoneMatricesTransposed();
}
