using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using V3 = Overload.Maths.Vector3;
using Q4 = Overload.Maths.Quaternion;

namespace Overload.ManagedHost;

public static unsafe partial class Bootstrap
{
    [StructLayout(LayoutKind.Explicit, Size = 16)]
    private struct Scalar
    {
        [FieldOffset(0)] internal byte B;
        [FieldOffset(0)] internal sbyte I8;
        [FieldOffset(0)] internal short I16;
        [FieldOffset(0)] internal ushort U16;
        [FieldOffset(0)] internal int I32;
        [FieldOffset(0)] internal uint U32;
        [FieldOffset(0)] internal long I64;
        [FieldOffset(0)] internal ulong U64;
        [FieldOffset(0)] internal float F32;
        [FieldOffset(0)] internal double F64;
        [FieldOffset(0)] internal V3 Vector;
        [FieldOffset(0)] internal Q4 Quaternion;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    private struct FieldValue { internal uint Size, Tag, Flags, Auxiliary; internal Scalar Scalar; internal Utf8 Text; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    private struct FieldResult {
        internal uint Size, Flags; internal ulong Token; internal uint Tag, Auxiliary;
        internal Utf8 Id, Name, Display; internal uint AliasCount, Reserved;
    }
    private sealed record ProjectField(ulong Token, string Id, string Name, List<string> Aliases, uint Tag, uint Auxiliary, FieldInfo Field);
    private static ulong nextField;
    private static readonly Dictionary<nuint, FieldResult> FieldResults = new();
    private static readonly Dictionary<nuint, FieldValue> FieldValues = new();
    private static uint Tag(Type t) => t == typeof(bool) ? 1u : t == typeof(sbyte) ? 2u : t == typeof(byte) ? 3u :
        t == typeof(short) ? 4u : t == typeof(ushort) || t == typeof(char) ? 5u : t == typeof(int) ? 6u : t == typeof(uint) ? 7u :
        t == typeof(long) ? 8u : t == typeof(ulong) ? 9u : t == typeof(float) ? 10u : t == typeof(double) ? 11u :
        t == typeof(string) ? 12u : t == typeof(V3) ? 15u : t == typeof(Q4) ? 16u : t == typeof(ActorRef) ? 17u : t == typeof(AssetRef) ? 18u : UITag(t);
    private static List<ProjectField> DiscoverFields(Type type)
    {
        List<ProjectField> fields = new(); HashSet<string> keys = new(StringComparer.Ordinal);
        for (Type? t = type; t != null && t != typeof(Behaviour); t = t.BaseType)
        foreach (FieldInfo f in t.GetFields(BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly).OrderBy(f => f.MetadataToken)) {
            if (f.IsStatic || f.IsInitOnly || f.IsLiteral || f.IsDefined(typeof(NonSerializedAttribute)) ||
                (!f.IsPublic && !f.IsDefined(typeof(SerializeFieldAttribute)))) continue;
            var (tag, auxiliary) = DescribeField(f.FieldType);
            if (tag == 0) { Log($"UnsupportedField: {type.FullName}.{f.Name} ({f.FieldType.FullName}); serialized data retained"); continue; }
            if (tag == 24) _ = ReferenceType(f.FieldType);
            string id = f.GetCustomAttribute<FieldIdAttribute>()?.Id ?? f.Name; IdentityText(id);
            List<string> aliases = f.GetCustomAttributes<FormerlySerializedAsAttribute>().Select(a => a.Name).ToList();
            if (id != f.Name && !aliases.Contains(f.Name, StringComparer.Ordinal)) aliases.Add(f.Name);
            foreach (string key in new[] { id }.Concat(aliases)) {
                IdentityText(key); Require(keys.Add(key), Status.DuplicateIdentity, $"Duplicate field ID/alias {type.FullName}.{key}");
            }
            fields.Add(new(checked(++nextField), id, f.Name, aliases, tag, auxiliary, f));
        }
        return fields;
    }
    private static ProjectType FindType(SessionHandle s, ulong token) => Lookup(s).Types.Find(t => t.Token == token) ?? throw new Failure(Status.TypeNotFound, "Unknown type token");
    private static ProjectField FindField(ProjectType type, ulong token) => type.Fields.Find(f => f.Token == token) ?? throw new Failure(Status.FieldNotFound, "Unknown field token for this type");
    private static void GetFieldResult(SessionHandle s, ulong token, uint index, FieldResult* p) {
        Require(p != null, Status.InvalidArgument, "Null field output"); *p = default;
        ProjectType type = FindType(s, token); Require(index < type.Fields.Count, Status.FieldNotFound, "Field index out of range");
        ProjectField f = type.Fields[(int)index];
        FieldResult r = new() { Size = 80, Token = f.Token, Tag = f.Tag, Auxiliary = f.Auxiliary, AliasCount = (uint)f.Aliases.Count };
        try { r.Id = Allocate(f.Id); r.Name = Allocate(f.Name); r.Display = Allocate(f.Field.GetCustomAttribute<InspectorNameAttribute>()?.DisplayName ?? f.Name); FieldResults.Add((nuint)r.Id.Data, r); *p = r; }
        catch { Free(r.Id); Free(r.Name); Free(r.Display); throw; }
    }
    private static void ReleaseFieldResult(FieldResult* p) {
        Require(p != null, Status.InvalidArgument, "Null field result");
        Require(FieldResults.TryGetValue((nuint)p->Id.Data, out var r) && new ReadOnlySpan<byte>(p,80).SequenceEqual(new ReadOnlySpan<byte>(&r,80)), Status.BufferOwnership, "Unknown/copied/modified field result");
        foreach (var text in new[] { r.Id, r.Name, r.Display }) Require(Buffers.TryGetValue((nuint)text.Data, out var size) && size == text.Length, Status.BufferOwnership, "Field member already released");
        FieldResults.Remove((nuint)p->Id.Data); Free(r.Id); Free(r.Name); Free(r.Display); *p = default;
    }
    private static void ReadFieldValue(SessionHandle s, InstanceHandle id, ulong token, FieldValue* p) {
        Require(p != null, Status.InvalidArgument, "Null value output"); *p = default;
        Instance i = FindInstance(s,id); Require(!i.Destroyed, Status.InvalidLifecycle, "Destroyed instance");
        ProjectField f = FindField(i.Type,token);
        object? value = f.Field.GetValue(i.Value); // Only cached fields, never property getters.
        FieldValue r = PackField(f.Field.FieldType, value, out string text);
        // Even scalar/empty-string results own a unique zero-length allocation receipt.
        r.Text = Allocate(text);
        if (r.Text.Data == null) { r.Text.Data = (byte*)NativeMemory.Alloc(1); Buffers.Add((nuint)r.Text.Data,0); }
        try { FieldValues.Add((nuint)r.Text.Data,r); *p = r; } catch { Free(r.Text); throw; }
    }
    private static void ReleaseFieldValue(FieldValue* p) {
        Require(p != null, Status.InvalidArgument, "Null field value");
        Require(FieldValues.TryGetValue((nuint)p->Text.Data,out var r) && new ReadOnlySpan<byte>(p,48).SequenceEqual(new ReadOnlySpan<byte>(&r,48)), Status.BufferOwnership, "Unknown/copied/modified value result");
        Require(Buffers.TryGetValue((nuint)r.Text.Data,out var size) && size == r.Text.Length, Status.BufferOwnership, "Value text already released");
        FieldValues.Remove((nuint)p->Text.Data); Free(r.Text); *p = default;
    }
    private static string NormalizeAsset(string path) {
        path = path.Replace('\\','/');
        Require(!path.StartsWith('/') && !path.Contains(':') && !path.Any(char.IsControl) && !path.Split('/').Contains(".."), Status.InvalidArgument, "Asset path must be project-relative without traversal");
        return string.Join('/', path.Split('/', StringSplitOptions.RemoveEmptyEntries).Where(s => s != "."));
    }
    private static void WriteFieldValue(SessionHandle s, InstanceHandle id, ulong token, FieldValue* p) {
        Require(p != null && p->Size == 48 && p->Flags == 0, Status.InvalidArgument, "Invalid field value layout/flags");
        Instance i = FindInstance(s,id); Require(!i.Destroyed && !i.Faulted, Status.InstanceFaulted, "Destroyed/faulted instance");
        ProjectField f = FindField(i.Type,token);
        object? value = UnpackField(f.Field.FieldType, p);
        f.Field.SetValue(i.Value,value);
        if (i.Value is EventSystems.UIBehaviour ui) ui.MarkDirtyFromHost();
    }
}
