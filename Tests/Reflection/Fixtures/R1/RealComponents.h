#pragma once
#include <OvReflection/Annotations.h>
#include <OvCore/ECS/Components/CPhysicalBox.h>
#include <OvCore/ECS/Components/CTransform.h>

// Real component ancestry and math types, plus R2's annotated production declarations.
namespace Fixture
{
    struct OV_TYPE(Component, Factory = AddComponent, Alias = "Legacy.Box") Box : OvCore::ECS::Components::CPhysicalBox
    {
        OV_PROPERTY(Setter = SetReflectedSize, Name = "fixtureSize", Serialize, Inspect)
        OvMaths::FVector3 GetReflectedSize() const { return GetSize(); }
        void SetReflectedSize(const OvMaths::FVector3& value) { SetSize(value); }
    };
    struct OV_TYPE(Component, Lookup = Existing) Transform : OvCore::ECS::Components::CTransform {};
}
