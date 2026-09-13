// An ID-only consumer intentionally does not include fixture or generated type headers.
#include <OvReflection/Reflection.h>
namespace OvReflection { Error RegisterFixtureTypes(Registry&); }
int main()
{
    OvReflection::Registry registry;
    if (OvReflection::RegisterFixtureTypes(registry) != OvReflection::Error::None) return 1;
    if (registry.Freeze() != OvReflection::Error::None) return 2;
    const auto* type = registry.Find("legacy.values");
    return type && type->FindProperty("old.amount") && registry.Find("R2::Unsigned") ? 0 : 3;
}
