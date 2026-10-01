using System;
using System.Linq;

namespace Overload.UI;

public static class GraphicRegistry
{
    internal static IDisposable Register(Graphic graphic) => UISessionRegistry<Graphic>.For(graphic.Owner).Register(graphic,graphic);
    public static Graphic[] GetGraphicsForCanvas(Canvas canvas) {
        ArgumentNullException.ThrowIfNull(canvas);
        return UISessionRegistry<Graphic>.For(canvas.Owner).Snapshot().Where(g=>g.IsActive() && g.canvas?.Handle==canvas.Handle).ToArray();
    }
    public static Graphic[] GetRaycastableGraphicsForCanvas(Canvas canvas) => GetGraphicsForCanvas(canvas).Where(g=>g.raycastTarget && !g.canvasRenderer.cull).ToArray();
}

public static class GraphicRebuildTracker
{
    /// <summary>Advance renderer tint tweens on the main thread with the caller's scaled/unscaled deltas.</summary>
    public static void Update(Actor anchor, float deltaTime, float unscaledDeltaTime) {
        if(!float.IsFinite(deltaTime)||!float.IsFinite(unscaledDeltaTime)||deltaTime<0||unscaledDeltaTime<0)throw new ArgumentOutOfRangeException(nameof(deltaTime));
        CoroutineTween.TweenRunnerRegistry.Tick(deltaTime, unscaledDeltaTime);
        CoroutineScheduler.Tick(deltaTime, unscaledDeltaTime);
        foreach(var graphic in UISessionRegistry<Graphic>.For(anchor).Snapshot()) if(graphic.IsActive() && anchor.Access.Query(anchor.Handle,19,graphic.Owner.Handle)!=0)graphic.AdvanceTween(deltaTime,unscaledDeltaTime);
    }
    public static void RequestRebuild(Actor anchor) {
        foreach(var graphic in UISessionRegistry<Graphic>.For(anchor).Snapshot())
            if(graphic.IsActive() && anchor.Access.Query(anchor.Handle,19,graphic.Owner.Handle)!=0)graphic.OnRebuildRequested();
    }
    public static void TrackGraphic(Graphic graphic) { graphic.EnsureNativeComponents(); graphic.SetAllDirty(); }
    public static void UnTrackGraphic(Graphic graphic) => CanvasUpdateRegistry.UnRegisterCanvasElementForRebuild(graphic);
}

public interface ICanvasRaycastFilter { bool IsRaycastLocationValid(Overload.Maths.Vector2 localPoint, Actor? eventCamera); }
