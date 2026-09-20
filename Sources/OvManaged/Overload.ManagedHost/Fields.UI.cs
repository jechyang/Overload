using System;
using Overload.Maths;
using Overload.UI;

namespace Overload.ManagedHost;

public static unsafe partial class Bootstrap
{
    private const int MaxFieldTextBytes = 64 * 1024;
    private static uint UITag(Type type) => type == typeof(Vector2) ? 19u : type == typeof(Vector4) ? 20u :
        type == typeof(Rect) ? 21u : type == typeof(RectOffset) ? 22u : type == typeof(Color) ? 23u :
        type.IsGenericType && type.GetGenericTypeDefinition() == typeof(ComponentRef<>) ? 24u : 0u;
    private static string ReferenceType(Type fieldType) => ComponentIdentity.Of(fieldType.GetGenericArguments()[0]);

    private static void ReadUIScalar(uint tag, object value, Scalar* scalar)
    {
        Span<float> f = new(&scalar->F32, 4);
        switch (tag) {
            case 19: var v2 = (Vector2)value; f[0] = v2.x; f[1] = v2.y; break;
            case 20: var v4 = (Vector4)value; f[0] = v4.X; f[1] = v4.Y; f[2] = v4.Z; f[3] = v4.W; break;
            case 21: var r = (Rect)value; f[0] = r.min.x; f[1] = r.min.y; f[2] = r.size.x; f[3] = r.size.y; break;
            case 22:
                var offset = (RectOffset)value; Span<int> n = new(&scalar->I32, 4);
                n[0] = offset.left; n[1] = offset.right; n[2] = offset.top; n[3] = offset.bottom; break;
            case 23: var color = (Color)value; f[0] = color.r; f[1] = color.g; f[2] = color.b; f[3] = color.a; break;
            default: throw new Failure(Status.UnsupportedField, "Unsupported UI value type");
        }
    }
    private static object WriteUIScalar(uint tag, Scalar* scalar)
    {
        ReadOnlySpan<float> f = new(&scalar->F32, 4);
        ReadOnlySpan<int> n = new(&scalar->I32, 4);
        return tag switch {
            19 => new Vector2(f[0], f[1]),
            20 => new Vector4(f[0], f[1], f[2], f[3]),
            21 => new Rect(new(f[0], f[1]), new(f[2], f[3])),
            22 => new RectOffset(n[0], n[1], n[2], n[3]),
            23 => new Color(f[0], f[1], f[2], f[3]),
            _ => throw new Failure(Status.UnsupportedField, "Unsupported UI value type")
        };
    }
    private static void ValidateFieldScalar(uint tag, Scalar* scalar)
    {
        int count = tag switch { 10 => 1, 15 => 3, 16 => 4, 19 => 2, 20 or 21 or 23 => 4, _ => 0 };
        ReadOnlySpan<float> values = new(&scalar->F32, count);
        foreach (float value in values) Require(float.IsFinite(value), Status.InvalidArgument, "Field floats must be finite");
        if (tag == 11) Require(double.IsFinite(scalar->F64), Status.InvalidArgument, "Field doubles must be finite");
    }
    private static void ValidateEnum(Type type, object value)
    {
        if (Enum.IsDefined(type, value)) return;
        Require(type.IsDefined(typeof(FlagsAttribute), false), Status.InvalidArgument, "Undefined enum value");
        Type underlying = Enum.GetUnderlyingType(type);
        bool signed = underlying == typeof(sbyte) || underlying == typeof(short) || underlying == typeof(int) || underlying == typeof(long);
        ulong Bits(object entry) => signed ? unchecked((ulong)Convert.ToInt64(entry)) : Convert.ToUInt64(entry);
        ulong mask = 0;
        foreach (object entry in Enum.GetValues(type)) mask |= Bits(entry);
        Require((Bits(value) & ~mask) == 0, Status.InvalidArgument, "Undefined enum flag bits");
    }
}
