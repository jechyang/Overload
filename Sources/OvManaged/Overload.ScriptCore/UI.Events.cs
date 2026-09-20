using System;
using System.Collections.Generic;

namespace Overload.UI;

internal sealed class UICancellation(Action cancel) : IDisposable
{
    private Action? callback = cancel;
    public void Dispose() {
        Action? pending = callback;
        pending?.Invoke();
        callback = null;
    }
}

// No static listener lists or project Type caches live in resident ScriptCore.
internal sealed class UIListeners<T> : IDisposable where T : Delegate
{
    private sealed class Entry(T callback) {
        internal T? Callback = callback;
        internal IDisposable? Subscription;
    }
    private readonly List<Entry> entries = new();
    private readonly int thread = Environment.CurrentManagedThreadId;
    private Behaviour? owner;
    private readonly bool owned;
    private bool disposed;
    internal UIListeners(Behaviour? owner) {
        this.owner = owner; owned = owner != null;
        owner?.OwnSubscription(this);
    }
    private void CheckThread() {
        if (Environment.CurrentManagedThreadId != thread) throw new InvalidOperationException("UI events require their creating thread");
    }
    internal IDisposable Subscribe(T callback) {
        CheckThread(); ObjectDisposedException.ThrowIf(disposed, this); ArgumentNullException.ThrowIfNull(callback);
        foreach (Entry existing in entries) if (existing.Callback?.Equals(callback) == true) return existing.Subscription!;
        var entry = new Entry(callback);
        Behaviour? receiver = callback.Target as Behaviour;
        UICancellation? subscription = null;
        subscription = new UICancellation(() => {
            CheckThread(); entry.Callback = null; entry.Subscription = null; entries.Remove(entry);
            receiver?.DisownSubscription(subscription!);
        });
        entry.Subscription = subscription; entries.Add(entry);
        try {
            // Direct Behaviour method listeners also unregister when the receiver
            // dies, even if the source belongs to a longer-lived scene object.
            receiver?.OwnSubscription(subscription);
        } catch { subscription.Dispose(); throw; }
        return subscription;
    }
    internal void Remove(T callback) {
        CheckThread(); ArgumentNullException.ThrowIfNull(callback);
        foreach (Entry entry in entries.ToArray()) if (entry.Callback?.Equals(callback) == true) entry.Subscription!.Dispose();
    }
    internal void Clear() {
        CheckThread();
        foreach (Entry entry in entries.ToArray()) entry.Subscription?.Dispose();
        entries.Clear();
    }
    internal void Invoke<TValue>(TValue value, Action<T, TValue> call) {
        CheckThread();
        if (disposed || owned && owner?.owner == null) return;
        // A listener added during dispatch participates in the next dispatch.
        // Removed listeners and a disposed source stop participating immediately.
        foreach (Entry entry in entries.ToArray()) {
            if (disposed) break;
            T? callback = entry.Callback;
            if (callback == null) continue;
            if (callback.Target is Behaviour receiver && receiver.owner == null) { entry.Subscription?.Dispose(); continue; }
            call(callback, value);
        }
    }
    public void Dispose() {
        CheckThread();
        if (disposed) return;
        disposed = true; Clear(); owner?.DisownSubscription(this); owner = null;
    }
}

/// <summary>Runtime UI event. Bind it to a Behaviour to release listeners at session teardown.</summary>
public class UIEvent : IDisposable
{
    private readonly UIListeners<Action> listeners;
    public UIEvent() : this(null) { }
    public UIEvent(Behaviour? owner) => listeners = new(owner);
    public void AddListener(Action listener) => listeners.Subscribe(listener);
    public IDisposable Subscribe(Action listener) => listeners.Subscribe(listener);
    public void RemoveListener(Action listener) => listeners.Remove(listener);
    public void RemoveAllListeners() => listeners.Clear();
    public void Invoke() => listeners.Invoke(false, static (listener, _) => listener());
    public void Dispose() => listeners.Dispose();
}

/// <summary>Typed runtime UI event; listeners are never serialized as object addresses.</summary>
public class UIEvent<T> : IDisposable
{
    private readonly UIListeners<Action<T>> listeners;
    public UIEvent() : this(null) { }
    public UIEvent(Behaviour? owner) => listeners = new(owner);
    public void AddListener(Action<T> listener) => listeners.Subscribe(listener);
    public IDisposable Subscribe(Action<T> listener) => listeners.Subscribe(listener);
    public void RemoveListener(Action<T> listener) => listeners.Remove(listener);
    public void RemoveAllListeners() => listeners.Clear();
    public void Invoke(T value) => listeners.Invoke(value, static (listener, argument) => listener(argument));
    public void Dispose() => listeners.Dispose();
}
