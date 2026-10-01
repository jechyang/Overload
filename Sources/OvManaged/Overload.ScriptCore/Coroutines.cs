using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Overload;

/// <summary>Waits the given unscaled-or-scaled seconds inside a coroutine.</summary>
public sealed class WaitForSeconds
{
    public readonly float seconds;
    public WaitForSeconds(float seconds)
    {
        if (!float.IsFinite(seconds) || seconds < 0) throw new ArgumentOutOfRangeException(nameof(seconds));
        this.seconds = seconds;
    }
}

/// <summary>Handle returned by <see cref="Behaviour.StartCoroutine"/>. Stop() is idempotent.</summary>
public sealed class Coroutine
{
    internal readonly IEnumerator Routine;
    internal readonly List<IEnumerator> Stack = new();
    internal float Wait;
    internal bool Waiting;
    internal bool Done;
    internal bool Stopped;
    internal readonly WeakReference<Behaviour> Owner;

    internal Coroutine(IEnumerator routine, Behaviour owner)
    {
        Routine = routine ?? throw new ArgumentNullException(nameof(routine));
        Owner = new WeakReference<Behaviour>(owner);
        Stack.Add(routine);
    }

    public bool isDone => Done || Stopped;
    public void Stop() => Stopped = true;
}

/// <summary>
/// Session-safe coroutine pump (no MonoBehaviour.StartCoroutine).
/// Routines advance on the UI/game tick; yield return null is one frame,
/// <see cref="WaitForSeconds"/> waits scaled time, nested IEnumerators run inline.
/// </summary>
public static class CoroutineScheduler
{
    private static readonly List<Coroutine> s_Active = new();
    private static readonly List<Coroutine> s_Spawned = new();
    private static readonly List<Action> s_Continuations = new();
    private static readonly List<Action> s_ContinuationsNext = new();
    private static readonly List<TimedWait> s_Timed = new();

    public static Coroutine Start(IEnumerator routine, Behaviour owner)
    {
        ArgumentNullException.ThrowIfNull(routine);
        ArgumentNullException.ThrowIfNull(owner);
        var coroutine = new Coroutine(routine, owner);
        s_Spawned.Add(coroutine);
        return coroutine;
    }

    /// <summary>Queue an async continuation for the next pump (for <see cref="AsyncYield"/>).</summary>
    public static void QueueContinuation(Action continuation)
    {
        ArgumentNullException.ThrowIfNull(continuation);
        s_ContinuationsNext.Add(continuation);
    }

    /// <summary>Queue a continuation after scaled seconds on the next pumps.</summary>
    public static void QueueContinuationAfter(float seconds, Action continuation)
    {
        ArgumentNullException.ThrowIfNull(continuation);
        if (!float.IsFinite(seconds) || seconds < 0) throw new ArgumentOutOfRangeException(nameof(seconds));
        if (seconds <= 0) { QueueContinuation(continuation); return; }
        s_Timed.Add(new TimedWait(seconds, continuation));
    }

    private readonly struct TimedWait
    {
        public readonly float remaining;
        public readonly Action continuation;
        public TimedWait(float remaining, Action continuation) { this.remaining = remaining; this.continuation = continuation; }
    }

    public static void Tick(float deltaTime, float unscaledDeltaTime)
    {
        if (!float.IsFinite(deltaTime) || deltaTime < 0 || !float.IsFinite(unscaledDeltaTime) || unscaledDeltaTime < 0)
            throw new ArgumentOutOfRangeException(nameof(deltaTime));

        for (int i = 0; i < s_Spawned.Count; ++i) s_Active.Add(s_Spawned[i]);
        s_Spawned.Clear();

        // Async continuations scheduled last frame run at the start of this tick.
        if (s_Continuations.Count > 0) {
            var batch = s_Continuations.ToArray();
            s_Continuations.Clear();
            foreach (var continuation in batch) {
                try { continuation(); } catch { /* logged by host if needed */ }
            }
        }
        for (int i = 0; i < s_ContinuationsNext.Count; ++i) s_Continuations.Add(s_ContinuationsNext[i]);
        s_ContinuationsNext.Clear();

        for (int i = s_Timed.Count - 1; i >= 0; --i) {
            var wait = s_Timed[i];
            float left = wait.remaining - deltaTime;
            if (left > 0) {
                s_Timed[i] = new TimedWait(left, wait.continuation);
                continue;
            }
            s_Timed.RemoveAt(i);
            try { wait.continuation(); } catch { }
        }

        for (int i = s_Active.Count - 1; i >= 0; --i) {
            var coroutine = s_Active[i];
            if (coroutine.Stopped) { coroutine.Done = true; s_Active.RemoveAt(i); continue; }
            if (!coroutine.Owner.TryGetTarget(out var owner) || owner == null || !owner.Owner.IsAlive) {
                coroutine.Stopped = true;
                coroutine.Done = true;
                s_Active.RemoveAt(i);
                continue;
            }
            if (coroutine.Waiting) {
                coroutine.Wait -= deltaTime; // WaitForSeconds is scaled time
                if (coroutine.Wait > 0) continue;
                coroutine.Waiting = false;
                coroutine.Wait = 0;
            }
            bool alive = Advance(coroutine);
            if (!alive || coroutine.Stopped) {
                coroutine.Done = true;
                s_Active.RemoveAt(i);
            }
        }
    }

    public static void Clear()
    {
        s_Active.Clear();
        s_Spawned.Clear();
        s_Continuations.Clear();
        s_ContinuationsNext.Clear();
        s_Timed.Clear();
    }

    private static bool Advance(Coroutine coroutine)
    {
        while (coroutine.Stack.Count > 0) {
            var current = coroutine.Stack[^1];
            bool moved;
            object? yielded = null;
            try {
                moved = current.MoveNext();
                if (moved) yielded = current.Current;
            } catch {
                coroutine.Stack.Clear();
                return false;
            }
            if (!moved) {
                coroutine.Stack.RemoveAt(coroutine.Stack.Count - 1);
                continue;
            }
            if (yielded is WaitForSeconds wait) {
                coroutine.Waiting = true;
                coroutine.Wait = wait.seconds;
                return true;
            }
            if (yielded is IEnumerator nested) {
                coroutine.Stack.Add(nested);
                continue;
            }
            // yield return null (or anything else) = wait one tick.
            return true;
        }
        return false;
    }
}

/// <summary>
/// Thin C# async wrapper over <see cref="CoroutineScheduler"/>: each await
/// completes on the next pump, so async methods share the coroutine tick.
/// </summary>
public static class AsyncYield
{
    public static FrameAwaitable NextFrame() => default;
    public static SecondsAwaitable Seconds(float seconds) => new(seconds);

    public readonly struct FrameAwaitable
    {
        public Awaiter GetAwaiter() => default;
        public readonly struct Awaiter : ICriticalNotifyCompletion
        {
            public bool IsCompleted => false;
            public void OnCompleted(Action continuation) => CoroutineScheduler.QueueContinuation(continuation);
            public void UnsafeOnCompleted(Action continuation) => CoroutineScheduler.QueueContinuation(continuation);
            public void GetResult() { }
        }
    }

    public readonly struct SecondsAwaitable
    {
        private readonly float seconds;
        public SecondsAwaitable(float seconds)
        {
            if (!float.IsFinite(seconds) || seconds < 0) throw new ArgumentOutOfRangeException(nameof(seconds));
            this.seconds = seconds;
        }
        public Awaiter GetAwaiter() => new(seconds);
        public readonly struct Awaiter : ICriticalNotifyCompletion
        {
            private readonly float seconds;
            public Awaiter(float seconds) { this.seconds = seconds; }
            public bool IsCompleted => seconds <= 0;
            public void OnCompleted(Action continuation) => CoroutineScheduler.QueueContinuationAfter(seconds, continuation);
            public void UnsafeOnCompleted(Action continuation) => OnCompleted(continuation);
            public void GetResult() { }
        }
    }
}
