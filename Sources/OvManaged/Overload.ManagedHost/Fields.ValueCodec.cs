using System;
using System.Linq;
using V3 = Overload.Maths.Vector3;
using Q4 = Overload.Maths.Quaternion;

namespace Overload.ManagedHost;

public static unsafe partial class Bootstrap
{
    private static FieldValue PackField(Type type, object? value, out string text, int depth = 0)
    {
        var kind = DescribeField(type);
        Require(value != null || kind.Tag == 25, Status.UnsupportedField, "Null scalar field is unsupported");
        FieldValue r = new() { Size = 48, Tag = kind.Tag, Auxiliary = kind.Auxiliary }; text = "";
        switch (kind.Tag) {
            case 1: r.Scalar.B = (bool)value! ? (byte)1 : (byte)0; break;
            case 2: r.Scalar.I8 = (sbyte)value!; break; case 3: r.Scalar.B = (byte)value!; break;
            case 4: r.Scalar.I16 = (short)value!; break; case 5: r.Scalar.U16 = (ushort)value!; break;
            case 6: r.Scalar.I32 = (int)value!; break; case 7: r.Scalar.U32 = (uint)value!; break;
            case 8: r.Scalar.I64 = (long)value!; break; case 9: r.Scalar.U64 = (ulong)value!; break;
            case 10: r.Scalar.F32 = (float)value!; break; case 11: r.Scalar.F64 = (double)value!; break;
            case 12: text = (string)value!; break;
            case 13: r.Scalar.I64 = Convert.ToInt64(value); break; case 14: r.Scalar.U64 = Convert.ToUInt64(value); break;
            case 15: r.Scalar.Vector = (V3)value!; break; case 16: r.Scalar.Quaternion = (Q4)value!; break;
            case 17: r.Scalar.U64 = ((ActorRef)value!).Guid; break;
            case 18: var asset = (AssetRef)value!; r.Auxiliary = (uint)asset.Kind; text = NormalizeAsset(asset.Path ?? ""); break;
            case 24:
                var reference = (IComponentReference)value!; r.Scalar.U64 = reference.ActorGuid;
                text = ReferenceType(type) + "\n" + reference.ComponentType; break;
            case 25: text = WriteCollection(type, value, depth); break;
            default: ReadUIScalar(kind.Tag, value!, &r.Scalar); break;
        }
        ValidateFieldScalar(kind.Tag, &r.Scalar);
        if (kind.Tag is 13 or 14) ValidateEnum(type, value!);
        Require(Encoding.GetByteCount(text) <= MaxFieldTextBytes, Status.InvalidArgument, "Field text exceeds byte limit");
        return r;
    }
    private static object? UnpackField(Type type, FieldValue* p, int depth = 0)
    {
        Require(p != null && p->Size == 48 && p->Flags == 0, Status.InvalidArgument, "Invalid field value layout/flags");
        var kind = DescribeField(type);
        Require(p->Tag == kind.Tag && (kind.Tag == 18 ? p->Auxiliary <= (uint)AssetKind.Prefab : p->Auxiliary == kind.Auxiliary), Status.FieldTypeMismatch, "Field tag/enum underlying type/asset kind mismatch");
        Require(kind.Tag is 12 or 18 or 24 or 25 || p->Text.Length == 0, Status.InvalidArgument, "Unexpected scalar text");
        Require(p->Text.Length <= MaxFieldTextBytes, Status.InvalidArgument, "Field text exceeds byte limit");
        ValidateFieldScalar(kind.Tag, &p->Scalar);
        object? value;
        switch (kind.Tag) {
            case 1: Require(p->Scalar.B <= 1, Status.InvalidArgument, "Bool must be 0 or 1"); value = p->Scalar.B != 0; break;
            case 2: value = p->Scalar.I8; break; case 3: value = p->Scalar.B; break;
            case 4: value = p->Scalar.I16; break; case 5: value = p->Scalar.U16; break;
            case 6: value = p->Scalar.I32; break; case 7: value = p->Scalar.U32; break;
            case 8: value = p->Scalar.I64; break; case 9: value = p->Scalar.U64; break;
            case 10: value = p->Scalar.F32; break; case 11: value = p->Scalar.F64; break;
            case 12: value = Decode(p->Text); break;
            case 13: {
                long v = p->Scalar.I64; Require(kind.Auxiliary == 8 || kind.Auxiliary == 2 && v >= sbyte.MinValue && v <= sbyte.MaxValue || kind.Auxiliary == 4 && v >= short.MinValue && v <= short.MaxValue || kind.Auxiliary == 6 && v >= int.MinValue && v <= int.MaxValue, Status.InvalidArgument, "Enum underlying overflow");
                value = Enum.ToObject(type,v); break;
            }
            case 14: {
                ulong v = p->Scalar.U64; Require(kind.Auxiliary == 9 || kind.Auxiliary == 3 && v <= byte.MaxValue || kind.Auxiliary == 5 && v <= ushort.MaxValue || kind.Auxiliary == 7 && v <= uint.MaxValue, Status.InvalidArgument, "Enum underlying overflow");
                value = Enum.ToObject(type,v); break;
            }
            case 15: value = p->Scalar.Vector; break; case 16: value = p->Scalar.Quaternion; break;
            case 17: value = new ActorRef(p->Scalar.U64); break;
            case 18: value = new AssetRef((AssetKind)p->Auxiliary,NormalizeAsset(Decode(p->Text))); break;
            case 24:
                string[] referenceType = Decode(p->Text).Split('\n');
                Require(referenceType.Length == 2 && referenceType[0] == ReferenceType(type) &&
                    referenceType[1].Length > 0 && !referenceType[1].Any(char.IsControl), Status.FieldTypeMismatch, "Component reference type mismatch");
                value = Activator.CreateInstance(type, p->Scalar.U64, referenceType[1])!; break;
            case 25: value = ReadCollection(type, Decode(p->Text), depth); break;
            default: value = WriteUIScalar(kind.Tag, &p->Scalar); break;
        }
        if (kind.Tag is 13 or 14) ValidateEnum(type, value!);
        return value;
    }
}
