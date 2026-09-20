using System;
using System.Collections.Generic;
using System.Linq;
using Overload.Maths;

namespace Overload.UI;

// Owned by the scripting session, never by static roots in the resident ScriptCore ALC.
public sealed class CanvasUpdateRegistry
{
    private readonly HashSet<ICanvasElement> layout = new(), graphics = new();
    private readonly Dictionary<Type, IUISessionResource> sessionResources = new();
    internal UISessionRegistry<T> GetRegistry<T>() where T : class {
        if (!sessionResources.TryGetValue(typeof(T), out IUISessionResource? registry))
            sessionResources.Add(typeof(T), registry = new UISessionRegistry<T>(this));
        return (UISessionRegistry<T>)registry;
    }
    private readonly LayoutChangeObserver layoutChanges = new();
    private readonly HashSet<ulong> executingLayouts = new();
    internal bool EnterLayout(ulong handle) => executingLayouts.Add(handle);
    internal void ExitLayout(ulong handle) => executingLayouts.Remove(handle);
    internal bool rebuildingLayout, rebuildingGraphics;
    public static bool IsRebuildingLayout(Actor owner) => owner.Access.UIRegistry.rebuildingLayout || owner.Access.UIRegistry.executingLayouts.Count != 0;
    public static bool IsRebuildingGraphics(Actor owner) => owner.Access.UIRegistry.rebuildingGraphics;
    public static bool TryRegisterCanvasElementForLayoutRebuild(ICanvasElement element) => element.transform.Owner.Access.UIRegistry.layout.Add(element);
    public static bool TryRegisterCanvasElementForGraphicRebuild(ICanvasElement element) => element.transform.Owner.Access.UIRegistry.graphics.Add(element);
    public static void RegisterCanvasElementForLayoutRebuild(ICanvasElement element) => TryRegisterCanvasElementForLayoutRebuild(element);
    public static void RegisterCanvasElementForGraphicRebuild(ICanvasElement element) => TryRegisterCanvasElementForGraphicRebuild(element);
    public static void UnRegisterCanvasElementForRebuild(ICanvasElement element) {
        var registry = element.transform.Owner.Access.UIRegistry;
        registry.layout.Remove(element); registry.graphics.Remove(element);
    }
    internal void Clear() {
        layout.Clear(); graphics.Clear(); layoutChanges.Clear(); executingLayouts.Clear();
        foreach (IUISessionResource resource in sessionResources.Values) resource.Release();
        sessionResources.Clear();
    }
    private static int Depth(ICanvasElement element) {
        int depth = 0; for (Actor? p = element.transform.Owner.Parent; p != null; p = p.Parent) ++depth; return depth;
    }
    internal void PerformUpdate(Action<Exception> report, Action afterLayout) {
        if (rebuildingLayout || rebuildingGraphics) return;
        void Run(HashSet<ICanvasElement> queue, CanvasUpdate first, CanvasUpdate last, bool isLayout) {
            var pending = queue.ToArray(); queue.Clear();
            var active = new List<ICanvasElement>();
            foreach (var item in pending) try { if (!item.IsDestroyed()) active.Add(item); } catch (Exception ex) { report(ex); }
            if (isLayout) active = active.OrderBy(Depth).ToList();
            var failed = new HashSet<ICanvasElement>();
            for (CanvasUpdate stage = first; stage <= last; ++stage)
                foreach (var item in active) {
                    if (failed.Contains(item)) continue;
                    try { if (!item.IsDestroyed()) item.Rebuild(stage); }
                    catch (Exception ex) { failed.Add(item); report(ex); }
                }
            foreach (var item in active) try { if (!item.IsDestroyed()) { if (isLayout) item.LayoutComplete(); else item.GraphicUpdateComplete(); } }
                catch (Exception ex) { report(ex); }
        }
        try {
            rebuildingLayout = true; Run(layout, CanvasUpdate.Prelayout, CanvasUpdate.PostLayout, true);
            afterLayout(); rebuildingLayout = false;
            rebuildingGraphics = true; Run(graphics, CanvasUpdate.PreRender, CanvasUpdate.LatePreRender, false);
        } finally { rebuildingLayout = rebuildingGraphics = false; }
    }
    internal void Update(Actor anchor, Vector2 viewport, IReadOnlyList<Behaviour> components, Action<Exception> report) {
        foreach (var ui in components.OfType<EventSystems.UIBehaviour>()) ui.EnsureNativeComponents();
        foreach (var scaler in components.OfType<CanvasScaler>()) scaler.UpdateScale(viewport);
        anchor.Access.Query(anchor.Handle, 20, NativeUIComponent.Pack(viewport));
        void Notify() {
            foreach (var ui in components.OfType<EventSystems.UIBehaviour>())
                try { ui.NotifyNativeChanges(); } catch (Exception ex) { report(ex); }
        }
        DrivenRectTransformTracker.ReleaseInactive(anchor, report);
        FontUpdateTracker.Update(anchor);
        Notify();
        layoutChanges.Observe(components, true, report);
        PerformUpdate(report, () => {
            Notify(); layoutChanges.Observe(components, false, report); ClipperRegistry.Cull(anchor);
        });
    }
}

public sealed class LayoutRebuilder : ICanvasElement, IEquatable<LayoutRebuilder>
{
    public RectTransform transform { get; }
    private readonly ulong key;
    private LayoutRebuilder(RectTransform rect) { transform = rect; key = rect.Owner.Handle; }
    public bool Equals(LayoutRebuilder? other) => other != null && key == other.key;
    public override bool Equals(object? other) => other is LayoutRebuilder value && Equals(value);
    public override int GetHashCode() => key.GetHashCode();
    public bool IsDestroyed() { _ = transform.rect; return false; } // Validates the native component epoch.
    public void LayoutComplete() { }
    public void GraphicUpdateComplete() { }
    public static void MarkLayoutForRebuild(RectTransform rect) => MarkLayoutForRebuild(rect, false);
    internal static void MarkLayoutForRebuild(RectTransform rect, bool deferDuringLayout) {
        if (!deferDuringLayout && CanvasUpdateRegistry.IsRebuildingLayout(rect.Owner)) return;
        RectTransform root = rect;
        for (RectTransform? parent = root.parent; parent != null; parent = parent.parent) {
            if (!parent.Owner.GetComponents<ILayoutGroup>().Any(LayoutUtility.Active)) break;
            root = parent;
        }
        if (root.Owner.Handle == rect.Owner.Handle && !root.Owner.GetComponents<ILayoutController>().Any(LayoutUtility.Active)) return;
        CanvasUpdateRegistry.RegisterCanvasElementForLayoutRebuild(new LayoutRebuilder(root));
    }
    public static void ForceRebuildLayoutImmediate(RectTransform layoutRoot) => new LayoutRebuilder(layoutRoot).Rebuild(CanvasUpdate.Layout);
    private static IEnumerable<RectTransform> Children(RectTransform rect) {
        for (int i = 0; i < rect.Owner.ChildCount; ++i) {
            var child = rect.Owner.GetChild(i).GetComponent<RectTransform>();
            if (child != null && child.Owner.ActiveInHierarchy) yield return child;
        }
    }
    private static void Calculate(RectTransform rect, int axis) {
        if (!rect.Owner.ActiveInHierarchy) return;
        var elements = rect.Owner.GetComponents<ILayoutElement>().Where(LayoutUtility.Active).ToArray();
        if (elements.Length == 0 && !rect.Owner.GetComponents<ILayoutGroup>().Any(LayoutUtility.Active)) return;
        foreach (var child in Children(rect)) Calculate(child, axis);
        foreach (var e in elements) { if (axis == 0) e.CalculateLayoutInputHorizontal(); else e.CalculateLayoutInputVertical(); }
    }
    private static void Control(RectTransform rect, int axis) {
        if (!rect.Owner.ActiveInHierarchy) return;
        var controllers = rect.Owner.GetComponents<ILayoutController>().Where(LayoutUtility.Active).ToArray();
        if (controllers.Length == 0) return;
        foreach (bool self in new[] { true, false }) foreach (var controller in controllers)
            if ((controller is ILayoutSelfController) == self) { if (axis == 0) controller.SetLayoutHorizontal(); else controller.SetLayoutVertical(); }
        foreach (var child in Children(rect)) Control(child, axis);
    }
    public void Rebuild(CanvasUpdate executing) {
        if (executing != CanvasUpdate.Layout) return;
        var registry = transform.Owner.Access.UIRegistry;
        if (!registry.EnterLayout(key)) throw new InvalidOperationException("Layout cycle: recursive ForceRebuildLayoutImmediate on the same root");
        try { Calculate(transform, 0); Control(transform, 0); Calculate(transform, 1); Control(transform, 1); }
        finally { registry.ExitLayout(key); }
    }
}
