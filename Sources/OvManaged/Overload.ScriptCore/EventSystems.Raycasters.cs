using System;
using System.Collections.Generic;
using System.Linq;
using Overload.Maths;
using Overload.UI;

namespace Overload.EventSystems;

public static class RaycasterManager
{
    public static BaseRaycaster[] GetRaycasters(Actor context) => UISessionRegistry<BaseRaycaster>.For(context).Snapshot()
        .Where(r => r.IsActive() && r.Owner.Access.Query(r.Owner.Handle,19,context.Handle) != 0).ToArray();
}
public abstract class BaseRaycaster : UIBehaviour
{
    private IDisposable? registration;
    public virtual Actor? eventCamera => null;
    public virtual float cameraDepth => 0;
    public virtual int sortOrderPriority => int.MinValue;
    public virtual int renderOrderPriority => int.MinValue;
    public virtual BaseRaycaster rootRaycaster {
        get {
            BaseRaycaster root = this;
            for (var a = Owner.Parent; a != null; a = a.Parent) if (a.GetComponent<BaseRaycaster>() is { } parent) root = parent;
            return root;
        }
    }
    public abstract void Raycast(PointerEventData eventData, List<RaycastResult> results);
    public override void OnEnable() => registration ??= UISessionRegistry<BaseRaycaster>.For(Owner).Register(this,this);
    public override void OnDisable() { registration?.Dispose(); registration = null; }
    public override void OnDestroy() => OnDisable();
    public override void OnReloadRelease() => OnDisable();
    public override void OnReloadRestore() { if (IsActive()) OnEnable(); }
    protected override void SetDirty() { }
    internal override void EnsureNativeComponents() { }
}

public readonly record struct UIRay(Vector3 origin, Vector3 direction, float maxDistance);
public readonly record struct UIPhysicsHit(Actor actor, float distance, Vector3 point, Vector3 normal, int sortingLayer = 0, int sortingOrder = 0);
public interface IUIPhysicsQueries
{
    void Raycast(UIRay ray, int layerMask, bool twoDimensional, List<UIPhysicsHit> results);
}
public interface IUIRayProjection
{
    bool ScreenPointToRay(Vector2 screenPoint, out UIRay ray);
}
public class PhysicsRaycaster : BaseRaycaster
{
    public IUIPhysicsQueries? queries { get; set; }
    public IUIRayProjection? projection { get; set; }
    public int eventMask { get; set; } = -1;
    public int cameraCullingMask { get; set; } = -1;
    public int finalEventMask => eventMask & cameraCullingMask;
    protected virtual bool twoDimensional => false;
    public override void OnEnable() { base.OnEnable(); queries ??= new NativeUIPhysicsQueries(Owner); projection ??= new NativeUIRayProjection(Owner); }
    public override void Raycast(PointerEventData eventData, List<RaycastResult> results) {
        if (queries == null || projection == null) throw new InvalidOperationException("PhysicsRaycaster needs the session physics/projection adapters");
        if (!projection.ScreenPointToRay(eventData.position,out var ray)) return;
        var hits = new List<UIPhysicsHit>(); queries.Raycast(ray,finalEventMask,twoDimensional,hits);
        hits.Sort((a,b) => a.distance.CompareTo(b.distance));
        foreach (var hit in hits) {
            if (!ReferenceEquals(hit.actor.Access.UIRegistry,Owner.Access.UIRegistry)) throw new InvalidOperationException("Cross-session physics hit");
            if (!ExecuteEvents.Live(hit.actor) || !float.IsFinite(hit.distance) || hit.distance < 0) continue;
            results.Add(new() { actor = hit.actor, module = this, distance = hit.distance, worldPosition = hit.point, worldNormal = hit.normal,
                sortingLayer = hit.sortingLayer, sortingOrder = hit.sortingOrder, screenPosition = eventData.position });
        }
    }
    public override void OnDestroy() { base.OnDestroy(); queries = null; projection = null; }
    public override void OnReloadRelease() => OnDestroy();
}
public class Physics2DRaycaster : PhysicsRaycaster { protected override bool twoDimensional => true; }

// B supplies one read-only hit view of its authoritative Graphic/clip/stencil
// state. F never creates a second render-frame format or guesses mask geometry.
public readonly record struct UIGraphicHit(Actor actor, float distance, int depth, int sortingLayer,
    int sortingOrder, Vector3 worldPosition, Vector3 worldNormal, bool reversed);
public interface IUIGraphicHitSource
{
    void GetHits(Actor canvasOwner, Vector2 screenPoint, List<UIGraphicHit> results);
    bool IsRaycastLocationValid(Actor graphicOwner, Vector2 screenPoint);
}
