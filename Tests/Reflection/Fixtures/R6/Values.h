#pragma once
#include <OvReflection/Annotations.h>
#include <OvMaths/FVector3.h>
#include <OvMaths/FQuaternion.h>
#include <cstdint>
#include <string>
#include <stdexcept>
namespace R6 {
enum class OV_TYPE() Mode : uint64_t { Zero = 0, Maximum = UINT64_MAX };
struct OV_TYPE() Other { OV_FIELD(Script) int number = 5; };
class OV_TYPE() Values {
    friend struct OvReflection::Access<Values>;
    OV_FIELD(Script) int secret = 17;
public:
    OV_FIELD(Script) bool flag = false;
    OV_FIELD(Script) int8_t i8 = 0;
    OV_FIELD(Script) uint8_t u8 = 0;
    OV_FIELD(Script) int16_t i16 = 0;
    OV_FIELD(Script) uint16_t u16 = 0;
    OV_FIELD(Script) int32_t i32 = 0;
    OV_FIELD(Script) uint32_t u32 = 0;
    OV_FIELD(Script) int64_t i64 = 0;
    OV_FIELD(Script) uint64_t u64 = 0;
    OV_FIELD(Script) float single = 0;
    OV_FIELD(Script) double real = 0;
    OV_FIELD(Script) std::string text;
    OV_FIELD(Script) Mode mode = Mode::Zero;
    OV_FIELD(Script, Ownership = Borrowed, Nullable = Allow) Other* target = nullptr;
    OV_FIELD(Script) OvMaths::FVector3 vector;
    OV_FIELD(Script) OvMaths::FQuaternion rotation;
    OV_FIELD(Script, ReadOnly) int immutable = 8;
    OV_FIELD() int nativeOnly = 123;
    int Unmarked(int value) { return value; }
    OV_METHOD(Script) int Add(int a, int b) const { return a + b; }
    OV_METHOD(Script) void Raise() { throw std::runtime_error("expected native failure"); }
    OV_METHOD(Script) void RaiseUnknown() { throw 42; }
    OV_METHOD(Script) std::string Echo(const std::string& value) const { return value; }
    OV_METHOD(Script, Return = Copy) const OvMaths::FVector3& Position() const { return vector; }
    OV_METHOD(Script, Name = "Integer", Overload = "int (int) const") int Overloaded(int x) const { return x + 3; }
    OV_METHOD(Script, Name = "Real", Overload = "double (double) const") double Overloaded(double x) const { return x + 0.5; }
    OV_METHOD(Script, Ownership = Borrowed, Nullable = Allow) Other* EchoObject(Other* value) { return value; }
    OV_METHOD(Script, Ownership = Borrowed, Nullable = Deny) Other* RequiredObject(Other* value) { return value; }
    OV_METHOD(Script, Ownership = Borrowed, Nullable = Deny) Other* Unpublished() { static Other other; return &other; }
};
}
