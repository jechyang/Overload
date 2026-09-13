#pragma once
#include <OvReflection/Annotations.h>
#include "Indirect.h"
#include <string>

namespace Fixture::Nested
{
    class OV_TYPE() EmptyClass {};
    struct OV_TYPE() EmptyStruct {};
    enum OV_TYPE() Plain : int { Negative = -7, Positive = 4 };
    enum class OV_TYPE() Scoped : std::uint64_t { Small = 1, Large = 18446744073709551615ULL };
    struct OV_TYPE(Id = "fixture.stable", Alias = "fixture.old", Alias = "legacy.Type") Value
    {
        OV_FIELD(Serialize) Counter count;
        OV_FIELD() std::int8_t small;
        OV_FIELD() std::string label;
        OV_FIELD() Scoped mode;
        OV_FIELD() const int constant = 4;
        int hidden = 0;
        OV_PROPERTY(Setter = SetSize, Serialize, Inspect, Name = "size", Alias = "legacy.size", Thread = Main, Min = 0, Step = 0.5)
        float GetSize() const;
        void SetSize(const float& value);
        OV_PROPERTY(ReadOnly) bool IsReady() const;
        OV_PROPERTY(ReadOnly, Name = "custom") double Measure() const;
        OV_METHOD(Script, Thread = Main) Counter Compute(std::int32_t input, const std::string& label) const & noexcept;
    };
    struct OV_TYPE() Base { OV_FIELD() int baseValue; };
    struct OV_TYPE() Derived : Base { OV_FIELD() float derivedValue; };
    class OV_TYPE() Private
    {
        friend struct OvReflection::Access<Private>;
        OV_FIELD() int secret;
        OV_PROPERTY(Setter = SetValue) int GetValue() const;
        void SetValue(int value);
    };
    struct OV_TYPE() Forward;
    struct Forward { OV_FIELD() int value; };
    inline Counter Value::Compute(std::int32_t input, const std::string&) const & noexcept { return input; }
}
namespace Other { struct OV_TYPE() EmptyStruct {}; }
namespace Fixture { inline namespace V1 { struct OV_TYPE() Inline {}; } }

// Prove the fixed authorization is also legal to the target compiler in normal mode.
namespace OvReflection
{
    template<> struct Access<Fixture::Nested::Private>
    {
        static int& Secret(Fixture::Nested::Private& value) { return value.secret; }
        static void Set(Fixture::Nested::Private& value, int input) { value.SetValue(input); }
    };
}
