#pragma once
#include <OvReflection/Annotations.h>
#include <string>
#include <limits>
namespace R5
{
    enum class OV_TYPE() Wide : unsigned long long { Zero = 0, Max = 18446744073709551615ULL };
    enum class OV_TYPE() Narrow : unsigned char { Zero = 0, Max = 255 };
    struct OV_TYPE() Values
    {
        OV_FIELD(Serialize, Inspect) bool boolean = true;
        OV_FIELD(Serialize, Inspect) signed char i8 = -128;
        OV_FIELD(Serialize, Inspect) unsigned char u8 = 255;
        OV_FIELD(Serialize, Inspect) short i16 = -32768;
        OV_FIELD(Serialize, Inspect) unsigned short u16 = 65535;
        OV_FIELD(Serialize, Inspect) int i32 = (-2147483647 - 1);
        OV_FIELD(Serialize, Inspect) unsigned int u32 = 4294967295U;
        OV_FIELD(Serialize, Inspect) long nativeLong = -2147483647L;
        OV_FIELD(Serialize, Inspect) unsigned long nativeUnsigned = 4294967295UL;
        OV_FIELD(Serialize, Inspect) long long i64 = (-9223372036854775807LL - 1);
        OV_FIELD(Serialize, Inspect) unsigned long long u64 = 18446744073709551615ULL;
        OV_FIELD(Serialize, Inspect, Min = -10, Max = 10, Step = 0.5) float real = -1.25f;
        OV_FIELD(Serialize, Inspect, Min = -1e300, Max = 1e300) double precise = 1.2345678901234567;
        OV_FIELD(Serialize, Inspect, Missing = Default, Default = "") std::string text = "<&> Unicode";
        OV_FIELD(Serialize, Inspect) Wide wide = Wide::Max;
        OV_FIELD(Serialize, Inspect) Narrow narrow = Narrow::Max;
        OV_FIELD(Serialize, Inspect, ReadOnly) int fixed = 42;
        OV_FIELD(Serialize, Inspect, Alias = "old-name", Missing = Default, Default = "-7") int fallback = 91;
        OV_FIELD(Serialize, Inspect) int kept = 31;
    };
    struct OV_TYPE() Required { OV_FIELD(Serialize, Missing = Error) int value = 2; };
    struct OV_TYPE(SerializeHook = "CustomWrite", DeserializeHook = "CustomRead") Custom
    { OV_FIELD(Serialize, Inspect, Drawer = "CustomDrawer") float value = 3; };
}
