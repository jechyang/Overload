#pragma once
#include <OvCore/ECS/Actor.h>
#include <OvReflection/Annotations.h>
#include <OvMaths/FVector3.h>
#include <OvCore/Resources/ResourceReference.h>
#include <OvRendering/Resources/Texture.h>
#include <array>
#include <vector>

namespace ReflectionFixture
{
    enum class OV_TYPE() Mode : unsigned long long { First = 1, Last = 18446744073709551615ULL };
    struct OV_TYPE(Record) Entry
    {
        OV_FIELD(Serialize, Inspect, Step = 0.1) float weight = 1;
        OV_FIELD(Serialize, Inspect, Step = 0.05) OvMaths::FVector3 position{1,2,3};
        OV_FIELD(Serialize) OvCore::Resources::ResourceReference<OvRendering::Resources::Texture> texture;
    };
    class OV_TYPE(Component, Factory = AddComponent, Alias = "class ReflectionFixture::CCollection", DisplayName = "Collection Fixture") CCollection : public OvCore::ECS::Components::AComponent
    {
    public:
        explicit CCollection(OvCore::ECS::Actor& actor) : AComponent(actor) {}
        std::string GetName() override { return "Collection Fixture"; }
        std::string GetTypeName() override { return "class ReflectionFixture::CCollection"; }
        OV_FIELD(Serialize, Inspect, ItemName = "item", DisplayName = "Fixed Numbers") std::array<int,3> fixed{1,2,3};
        OV_FIELD(Serialize, Inspect, ItemName = "item", MaxItems = 64, DisplayName = "Records") std::vector<Entry> records{{},{}};
        OV_FIELD(Serialize, Inspect, ItemName = "item", MaxItems = 4, DisplayName = "Flags") std::vector<bool> flags{true,false};
        OV_FIELD(Serialize, Inspect, ItemName = "item", MaxItems = 8, DisplayName = "Names") std::vector<std::string> names{"one","two"};
        OV_FIELD(Serialize, Inspect, ItemName = "item", MaxItems = 4, DisplayName = "Modes") std::vector<Mode> modes{Mode::First,Mode::Last};
        OV_FIELD(Serialize, Inspect, ItemName = "item", MaxItems = 4, DisplayName = "Points") std::vector<OvMaths::FVector3> points{{1,2,3},{4,5,6}};
        OV_FIELD(Serialize, Inspect, ReadOnly, ItemName = "item", DisplayName = "Read Only") std::array<float,2> readOnly{1,2};
    };
}
namespace OvCore::ECS::Components
{
    template<> struct ComponentTraits<ReflectionFixture::CCollection>
    { static constexpr std::string_view Name="class ReflectionFixture::CCollection"; };
}
