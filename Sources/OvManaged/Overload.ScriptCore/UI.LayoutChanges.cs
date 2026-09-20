using System;
using System.Collections.Generic;
using Overload.Maths;

namespace Overload.UI;

// Transitional observer until native revisions are exposed. Reads live geometry,
// but only changed roots run measurement/control. State belongs to this session.
internal sealed class LayoutChangeObserver
{
    private readonly record struct RectState(ulong Actor, ulong Rect, bool Active, ulong Parent,
        Vector2 ParentSize, Vector2 Min, Vector2 Max, Vector2 Pivot, Vector2 Position, Vector2 Delta, Vector2 Size);
    private readonly record struct InputState(Behaviour Component, bool Active, bool Ignore, int Priority, float MinX, float MinY, float PreferredX, float PreferredY, float FlexibleX, float FlexibleY);
    private sealed record Snapshot(RectState Own, RectState[] Children, InputState[] Components);
    private static InputState Input(Behaviour value) {
        var e = value as ILayoutElement;
        return new(value, LayoutUtility.Active(value), (value as ILayoutIgnorer)?.ignoreLayout ?? false,
            e?.layoutPriority ?? 0, e?.minWidth ?? 0, e?.minHeight ?? 0, e?.preferredWidth ?? 0, e?.preferredHeight ?? 0, e?.flexibleWidth ?? 0, e?.flexibleHeight ?? 0);
    }
    private readonly Dictionary<Behaviour, Snapshot> previous = new();
    internal void Clear() => previous.Clear();
    private static RectState Capture(Actor actor) {
        RectTransform? rect = actor.GetComponent<RectTransform>();
        Actor? parent = actor.Parent;
        return new(actor.Handle, rect?.Handle ?? 0, actor.ActiveInHierarchy, parent?.Handle ?? 0,
            parent?.GetComponent<RectTransform>()?.rect.size ?? Vector2.zero,
            rect?.anchorMin ?? Vector2.zero, rect?.anchorMax ?? Vector2.zero, rect?.pivot ?? Vector2.zero,
            rect?.anchoredPosition ?? Vector2.zero, rect?.sizeDelta ?? Vector2.zero, rect?.rect.size ?? Vector2.zero);
    }
    private static bool Equal(Snapshot a, Snapshot b) {
        if (a.Own != b.Own || a.Children.Length != b.Children.Length || a.Components.Length != b.Components.Length) return false;
        for (int i = 0; i < a.Children.Length; ++i) if (a.Children[i] != b.Children[i]) return false;
        for (int i = 0; i < a.Components.Length; ++i) if (a.Components[i] != b.Components[i]) return false;
        return true;
    }
    internal void Observe(IReadOnlyList<Behaviour> components, bool schedule, Action<Exception> report) {
        var live = new HashSet<Behaviour>();
        foreach (var component in components) {
            if (component is not (ILayoutElement or ILayoutController) || component.owner == null) continue;
            live.Add(component);
            try {
                Actor owner = component.Owner;
                var children = new List<RectState>();
                var peers = new List<Behaviour>();
                peers.AddRange(owner.GetComponents<Behaviour>());
                if (component is ILayoutGroup) {
                    for (int i = 0; i < owner.ChildCount; ++i) {
                        Actor child = owner.GetChild(i); children.Add(Capture(child));
                        peers.AddRange(child.GetComponents<Behaviour>());
                    }
                }
                var inputs = new List<InputState>();
                foreach (var peer in peers) inputs.Add(Input(peer));
                var now = new Snapshot(Capture(owner), children.ToArray(), inputs.ToArray());
                if (schedule && (!previous.TryGetValue(component, out var before) || !Equal(before, now))) {
                    if (owner.GetComponent<RectTransform>() is RectTransform rect) LayoutRebuilder.MarkLayoutForRebuild(rect);
                    if (before != null && before.Own.Parent != now.Own.Parent && before.Own.Parent != 0) {
                        try {
                            var oldParent = new Actor(owner.Access, before.Own.Parent).GetComponent<RectTransform>();
                            if (oldParent != null) LayoutRebuilder.MarkLayoutForRebuild(oldParent);
                        } catch (InvalidOperationException) { }
                    }
                }
                previous[component] = now;
            } catch (Exception ex) { report(ex); }
        }
        foreach (var key in new List<Behaviour>(previous.Keys)) if (!live.Contains(key)) previous.Remove(key);
    }
}
