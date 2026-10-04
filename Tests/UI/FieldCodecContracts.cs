using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Xml.Linq;
using Overload.Maths;
using Overload.UI;

namespace Overload.ManagedHost;

// Compiled with the production host sources: exercises the exact field codec without native mocks.
public static unsafe partial class Bootstrap
{
    private enum Choice : short { First = -2, Last = 7 }
    [Flags] private enum Bits : uint { None = 0, A = 1, B = 4 }
    private static int checks;
    private static int cleanupLogs;
    private sealed class CleanupProbe : EventSystems.UIBehaviour {
        public override void OnDestroy() => throw new InvalidOperationException("fixture destroy failure");
        internal override void ReleaseNativeComponents() => throw new InvalidOperationException("fixture native release failure");
    }
    private sealed class CleanupReceipt : IDisposable {
        public bool disposed;
        public void Dispose() => disposed = true;
    }
    private sealed class FieldExposureProbe : Behaviour {
        public int scalar = 3;
        public int[] pendingCollection = new[] { 1, 2 };
    }
    private static void FieldExposureContracts() {
        var previousServices = services;
        try {
            services.Log = &CaptureCleanupLog;
            var fields = DiscoverFields(typeof(FieldExposureProbe));
            Assert(fields.Count == 2 && fields.Any(f => f.Name == "pendingCollection"), "connected collection ABI is exposed to native Inspector");
        } finally { services = previousServices; }
    }
    [System.Runtime.InteropServices.UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void CaptureCleanupLog(uint level, Utf8 message) { ++cleanupLogs; }
    private static void CleanupContracts() {
        var previousServices = services;
        try {
            services.Log = &CaptureCleanupLog;
            var value = new CleanupProbe();
            var receipt = value.OwnSubscription(new CleanupReceipt());
            var type = new ProjectType(1, "cleanup", "tests", "CleanupProbe", new(), typeof(CleanupProbe), new());
            var instance = new Instance(value, 1, type) { Awake = true };
            Destroy(instance);
            Assert(receipt.disposed && instance.Destroyed && value.owner == null, "cleanup continues after both destroy callbacks fail");
            Assert(cleanupLogs == 2, "both cleanup failures are reported");
            Destroy(instance);
            Assert(cleanupLogs == 2, "destroy remains idempotent after failure");
        } finally { services = previousServices; }
    }
    private static void Assert(bool condition, string label) {
        ++checks; if (!condition) throw new Exception(label);
    }
    private static void Rejected(Action action, string label) {
        try { action(); } catch (Failure) { ++checks; return; }
        throw new Exception("Accepted invalid value: " + label);
    }
    private static object? DecodeValue(Type type, FieldValue value, string text) {
        byte[] bytes = Encoding.GetBytes(text);
        fixed (byte* data = bytes) {
            value.Text = new Utf8 { Data = data, Length = (ulong)bytes.Length };
            return UnpackField(type, &value);
        }
    }
    private static void Roundtrip(Type type, object? input) {
        FieldValue first = PackField(type, input, out string text);
        object? restored = DecodeValue(type, first, text);
        FieldValue second = PackField(type, restored, out string again);
        Assert(text == again && first.Tag == second.Tag && first.Auxiliary == second.Auxiliary &&
            new ReadOnlySpan<byte>(&first.Scalar, 16).SequenceEqual(new ReadOnlySpan<byte>(&second.Scalar, 16)), "canonical roundtrip " + type);
        if (input is IList) Assert(!ReferenceEquals(input, restored), "fresh collection storage");
    }
    private static void BadList<T>(T[] value, Action<XElement> change, string label) {
        FieldValue packed = PackField(typeof(T[]), value, out string text);
        XElement xml = XElement.Parse(text); change(xml);
        Rejected(() => DecodeValue(typeof(T[]), packed, xml.ToString(SaveOptions.DisableFormatting)), label);
    }
    public static int Main() {
        CleanupContracts();
        FieldExposureContracts();
        Roundtrip(typeof(char), '密');
        Roundtrip(typeof(ColorBlock), ColorBlock.defaultColorBlock);
        Roundtrip(typeof(Dropdown.OptionDataList), new Dropdown.OptionDataList { options = new() { new("中文😀"), new("Beta") } });
        Roundtrip(typeof(SpriteState), default(SpriteState));
        Roundtrip(typeof(bool[]), new[] { true, false });
        Roundtrip(typeof(sbyte[]), new[] { sbyte.MinValue, sbyte.MaxValue });
        Roundtrip(typeof(byte[]), new byte[] { 0, 255 });
        Roundtrip(typeof(short[]), new[] { short.MinValue, short.MaxValue });
        Roundtrip(typeof(ushort[]), new[] { ushort.MinValue, ushort.MaxValue });
        Roundtrip(typeof(int[]), new[] { int.MinValue, int.MaxValue });
        Roundtrip(typeof(uint[]), new[] { uint.MinValue, uint.MaxValue });
        Roundtrip(typeof(long[]), new[] { long.MinValue, long.MaxValue });
        Roundtrip(typeof(ulong[]), new[] { ulong.MinValue, ulong.MaxValue });
        Roundtrip(typeof(float[]), new[] { float.MinValue, -0f, float.MaxValue });
        Roundtrip(typeof(double[]), new[] { double.MinValue, double.MaxValue });
        Roundtrip(typeof(string[]), new[] { "", "中文\0<&\"\n😀" });
        Roundtrip(typeof(Choice[]), new[] { Choice.First, Choice.Last });
        Roundtrip(typeof(Bits[]), new[] { Bits.None, Bits.A | Bits.B });
        Roundtrip(typeof(Vector2[]), new[] { new Vector2(-3, 4) });
        Roundtrip(typeof(Vector3[]), new[] { new Vector3(-3, 4, 5) });
        Roundtrip(typeof(Vector4[]), new[] { new Vector4(-3, 4, 5, 6) });
        Roundtrip(typeof(Quaternion[]), new[] { new Quaternion(0, 0, 0, 1) });
        Roundtrip(typeof(Rect[]), new[] { new Rect(new(-3, 4), new(80, 50)) });
        Roundtrip(typeof(RectOffset[]), new[] { new RectOffset(-3, 4, 5, 6) });
        Roundtrip(typeof(Color[]), new[] { new Color(-2, 3, .5f, 1) });
        Roundtrip(typeof(ActorRef[]), new[] { new ActorRef(0), new ActorRef(ulong.MaxValue) });
        Roundtrip(typeof(AssetRef[]), new[] { new AssetRef(AssetKind.Font, "Fonts/字.ttf"), default(AssetRef) });
        Roundtrip(typeof(ComponentRef<RectTransform>[]), new[] { default(ComponentRef<RectTransform>), new ComponentRef<RectTransform>(42) });
        Roundtrip(typeof(List<int>), new List<int> { 2, -6, 91 });
        Roundtrip(typeof(List<int[]>), new List<int[]> { new[] { 1, 2 }, Array.Empty<int>() });
        Roundtrip(typeof(int[][]), new int[][] { null!, new[] { 3 }, Array.Empty<int>() });
        Roundtrip(typeof(int[]), null);
        Roundtrip(typeof(List<string>), null);
        Roundtrip(typeof(int[]), Array.Empty<int>());
        Type nested = typeof(int); object? nestedValue = 1;
        for (int i = 0; i < 8; ++i) { var array = Array.CreateInstance(nested, 0); nested = nested.MakeArrayType(); nestedValue = array; }
        Assert(DescribeField(nested).Tag == 25, "depth eight supported");
        // Empty nesting still encodes its default chain; its total bytes remain bounded.
        Roundtrip(nested, nestedValue);
        Assert(DescribeField(nested.MakeArrayType()).Tag == 0, "depth nine unsupported");
        Assert(DescribeField(typeof(int[,])).Tag == 0, "multidimensional array unsupported");
        Assert(DescribeField(typeof(List<object>)).Tag == 0, "arbitrary objects unsupported");
        Rejected(() => PackField(typeof(int[]), new int[1025], out _), "item count");
        Rejected(() => PackField(typeof(string[]), new[] { new string('x', 40000) }, out _), "aggregate encoded bytes");
        Rejected(() => PackField(typeof(float[]), new[] { float.NaN }, out _), "NaN");
        Rejected(() => PackField(typeof(Vector2[]), new[] { new Vector2(1, float.PositiveInfinity) }, out _), "nonfinite vector");
        Rejected(() => PackField(typeof(Choice[]), new[] { (Choice)3 }, out _), "enum membership");
        Rejected(() => PackField(typeof(Bits[]), new[] { (Bits)2 }, out _), "enum flags");
        Rejected(() => PackField(typeof(AssetRef[]), new[] { new AssetRef(AssetKind.Texture, "../bad") }, out _), "asset traversal");
        BadList(new[] { 1 }, x => x.SetAttributeValue("schema", "array:System.Single"), "schema change on reload");
        BadList(new[] { 1 }, x => x.SetAttributeValue("null", "1"), "null with elements");
        BadList(new[] { 1 }, x => x.Element("default")!.Remove(), "missing default");
        BadList(new[] { 1 }, x => x.Element("item")!.SetAttributeValue("scalar", "00"), "short scalar");
        BadList(new[] { 1 }, x => x.Element("item")!.SetAttributeValue("tag", 10), "forged tag");
        BadList(new[] { 1 }, x => x.Element("item")!.SetAttributeValue("auxiliary", 1), "forged auxiliary");
        BadList(new[] { true }, x => x.Element("item")!.SetAttributeValue("scalar", "02000000000000000000000000000000"), "invalid bool");
        BadList(new[] { 1f }, x => x.Element("item")!.SetAttributeValue("scalar", "0000C07F00000000000000000000000000"), "incoming NaN");
        BadList(new[] { Choice.First }, x => x.Element("item")!.SetAttributeValue("scalar", "03000000000000000000000000000000"), "incoming enum");
        BadList(new[] { "ok" }, x => x.Element("item")!.Element("text")!.Value = "GG", "invalid hex");
        BadList(new[] { new ComponentRef<RectTransform>(4) }, x => {
            var node = x.Element("item")!.Element("text")!;
            node.Value = Convert.ToHexString(Encoding.GetBytes("native:Overload.Canvas\nnative:Overload.Canvas"));
        }, "component declared type mismatch");
        var baseValue = PackField(typeof(int[]), new[] { 1 }, out string baseText);
        Rejected(() => DecodeValue(typeof(List<int>), baseValue, baseText), "array/list migration mismatch");
        Rejected(() => DecodeValue(typeof(int[]), baseValue, "<!DOCTYPE list [<!ENTITY x SYSTEM 'file:///H:/private'>]><list>&x;</list>"), "external entity");
        var random = new Random(1701);
        for (int i = 0; i < 250; ++i) Roundtrip(typeof(List<int[]>), Enumerable.Range(0, random.Next(1, 8))
            .Select(_ => Enumerable.Range(0, random.Next(0, 12)).Select(_ => random.Next()).ToArray()).ToList());
        Console.WriteLine($"PASS field codec: {checks} assertions; production scalar/array/list codec; no Inspector/runtime claim");
        return 0;
    }
}
