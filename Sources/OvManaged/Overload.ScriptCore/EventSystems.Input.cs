using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Overload.Maths;

namespace Overload.EventSystems;

public enum UIInputKind { PointerMove, PointerDown, PointerUp, Scroll, KeyDown, KeyUp, KeyRepeat, Character, Touch, Focus, Composition }
public enum TouchPhase { Began, Moved, Stationary, Ended, Canceled }
public readonly record struct UIInputEvent(UIInputKind kind, int id = 0, Vector2 position = default,
    TouchPhase phase = TouchPhase.Began, string composition = "", int compositionCursor = 0);

/// <summary>Immutable Game viewport framebuffer pixels, bottom-left origin; seconds, Unicode scalars, GLFW key codes.</summary>
public sealed class UIInputSnapshot
{
    public ulong sequence { get; }
    public double time { get; }
    public float deltaTime { get; }
    public bool focused { get; }
    public Vector2 mousePosition { get; }
    public Vector2 viewportSize { get; }
    public ReadOnlyCollection<UIInputEvent> events { get; }
    public ReadOnlyCollection<int> heldKeys { get; }
    public ReadOnlyCollection<int> heldButtons { get; }
    public string composition { get; }
    public int compositionCursor { get; }
    public UIInputSnapshot(ulong sequence, double time, float deltaTime, bool focused, Vector2 mousePosition,
        Vector2 viewportSize, IEnumerable<UIInputEvent>? events = null, IEnumerable<int>? heldKeys = null,
        IEnumerable<int>? heldButtons = null, string composition = "", int compositionCursor = 0)
    {
        if (!double.IsFinite(time) || !float.IsFinite(deltaTime) || deltaTime < 0 || !Finite(mousePosition) ||
            !Finite(viewportSize) || viewportSize.x < 0 || viewportSize.y < 0) throw new ArgumentException("Invalid input snapshot");
        var copy = (events ?? Array.Empty<UIInputEvent>()).Take(16385).ToArray();
        if (copy.Length > 16384) throw new ArgumentException("Input event limit");
        foreach (var e in copy) {
            if (!Enum.IsDefined(e.kind) || !Finite(e.position) || !Enum.IsDefined(e.phase) ||
                e.kind == UIInputKind.Touch && e.id < 0 ||
                e.kind == UIInputKind.Character && !System.Text.Rune.IsValid(e.id)) throw new ArgumentException("Invalid input event");
            ValidateComposition(e.composition, e.compositionCursor);
        }
        ValidateComposition(composition, compositionCursor);
        this.sequence = sequence; this.time = time; this.deltaTime = deltaTime; this.focused = focused;
        this.mousePosition = mousePosition; this.viewportSize = viewportSize;
        this.events = Array.AsReadOnly(copy);
        this.heldKeys = Array.AsReadOnly((heldKeys ?? Array.Empty<int>()).Take(1025).Distinct().ToArray());
        this.heldButtons = Array.AsReadOnly((heldButtons ?? Array.Empty<int>()).Take(33).Distinct().ToArray());
        if (this.heldKeys.Count > 1024 || this.heldButtons.Count > 32) throw new ArgumentException("Held input limit");
        this.composition = composition; this.compositionCursor = compositionCursor;
    }
    private static void ValidateComposition(string text, int cursor) {
        if (text == null || text.Length > 8192 || cursor < 0) throw new ArgumentException("Invalid composition");
        int count = 0;
        for (int i = 0; i < text.Length; ++i, ++count) {
            if (char.IsHighSurrogate(text[i])) { if (++i == text.Length || !char.IsLowSurrogate(text[i])) throw new ArgumentException("Invalid UTF-16"); }
            else if (char.IsLowSurrogate(text[i])) throw new ArgumentException("Invalid UTF-16");
        }
        if (cursor > count) throw new ArgumentException("Composition cursor is a scalar index");
    }
    internal static bool Finite(Vector2 p) => float.IsFinite(p.x) && float.IsFinite(p.y);
}

/// <summary>Session-owned native adapter. Capture returns one shared immutable snapshot per tick.</summary>
public interface IUIInputSource
{
    UIInputSnapshot Capture();
    string clipboard { get; set; }
}

public class BaseInput : UIBehaviour
{
    private readonly Queue<UIInputSnapshot> injected = new();
    private readonly int thread = Environment.CurrentManagedThreadId;
    public IUIInputSource? source { get; set; }
    public UIInputSnapshot snapshot { get; private set; } = new(0, 0, 0, true, default, default);
    public virtual string compositionString => snapshot.composition;
    public virtual int compositionCursor => snapshot.compositionCursor;
    public virtual Vector2 mousePosition => snapshot.mousePosition;
    public virtual Vector2 mouseScrollDelta => snapshot.events.Where(e => e.kind == UIInputKind.Scroll).Aggregate(Vector2.zero, (sum, e) => sum + e.position);
    public virtual bool mousePresent => true;
    public virtual bool touchSupported => true;
    public virtual int touchCount => snapshot.events.Count(e => e.kind == UIInputKind.Touch);
    public virtual UIInputEvent GetTouch(int index) => snapshot.events.Where(e => e.kind == UIInputKind.Touch).ElementAt(index);
    public virtual string inputString => string.Concat(snapshot.events.Where(e => e.kind == UIInputKind.Character).Select(e => new System.Text.Rune(e.id).ToString()));
    public virtual float GetAxisRaw(string axis) => axis switch {
        "Horizontal" => (GetKey(262) ? 1 : 0)-(GetKey(263) ? 1 : 0),
        "Vertical" => (GetKey(265) ? 1 : 0)-(GetKey(264) ? 1 : 0),
        _ => 0
    };
    public virtual bool GetButtonDown(string button) => button switch {
        "Submit" => GetKeyDown(257) || GetKeyDown(335), "Cancel" => GetKeyDown(256), _ => false
    };
    public virtual bool GetMouseButton(int button) => snapshot.heldButtons.Contains(button);
    public virtual bool GetMouseButtonDown(int button) => Has(UIInputKind.PointerDown, button);
    public virtual bool GetMouseButtonUp(int button) => Has(UIInputKind.PointerUp, button);
    public virtual bool GetKey(int key) => snapshot.heldKeys.Contains(key);
    public virtual bool GetKeyDown(int key) => Has(UIInputKind.KeyDown, key);
    public virtual bool GetKeyUp(int key) => Has(UIInputKind.KeyUp, key);
    public virtual bool GetKeyRepeat(int key) => Has(UIInputKind.KeyRepeat, key);
    public virtual string clipboard {
        get { CheckThread(); return (source ?? throw new InvalidOperationException("No platform input source bound")).clipboard; }
        set { CheckThread(); if (value == null || value.Length > 1048576 || value.Contains('\0')) throw new ArgumentException("Invalid clipboard");
            (source ?? throw new InvalidOperationException("No platform input source bound")).clipboard = value; }
    }
    private bool Has(UIInputKind kind, int id) => snapshot.events.Any(e => e.kind == kind && e.id == id);
    private void CheckThread() { if (thread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Input requires Main"); }
    public void Inject(UIInputSnapshot frame) {
        CheckThread(); ArgumentNullException.ThrowIfNull(frame);
        if (injected.Count >= 256) throw new InvalidOperationException("Input injection queue full");
        injected.Enqueue(frame);
    }
    public virtual UIInputSnapshot Capture() {
        CheckThread();
        UIInputSnapshot next = injected.Count != 0 ? injected.Dequeue() : source?.Capture() ?? snapshot;
        if (next.sequence < snapshot.sequence || next.time < snapshot.time) throw new InvalidOperationException("Input sequence/time moved backwards");
        snapshot = next; return snapshot;
    }
    public override void OnEnable() { source ??= new NativeUIInputSource(Owner); }
    public override void OnDisable() { injected.Clear(); }
    public override void OnDestroy() { injected.Clear(); source = null; }
    public override void OnReloadRelease() => OnDestroy();
    public override void OnReloadRestore() { if (IsActive()) OnEnable(); }
    protected override void SetDirty() { }
    internal override void EnsureNativeComponents() { }
}
