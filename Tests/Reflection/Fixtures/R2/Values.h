#pragma once
#include <OvReflection/Annotations.h>
#include <cstdint>
#include <string>

namespace R2
{
    enum OV_TYPE() Signed : std::int64_t { Negative = -9223372036854775807LL - 1, Zero = 0 };
    enum class OV_TYPE() Unsigned : std::uint64_t { Maximum = 18446744073709551615ULL };
    struct OV_TYPE() Base { OV_FIELD(Alias = "old.base") int base = 5; };
    // The derived vptr requires a real adjustment to the non-polymorphic base on MSVC.
    class OV_TYPE() Derived : public Base
    {
    public:
        virtual ~Derived() = default;
        OV_FIELD() float own = 2;
    };
    struct OV_TYPE() Leaf : Derived {};
    struct Gap : Base {};
    struct OV_TYPE() ThroughGap : Gap {};
    struct OV_TYPE(Id = "r2.values", Alias = "legacy.values", DisplayName = "数值") Values
    {
        OV_FIELD() bool enabled = false;
        OV_FIELD() std::int8_t i8 = 0;
        OV_FIELD() std::uint8_t u8 = 0;
        OV_FIELD() std::int16_t i16 = 0;
        OV_FIELD() std::uint16_t u16 = 0;
        OV_FIELD() std::int32_t i32 = 0;
        OV_FIELD() std::uint32_t u32 = 0;
        OV_FIELD() std::int64_t i64 = 0;
        OV_FIELD() std::uint64_t u64 = 0;
        OV_FIELD() float f32 = 0;
        OV_FIELD() double f64 = 0;
        OV_FIELD() std::string text;
        OV_FIELD() Signed signedEnum = Negative;
        OV_FIELD() Unsigned unsignedEnum = Unsigned::Maximum;
        OV_FIELD() const int fixed = 7;
        OV_FIELD(ReadOnly) int policyReadOnly = 11;
        int calls = 0;
        int hidden = 0;
        OV_PROPERTY(Setter = SetAmount, Name = "amount", Alias = "old.amount", Thread = Main, Min = 0, Max = 10)
        int GetAmount() const { return hidden; }
        void SetAmount(const int& input) { hidden = input * 2; ++calls; }
        OV_PROPERTY(ReadOnly) bool IsReady() const { return true; }
        OV_METHOD(Script, Thread = Main) std::uint64_t Count(const std::string&, int) const & noexcept { return u64; }
    };
    class OV_TYPE() Secret
    {
        friend struct OvReflection::Access<Secret>;
        using Number = int;
        OV_FIELD() Number value = 3;
        OV_PROPERTY(Setter = SetNumber) Number GetNumber() const { return value; }
        void SetNumber(Number input) { value = input + 1; }
        [[maybe_unused]] int unmarked = 99;
    };
}
