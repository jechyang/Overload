using System;
using System.Collections.Generic;

namespace Overload.UI;

internal interface IUISessionResource { void Release(); }

/// <summary>A main-thread, weak registry owned by one managed session.</summary>
public sealed class UISessionRegistry<T> : IUISessionResource where T : class
{
    private sealed class Entry(T value) {
        internal readonly WeakReference<T> Value = new(value);
        internal IDisposable? Subscription;
    }
    private readonly CanvasUpdateRegistry session;
    private readonly List<Entry> entries = new();
    private readonly int thread = Environment.CurrentManagedThreadId;
    private bool released;
    internal UISessionRegistry(CanvasUpdateRegistry session) => this.session = session;
    public static UISessionRegistry<T> For(Actor owner) {
        ArgumentNullException.ThrowIfNull(owner);
        return owner.Access.UIRegistry.GetRegistry<T>();
    }
    private void Check() {
        if (Environment.CurrentManagedThreadId != thread) throw new InvalidOperationException("UI registries require Main");
        ObjectDisposedException.ThrowIf(released, this);
    }
    public IDisposable Register(T value, Behaviour lifetimeOwner) {
        Check(); ArgumentNullException.ThrowIfNull(value); ArgumentNullException.ThrowIfNull(lifetimeOwner);
        if (!ReferenceEquals(lifetimeOwner.Owner.Access.UIRegistry, session) ||
            value is Behaviour behaviour && !ReferenceEquals(behaviour.Owner.Access.UIRegistry, session))
            throw new InvalidOperationException("A UI registry cannot retain objects from another session");
        foreach (Entry existing in entries.ToArray()) {
            if (!existing.Value.TryGetTarget(out T? item)) existing.Subscription!.Dispose();
            else if (ReferenceEquals(item, value)) return existing.Subscription!;
        }
        var entry = new Entry(value);
        var owner = new WeakReference<Behaviour>(lifetimeOwner);
        UICancellation? subscription = null;
        subscription = new UICancellation(() => {
            Check(); entries.Remove(entry); entry.Subscription = null;
            if (owner.TryGetTarget(out Behaviour? target)) target.DisownSubscription(subscription!);
        });
        entry.Subscription = subscription; entries.Add(entry);
        try { lifetimeOwner.OwnSubscription(subscription); }
        catch { subscription.Dispose(); throw; }
        return subscription;
    }
    public T[] Snapshot() {
        Check(); var live = new List<T>(entries.Count);
        foreach (Entry entry in entries.ToArray()) {
            if (entry.Value.TryGetTarget(out T? value)) live.Add(value);
            else entry.Subscription!.Dispose();
        }
        return live.ToArray();
    }
    public int Count => Snapshot().Length;
    public void Clear() {
        Check();
        foreach (Entry entry in entries.ToArray()) entry.Subscription!.Dispose();
    }
    void IUISessionResource.Release() { Clear(); released = true; }
}
