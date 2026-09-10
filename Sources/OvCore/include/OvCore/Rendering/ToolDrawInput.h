#pragma once
#include <baregl/types/EPrimitiveMode.h>
#include <OvRendering/Data/Material.h>
#include <OvRendering/Resources/IMesh.h>
#include <OvTools/Utils/OptRef.h>
namespace OvCore::Rendering
{
    // Extraction-only input. The collector immediately retains geometry and material
    // generations; transforms and the transposed palette are explicit values.
    struct ToolDrawInput
    {
        OvTools::Utils::OptRef<OvRendering::Resources::IMesh> mesh;
        OvTools::Utils::OptRef<OvRendering::Data::Material> material;
        OvRendering::Data::StateMask stateMask;
        baregl::types::EPrimitiveMode primitiveMode = baregl::types::EPrimitiveMode::TRIANGLES;
        std::optional<std::string> pass;
        std::optional<OvRendering::Data::FeatureSet> featureSetOverride;
        OvMaths::FMatrix4 model = OvMaths::FMatrix4::Identity;
        OvMaths::FMatrix4 user = OvMaths::FMatrix4::Identity;
        std::vector<OvMaths::FMatrix4> palette;
    };
}
