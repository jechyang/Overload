using System;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using Overload;

namespace GeneratedFieldsValidation;

#if GENERATOR_PROJECT
public abstract class BaseFields : Behaviour
{
    [SerializeField, FieldId("base-id")] private int inherited = 31;
    public int Inherited => inherited;
}
public sealed class Fields : BaseFields
{
    [SerializeField, FieldId("speed"), FormerlySerializedAs("old-speed"), InspectorName("Movement Speed")]
    private float velocity = 2;
    public float Velocity => velocity;
    public int[] Items = [1, 2];
    public string? Text;
    public State Mode = State.Max;
    [field: SerializeField, FieldId("auto")] public int Automatic { get; set; } = 13;
    public readonly int Readonly = 7;
    [NonSerialized] public int Excluded;
    public static int Static;
    public int DangerousProperty => throw new Exception("Property getters must not run");
}
public enum State : ulong { Min, Max = ulong.MaxValue }
public sealed class UnsupportedFields : Behaviour { public dynamic Anything = 1; }
[Serializable] public struct Record { public int Count; public string Name; public Record() { Count = 0; Name = ""; } }
// A precompiled declaration without a generated schema remains compatible.
public sealed class Legacy { [SerializeField] private int number = 9; public int Number => number; }
public sealed class Inaccessible : Behaviour
{
    [Serializable] private sealed class Nested { public int Count = 3; }
    [SerializeField] private Nested data = new();
    public int Count => data.Count;
}
#else
public static class Program
{
    private sealed class ProjectContext() : AssemblyLoadContext(isCollectible: true)
    {
        protected override Assembly? Load(AssemblyName name) => name.Name == typeof(Behaviour).Assembly.GetName().Name ? typeof(Behaviour).Assembly : null;
    }
    private static int checks;
    private static void Check(bool condition, string name) {
        if (!condition) throw new Exception(name);
        ++checks; Console.WriteLine("PASS: " + name);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static bool Alive(WeakReference weak) => weak.IsAlive;
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference LoadAndRelease(int mode) {
        var context = new ProjectContext();
        var assembly = context.LoadFromAssemblyPath(typeof(Fields).Assembly.Location);
        Check(context.IsCollectible && assembly.IsCollectible && ReferenceEquals(AssemblyLoadContext.GetLoadContext(assembly), context), "isolated collectible assembly loaded");
        if (mode > 0) {
            var type = assembly.GetType(typeof(Fields).FullName!)!;
            var instance = Activator.CreateInstance(type)!;
            Check(GeneratedFieldSchema.IsGenerated(type), "collectible project uses generated schema");
            var descriptor = GeneratedFieldSchema.DeclaredFields(type).Single(f => f.Id == "speed");
            if (mode > 1) {
                descriptor.Write(instance, 5f);
                Check((float)descriptor.Read(instance)! == 5f, "collectible private accessor works");
            }
        }
        var weak = new WeakReference(context, trackResurrection: true); context.Unload(); return weak;
    }
    public static void Main() {
        Check(GeneratedFieldSchema.IsGenerated(typeof(Fields)), "compiled schema selected");
        var fields = GeneratedFieldSchema.DeclaredFields(typeof(Fields));
        Check(fields.Select(f => f.Name).SequenceEqual(new[] { "velocity", "Items", "Text", "Mode", "<Automatic>k__BackingField" }), "declaration order and excluded fields");
        var instance = new Fields(); var speed = fields[0];
        Check(speed.Id == "speed" && speed.DisplayName == "Movement Speed" && speed.Aliases.SequenceEqual(new[] { "old-speed", "velocity" }), "stable identity, aliases and display name");
        Check((float)speed.Read(instance)! == 2, "private initializer retained");
        speed.Write(instance, 7f); Check(instance.Velocity == 7, "private field written directly");
        Check(GeneratedFieldSchema.IsGenerated(typeof(BaseFields)), "abstract base schema generated");
        var inherited = GeneratedFieldSchema.DeclaredFields(typeof(BaseFields))[0]; inherited.Write(instance, 42);
        Check(instance.Inherited == 42 && inherited.Id == "base-id", "inherited private field accessor");
        fields[1].Write(instance, new int[] { 4, 5 }); Check(instance.Items.SequenceEqual(new[] { 4, 5 }), "array field access");
        fields[2].Write(instance, null); Check(fields[2].Read(instance) == null, "nullable field access");
        fields[3].Write(instance, State.Max); Check((State)fields[3].Read(instance)! == State.Max, "64-bit enum field access");
        var automatic = fields.Single(f => f.Id == "auto"); automatic.Write(instance, 23);
        Check(instance.Automatic == 23 && (int)automatic.Read(instance)! == 23, "explicitly annotated backing field preserved");
        object record = new Record(); var members = GeneratedFieldSchema.DeclaredFields(typeof(Record));
        members.Single(f => f.Name == "Count").Write(record, 8);
        Check(((Record)record).Count == 8, "boxed struct updated by reference");
        Check(GeneratedFieldSchema.IsGenerated(typeof(Record)), "record schema generated");
        Check(GeneratedFieldSchema.DeclaredFields(typeof(UnsupportedFields))[0].FieldType == typeof(object), "dynamic metadata uses its erased runtime type");
        var host = typeof(Overload.ManagedHost.Bootstrap);
        var writeRecord = host.GetMethod("WriteRecord", BindingFlags.Static | BindingFlags.NonPublic)!;
        var readRecord = host.GetMethod("ReadRecord", BindingFlags.Static | BindingFlags.NonPublic)!;
        string xml = (string)writeRecord.Invoke(null, new object[] { typeof(Record), new Record { Count = 19, Name = "<&>" }, 0 })!;
        var restored = (Record)readRecord.Invoke(null, new object[] { typeof(Record), xml, 0 })!;
        Check(restored.Count == 19 && restored.Name == "<&>", "host record XML codec uses generated boxed field access");
        var writeCollection = host.GetMethod("WriteCollection", BindingFlags.Static | BindingFlags.NonPublic)!;
        var readCollection = host.GetMethod("ReadCollection", BindingFlags.Static | BindingFlags.NonPublic)!;
        xml = (string)writeCollection.Invoke(null, new object[] { typeof(Record[]), new Record[] { restored, new() { Count = -7, Name = "next" } }, 0 })!;
        var restoredItems = (Record[])readCollection.Invoke(null, new object[] { typeof(Record[]), xml, 0 })!;
        Check(restoredItems.Length == 2 && restoredItems[0].Name == "<&>" && restoredItems[1].Count == -7, "host collection XML codec roundtrips generated records");
        Check(!GeneratedFieldSchema.IsGenerated(typeof(Legacy)), "legacy schema fallback selected");
        var legacy = new Legacy(); GeneratedFieldSchema.DeclaredFields(typeof(Legacy))[0].Write(legacy, 11);
        Check(legacy.Number == 11, "legacy private field preserved");
        Check(!GeneratedFieldSchema.IsGenerated(typeof(Inaccessible)) && GeneratedFieldSchema.DeclaredFields(typeof(Inaccessible)).Length == 1,
            "inaccessible nested type retained by compatibility schema");
        Check(GeneratedFieldSchema.IsGenerated(typeof(Overload.UI.Button)), "built-in UI schema generated");
        var button = new Overload.UI.Button();
        var interactable = GeneratedFieldSchema.DeclaredFields(typeof(Overload.UI.Selectable)).Single(f => f.Name == "m_Interactable");
        interactable.Write(button, false);
        Check(!(bool)interactable.Read(button)!, "built-in UI inherited private field uses generated access");
        for (int mode = 0; mode < 3; ++mode) {
            WeakReference weak = null!;
            int scenario = mode;
            var thread = new System.Threading.Thread(() => weak = LoadAndRelease(scenario));
            thread.Start(); thread.Join();
            for (int i = 0; i < 30 && Alive(weak); ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); System.Threading.Thread.Sleep(10); }
            Check(!Alive(weak), "collectible project unload mode " + mode);
        }
        Console.WriteLine($"PASS: {checks} generated field checks");
    }
}
#endif
