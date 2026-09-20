using System;
using System.Collections.Generic;
using Overload.EventSystems;

namespace Overload.UI;

public class GraphicRaycaster : BaseRaycaster
{
    public enum BlockingObjects { None, TwoD, ThreeD, All }
    public bool ignoreReversedGraphics { get; set; } = true;
    public BlockingObjects blockingObjects { get; set; }
    public int blockingMask { get; set; } = -1;
    public IUIGraphicHitSource? hitSource { get; set; } = new GraphicHitSource();
    public IUIPhysicsQueries? physics { get; set; }
    public IUIRayProjection? projection { get; set; }
    public override void OnEnable() { base.OnEnable(); hitSource ??= new GraphicHitSource(); physics ??= new NativeUIPhysicsQueries(Owner); projection ??= new NativeUIRayProjection(Owner); }
    public override Actor? eventCamera => Owner.GetComponent<Canvas>()?.worldCamera;
    public override int sortOrderPriority => Owner.GetComponent<Canvas>()?.sortingOrder ?? 0;
    public override void Raycast(PointerEventData eventData, List<RaycastResult> results) {
        var source = hitSource ?? throw new InvalidOperationException("GraphicRaycaster requires B's Graphic/clip hit source");
        float blockingDistance = float.PositiveInfinity;
        var canvas = Owner.GetComponent<Canvas>();
        if (blockingObjects != BlockingObjects.None && canvas?.renderMode != RenderMode.ScreenSpaceOverlay) {
            if (physics == null || projection == null) throw new InvalidOperationException("Blocking objects require physics and camera projection");
            if (!projection.ScreenPointToRay(eventData.position,out var ray)) return;
            var blocks = new List<UIPhysicsHit>();
            if (blockingObjects is BlockingObjects.TwoD or BlockingObjects.All) physics.Raycast(ray,blockingMask,true,blocks);
            if (blockingObjects is BlockingObjects.ThreeD or BlockingObjects.All) physics.Raycast(ray,blockingMask,false,blocks);
            foreach (var hit in blocks) if (float.IsFinite(hit.distance) && hit.distance >= 0) blockingDistance = Math.Min(blockingDistance,hit.distance);
        }
        var hits = new List<UIGraphicHit>(); source.GetHits(Owner,eventData.position,hits);
        foreach (var hit in hits) {
            if (!ReferenceEquals(hit.actor.Access.UIRegistry,Owner.Access.UIRegistry)) throw new InvalidOperationException("Cross-session graphic hit");
            if (!ExecuteEvents.Live(hit.actor) || hit.depth < 0 || !float.IsFinite(hit.distance) || hit.distance < 0 ||
                hit.distance >= blockingDistance || ignoreReversedGraphics && hit.reversed || !source.IsRaycastLocationValid(hit.actor,eventData.position)) continue;
            results.Add(new() { actor = hit.actor, module = this, distance = hit.distance, depth = hit.depth,
                sortingLayer = hit.sortingLayer, sortingOrder = hit.sortingOrder, worldPosition = hit.worldPosition,
                worldNormal = hit.worldNormal, screenPosition = eventData.position });
        }
    }
    public override void OnDestroy() { base.OnDestroy(); hitSource = null; physics = null; projection = null; }
    public override void OnReloadRelease() => OnDestroy();
}
