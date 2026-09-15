using System;
using Overload;
using Overload.Maths;

namespace S4;
public enum Signed : sbyte { Min = sbyte.MinValue, Max = sbyte.MaxValue }
public enum Unsigned : ulong { Max = ulong.MaxValue }
public abstract class BaseFields : Behaviour { [SerializeField] private int inherited = 31; public int CheckInherited() => inherited; }
[ScriptId("s4-matrix")]
public class Matrix : BaseFields
{
    public bool Bool = true;
    public sbyte I8; public byte U8; public short I16; public ushort U16;
    public int I32; public uint U32; public long I64; public ulong U64;
    public float F32 = 1.25f; public double F64 = 2.5;
    public string Text = "default";
    public Signed SignedEnum = Signed.Min; public Unsigned UnsignedEnum = Unsigned.Max;
    public Vector3 Vector = new(1,2,3); public Quaternion Rotation = new(0,0,0,1);
    public ActorRef Target; public ActorRef External;
    public AssetRef Asset = new(AssetKind.Prefab, "");
    [FieldId("speed"), FormerlySerializedAs("OldSpeed")] public float Speed = 2;
    [SerializeField] private int secret = 17;
    [NonSerialized] public int Excluded = 4;
    public readonly int Readonly = 8;
    public static int Static;
    public int Property => throw new Exception("Property getter must never run");
    public int[] Unsupported = new[] { 1,2 };
    public string? NullText;
#if RENAMED
    [FormerlySerializedAs("RenameMe")] public int Renamed = 3;
    public string ChangeMe = "new type default";
#else
    public int RenameMe = 3;
    public int ChangeMe = 4;
    public long DeleteMe = 5;
#endif
#if DUPLICATE_FIELD
    [FieldId("speed")] public int Collision;
#endif
    public int ConstructorCount;
    public int ConstructorOrdinal;
    private static int nextConstructor;
    public Matrix() { ConstructorCount++; ConstructorOrdinal=++nextConstructor; }
    public float ObservedSpeed;
    public ulong ObservedTarget, ObservedExternal;
    public bool ObservedAsset;
    public override void OnAwake() {
        if(ConstructorCount!=1 || secret!=17 || CheckInherited()!=31) throw new Exception("Construction/default field restoration failed");
        ObservedSpeed=Speed;
        ObservedTarget=Target.Resolve(Owner)?.Guid ?? 0;
        ObservedExternal=External.Resolve(Owner)?.Guid ?? 0;
        ObservedAsset=Asset.Resolve(Owner);
        Transform.Position=new(Speed,0,0);
    }
    public override void OnUpdate(float dt) {
        Transform.Position=new(Transform.Position.X+Speed*dt,0,0);
        ObservedTarget=Target.Resolve(Owner)?.Guid ?? 0;
        Speed+=1;
    }
}
