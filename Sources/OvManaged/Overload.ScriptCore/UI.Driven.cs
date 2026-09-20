using System;
using System.Collections.Generic;

namespace Overload;

/// <summary>The 2D properties supported by Overload RectTransform.</summary>
[Flags]
public enum DrivenTransformProperties
{
    None = 0,
    AnchoredPositionX = 1, AnchoredPositionY = 2,
    AnchorMinX = 4, AnchorMinY = 8, AnchorMaxX = 16, AnchorMaxY = 32,
    SizeDeltaX = 64, SizeDeltaY = 128, PivotX = 256, PivotY = 512,
    AnchoredPosition = AnchoredPositionX | AnchoredPositionY,
    AnchorMin = AnchorMinX | AnchorMinY, AnchorMax = AnchorMaxX | AnchorMaxY,
    Anchors = AnchorMin | AnchorMax, SizeDelta = SizeDeltaX | SizeDeltaY,
    Pivot = PivotX | PivotY, All = AnchoredPosition | Anchors | SizeDelta | Pivot
}

/// <summary>Main-thread, session-owned property leases. Clear restores authored values.
/// A reference type deliberately avoids accidentally copying ownership through a struct.</summary>
public sealed class DrivenRectTransformTracker
{
    private sealed class Entry(RectTransform rect)
    {
        internal readonly RectTransform Rect = rect;
        internal ulong Token;
        internal DrivenTransformProperties Properties, Wanted;
    }
    private readonly Dictionary<ulong, Entry> entries = new();
    private Behaviour? driver;
    private IDisposable? registration, cleanup;
    private int thread;
    private void CheckThread() {
        if (driver != null && thread != Environment.CurrentManagedThreadId)
            throw new InvalidOperationException("Driven properties require Main");
    }
    public void Add(Behaviour owner, RectTransform rect, DrivenTransformProperties properties) {
        ArgumentNullException.ThrowIfNull(owner); ArgumentNullException.ThrowIfNull(rect);
        CheckThread();
        if ((properties & ~DrivenTransformProperties.All) != 0) throw new ArgumentOutOfRangeException(nameof(properties));
        if (properties == DrivenTransformProperties.None) return;
        if (!ReferenceEquals(owner.Owner.Access.UIRegistry, rect.Owner.Access.UIRegistry))
            throw new InvalidOperationException("Driven properties cannot cross sessions");
        if (driver != null && !ReferenceEquals(owner, driver)) throw new InvalidOperationException("A tracker has one driver");
        var registry = UI.UISessionRegistry<DrivenRectTransformTracker>.For(owner.Owner);
        foreach (var other in registry.Snapshot()) {
            if (ReferenceEquals(other, this)) continue;
            if (other.entries.TryGetValue(rect.Handle, out var claim) && (claim.Properties & properties) != 0)
                throw new InvalidOperationException("Layout cycle/conflict: multiple controllers drive the same RectTransform property");
        }
        if (driver == null) {
            thread = Environment.CurrentManagedThreadId; driver = owner;
            registration = registry.Register(this, owner);
            cleanup = owner.OwnSubscription(new UI.UICancellation(Clear));
        }
        if (!entries.TryGetValue(rect.Handle, out var entry)) entries.Add(rect.Handle, entry = new(rect));
        var added = properties & ~entry.Properties;
        if (added != DrivenTransformProperties.None)
            entry.Token = rect.Owner.Access.Query(rect.Handle, 60, (ulong)added,
                entry.Token == 0 ? "" : entry.Token.ToString(System.Globalization.CultureInfo.InvariantCulture));
        entry.Properties |= properties; entry.Wanted |= properties;
    }
    internal static void ReleaseInactive(Actor anchor, Action<Exception> report) {
        foreach (var tracker in UI.UISessionRegistry<DrivenRectTransformTracker>.For(anchor).Snapshot()) {
            try { if (tracker.driver == null || !UI.LayoutUtility.Active(tracker.driver)) tracker.Clear(); }
            catch (Exception ex) { report(ex); }
        }
    }
    // Reconcile claims without restoring/reapplying every property each frame.
    internal void BeginUpdate() {
        CheckThread(); foreach (var entry in entries.Values) entry.Wanted = DrivenTransformProperties.None;
    }
    internal void EndUpdate() {
        CheckThread();
        foreach (var key in new List<ulong>(entries.Keys)) {
            Entry entry = entries[key]; Restore(entry, entry.Properties & ~entry.Wanted);
            entry.Properties = entry.Wanted;
            if (entry.Properties == DrivenTransformProperties.None) entries.Remove(key);
        }
    }
    private static void Restore(Entry entry, DrivenTransformProperties properties) {
        if (properties == DrivenTransformProperties.None) return;
        try {
            entry.Rect.Owner.Access.Query(entry.Rect.Handle, 61, entry.Token,
                ((uint)properties).ToString(System.Globalization.CultureInfo.InvariantCulture));
        } catch (InvalidOperationException) {
            // Suppress only invalid epochs. Real restore failures must reach the host log.
            bool invalid = false;
            try { _ = entry.Rect.rect; } catch (InvalidOperationException) { invalid = true; }
            if (!invalid) throw;
        }
    }

    public void Clear() {
        CheckThread();
        foreach (var entry in entries.Values) Restore(entry, entry.Properties);
        entries.Clear();
        Behaviour? previous = driver; driver = null;
        IDisposable? subscription = cleanup; cleanup = null;
        if (subscription != null) { previous?.DisownSubscription(subscription); subscription.Dispose(); }
        registration?.Dispose(); registration = null;
    }
}
