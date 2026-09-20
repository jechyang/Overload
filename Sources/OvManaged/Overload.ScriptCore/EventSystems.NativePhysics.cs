using System;
using System.Collections.Generic;
using System.Globalization;
using Overload.Maths;
using Overload.UI;

namespace Overload.EventSystems;

public sealed class NativeUIRayProjection(Actor owner) : IUIRayProjection
{
    public bool ScreenPointToRay(Vector2 screenPoint, out UIRay ray) {
        ulong packed = NativeUIComponent.Pack(screenPoint);
        Vector2 Read(string name) => NativeUIComponent.Unpack(owner.Access.Query(owner.Handle,88,packed,name));
        if (owner.Access.Query(owner.Handle,88,packed,"valid") == 0) { ray = default; return false; }
        var xy = Read("originXY"); var zx = Read("originZdirectionX"); var yz = Read("directionYZ");
        ray = new(new(xy.x,xy.y,zx.x),new(zx.y,yz.x,yz.y),Read("distance").x); return true;
    }
}

/// <summary>Minimal native 2D query shapes; this component does not simulate rigid bodies.</summary>
public class UIPhysicsCollider2D : UIBehaviour
{
    public enum Shape { Box, Circle }
    [SerializeField] private Shape m_Shape;
    [SerializeField] private Vector2 m_Size = new(1,1);
    [SerializeField] private Vector2 m_Offset;
    [SerializeField] private float m_Rotation;
    [SerializeField] private int m_Layer;
    private IDisposable? registration;
    public Shape shape { get => m_Shape; set => m_Shape = value; }
    public Vector2 size { get => m_Size; set => m_Size = value; }
    public Vector2 offset { get => m_Offset; set => m_Offset = value; }
    public float rotationRadians { get => m_Rotation; set => m_Rotation = value; }
    public int layer { get => m_Layer; set { if (value < 0 || value > 31) throw new ArgumentOutOfRangeException(nameof(value)); m_Layer = value; } }
    public int sortingLayer { get; set; }
    public int sortingOrder { get; set; }
    public override void OnEnable() => registration ??= UISessionRegistry<UIPhysicsCollider2D>.For(Owner).Register(this,this);
    public override void OnDisable() { registration?.Dispose(); registration = null; }
    public override void OnDestroy() => OnDisable();
    public override void OnReloadRelease() => OnDisable();
    public override void OnReloadRestore() { if (IsActive()) OnEnable(); }
    protected override void SetDirty() { }
    internal override void EnsureNativeComponents() { }
}

public sealed class NativeUIPhysicsQueries(Actor owner) : IUIPhysicsQueries
{
    private static string Number(float x) => x.ToString("R",CultureInfo.InvariantCulture);
    private static Vector2 Pair(ulong bits) => new(BitConverter.UInt32BitsToSingle((uint)bits),BitConverter.UInt32BitsToSingle((uint)(bits >> 32)));
    public void Raycast(UIRay ray, int layerMask, bool twoDimensional, List<UIPhysicsHit> results) {
        var o = ray.origin; var d = ray.direction;
        float length = MathF.Sqrt(d.X*d.X+d.Y*d.Y+d.Z*d.Z);
        if (!float.IsFinite(length) || length <= 0 || !float.IsFinite(ray.maxDistance) || ray.maxDistance < 0) throw new ArgumentException("Invalid physics ray");
        d = new(d.X/length,d.Y/length,d.Z/length);
        string query = string.Join(" ",Number(o.X),Number(o.Y),Number(o.Z),Number(d.X),Number(d.Y),Number(d.Z),Number(ray.maxDistance));
        if (twoDimensional) {
            foreach (var collider in UISessionRegistry<UIPhysicsCollider2D>.For(owner).Snapshot()) {
                if (!collider.IsActive() || (layerMask & (1 << collider.layer)) == 0) continue;
                var c = collider.Transform.Position; var offset = collider.offset; var size = collider.size;
                string request = query + " " + ((int)collider.shape).ToString(CultureInfo.InvariantCulture) + " " +
                    string.Join(" ",Number(c.X+offset.x),Number(c.Y+offset.y),Number(c.Z),Number(size.x/2),Number(size.y/2),Number(collider.rotationRadians));
                double distance = BitConverter.UInt64BitsToDouble(owner.Access.Query(collider.Owner.Handle,85,0,request));
                if (distance < 0) continue;
                float t = (float)distance;
                results.Add(new(collider.Owner,t,new(o.X+d.X*t,o.Y+d.Y*t,o.Z+d.Z*t),new(0,0,d.Z > 0 ? -1 : 1),collider.sortingLayer,collider.sortingOrder));
            }
        } else {
            query += " " + layerMask.ToString(CultureInfo.InvariantCulture);
            ulong count = owner.Access.Query(owner.Handle,86,0,query);
            if (count > 65536) throw new InvalidOperationException("Physics hit limit");
            for (ulong index = 0; index < count; ++index) {
                ulong Read(ulong field) => owner.Access.Query(owner.Handle,87,index*8+field,query);
                Actor? actor = owner.Find(Read(0)); if (actor == null) continue;
                float distance = (float)BitConverter.UInt64BitsToDouble(Read(1)); var p = Pair(Read(2)); var pn = Pair(Read(3)); var n = Pair(Read(4));
                results.Add(new(actor,distance,new(p.x,p.y,pn.x),new(pn.y,n.x,n.y)));
            }
        }
    }
}
