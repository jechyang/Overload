#include "Reflection.generated.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace OvReflection;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
const PropertyDesc& Property(const TypeDesc& type, std::string_view name)
{
    auto* p = type.FindProperty(name);
    Require(p != nullptr, "missing property");
    return *p;
}
template<class V> void Roundtrip(const Registry& registry, R2::Values& object, const char* name, V input)
{
    V output{};
    const auto& p = Property(TypeOf<R2::Values>(), name);
    Require(p.Write(registry.Main(), ObjectRef::From(object), ValueRef::From(input)) == Error::None, "write failed");
    Require(p.Read(registry.Main(), ObjectRef::From(object), ValueRef::From(output)) == Error::None && output == input, "roundtrip mismatch");
}
int main() try
{
    Registry registry;
    Require(RegisterBuiltinTypes(registry) == Error::None, "builtin registration");
    Require(TypeOf<std::uint64_t>().Info().underlying.bits == 64 && !TypeOf<std::uint64_t>().Info().underlying.isSigned, "builtin integer width/sign");
    Require(registry.Find("cpp.std_string") == &TypeOf<std::string>(), "builtin string query");
    Require(RegisterFixtureTypes(registry) == Error::None, "explicit module registration failed");
    Require(RegisterFixtureTypes(registry) == Error::Duplicate, "duplicate module accepted");
    Require(registry.Find("legacy.values") == &TypeOf<const R2::Values&>(), "alias/typed lookup mismatch");
    Require(!registry.Find("unknown"), "unknown lookup");
    Require(!TypeOf<R2::Values>().FindProperty("hidden"), "unmarked member exposed");
    Require(TypeOf<R2::Signed>().Info().enumerators[0].decimalValue == "-9223372036854775808", "signed enum precision");
    Require(TypeOf<R2::Unsigned>().Info().enumerators[0].decimalValue == "18446744073709551615", "unsigned enum precision");
    const auto& method = TypeOf<R2::Values>().Info().methods[0];
    Require(method.isConst && method.isNoexcept && method.refQualifier == "&" && method.parameters.size() == 2, "method signature");
    Require(method.result.key == Key<std::uint64_t>() && method.parameters[0].type.key == Key<std::string>(), "method value identities");

    // Registration rejects a cross-module alias collision and rolls back the whole batch.
    auto collisionInfo = TypeOf<R2::Values>().Info();
    collisionInfo.id = "other"; collisionInfo.module = "Other"; collisionInfo.key = Key<long double>();
    TypeDesc collision(collisionInfo);
    auto validInfo = TypeOf<R2::Base>().Info();
    validInfo.id = "valid"; validInfo.module = "Other"; validInfo.key = Key<char>();
    validInfo.properties = {}; validInfo.aliases = {};
    TypeDesc valid(validInfo);
    std::array<const TypeDesc*, 2> batch{&valid, &collision};
    const auto size = registry.Types().size();
    Require(registry.RegisterModule("Other", batch) == Error::Duplicate && registry.Types().size() == size && !registry.Find("valid"), "non-atomic registration");
    collisionInfo.aliases = {}; collisionInfo.properties = {}; collisionInfo.methods = {};
    collisionInfo.id = "R2::Base.base";
    TypeDesc propertyCollision(collisionInfo);
    std::array<const TypeDesc*, 1> propertyBatch{&propertyCollision};
    Require(registry.RegisterModule("Other", propertyBatch) == Error::Duplicate, "global property identity collision");
    Require(registry.Freeze() == Error::None && registry.IsFrozen(), "freeze failed");
    Require(RegisterFixtureTypes(registry) == Error::Frozen, "registration after freeze");

    R2::Values object;
    Roundtrip(registry, object, "enabled", true);
    Roundtrip(registry, object, "i8", std::numeric_limits<std::int8_t>::min());
    Roundtrip(registry, object, "u8", std::numeric_limits<std::uint8_t>::max());
    Roundtrip(registry, object, "i16", std::numeric_limits<std::int16_t>::min());
    Roundtrip(registry, object, "u16", std::numeric_limits<std::uint16_t>::max());
    Roundtrip(registry, object, "i32", std::numeric_limits<std::int32_t>::min());
    Roundtrip(registry, object, "u32", std::numeric_limits<std::uint32_t>::max());
    Roundtrip(registry, object, "i64", std::numeric_limits<std::int64_t>::min());
    Roundtrip(registry, object, "u64", std::numeric_limits<std::uint64_t>::max());
    Roundtrip(registry, object, "f32", 1.25f);
    Roundtrip(registry, object, "f64", 1.23456789012345);
    Roundtrip(registry, object, "text", std::string("a\0b", 3));
    Roundtrip(registry, object, "signedEnum", R2::Signed::Negative);
    Roundtrip(registry, object, "unsignedEnum", R2::Unsigned::Maximum);

    const auto& amount = Property(TypeOf<R2::Values>(), "old.amount");
    int input = -20, output = 999;
    Require(amount.Write(registry.Main(), ObjectRef::From(object), ValueRef::From(input)) == Error::None && object.calls == 1 && object.hidden == -40, "setter bypassed or UI range incorrectly clamped");
    Require(amount.Read(registry.Main(), ObjectRef::From(std::as_const(object)), ValueRef::From(output)) == Error::None && output == -40, "const getter failed");
    R2::Secret secret;
    Require(amount.Write(registry.Main(), ObjectRef::From(secret), ValueRef::From(input)) == Error::WrongObjectType, "wrong object accepted");
    Require(amount.Read(registry.Main(), ObjectRef::From(secret), ValueRef::From(output)) == Error::WrongObjectType && output == -40, "wrong object read mutated output");
    float wrong = 1;
    Require(amount.Write(registry.Main(), ObjectRef::From(object), ValueRef::From(wrong)) == Error::WrongValueType && object.calls == 1, "wrong value invoked setter");
    Require(amount.Read(registry.Main(), ObjectRef::From(object), ValueRef::From(wrong)) == Error::WrongValueType && wrong == 1, "wrong output type accepted");
    Require(amount.Read(registry.Main(), ObjectRef::From(object), ValueRef::From(std::as_const(output))) == Error::WrongValueType, "const output accepted");
    Require(amount.Write(registry.Main(), ObjectRef::From(std::as_const(object)), ValueRef::From(input)) == Error::ReadOnly, "const object mutated");
    for (auto name : {"fixed", "policyReadOnly", "Ready"})
        Require(Property(TypeOf<R2::Values>(), name).Write(registry.Main(), ObjectRef::From(object), ValueRef::From(input)) == Error::ReadOnly, "readonly write");
    Require(Property(TypeOf<R2::Secret>(), "Number").Write(registry.Main(), ObjectRef::From(secret), ValueRef::From(input)) == Error::None, "private setter");
    Require(Property(TypeOf<R2::Secret>(), "value").Read(registry.Main(), ObjectRef::From(secret), ValueRef::From(output)) == Error::None && output == -19, "private field/accessor");
    Require(!TypeOf<R2::Secret>().FindProperty("unmarked"), "private unmarked field exposed");
    R2::Leaf leaf;
    Require(ObjectRef::From(leaf).As<R2::Base>() == static_cast<R2::Base*>(&leaf), "base adjustment");
    Require(Property(TypeOf<R2::Leaf>(), "old.base").Write(registry.Main(), ObjectRef::From(leaf), ValueRef::From(input)) == Error::None && leaf.base == input, "inherited property write");
    Require(!ObjectRef::From(static_cast<R2::Base&>(leaf)).As<R2::Leaf>(), "unchecked downcast");
    R2::ThroughGap gap;
    Require(ObjectRef::From(gap).As<R2::Base>() == static_cast<R2::Base*>(&gap), "unannotated gap conversion");
    Require(Property(TypeOf<R2::ThroughGap>(), "old.base").Write(registry.Main(), ObjectRef::From(gap), ValueRef::From(input)) == Error::None && gap.base == input, "unannotated gap property");

    bool workerPassed = false;
    std::thread worker([&]
    {
        int unchanged = 101;
        workerPassed = amount.Read(registry.Main(), ObjectRef::From(object), ValueRef::From(unchanged)) == Error::WrongThread
            && unchanged == 101
            && amount.Write(registry.Main(), ObjectRef::From(object), ValueRef::From(input)) == Error::WrongThread
            && Access<R2::Values>::Write_GetAmount(registry.Main(), object, input) == Error::WrongThread
            && registry.Find("legacy.values") == &TypeOf<R2::Values>()
            && registry.Freeze() == Error::WrongThread;
        Roundtrip(registry, object, "u64", std::numeric_limits<std::uint64_t>::max());
    });
    worker.join();
    Require(workerPassed && object.calls == 1, "Main thread access or immutable metadata sharing");
    std::puts("PASS: registration/static library retention, primitive/enum/string roundtrips, private access, inherited adjustment, rejection and Main/Any rules");
    return 0;
}
catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
