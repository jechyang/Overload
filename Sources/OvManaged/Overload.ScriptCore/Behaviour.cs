using System;
using System.Collections;
using System.Runtime.CompilerServices;
using Vector3 = Overload.Maths.Vector3;

[assembly: InternalsVisibleTo("Overload.ManagedHost")]

namespace Overload;

public abstract class Behaviour
{
    internal Actor? owner;
    private readonly System.Collections.Generic.List<IDisposable> subscriptions = new();
    private readonly System.Collections.Generic.List<Coroutine> coroutines = new();
    private bool subscriptionsClosed;
    protected T TrackSubscription<T>(T subscription) where T : IDisposable {
        if (subscriptionsClosed) { subscription.Dispose(); throw new InvalidOperationException("Behaviour subscription scope closed"); }
        subscriptions.Add(subscription); return subscription;
    }
    internal T OwnSubscription<T>(T subscription) where T : IDisposable => TrackSubscription(subscription);
    internal void DisownSubscription(IDisposable subscription) => subscriptions.Remove(subscription);
    internal void ReleaseSubscriptions(Action<string> log) {
        subscriptionsClosed = true;
        StopAllCoroutines();
        IDisposable[] pending = subscriptions.ToArray(); subscriptions.Clear();
        foreach (IDisposable subscription in pending) try { subscription.Dispose(); } catch (Exception ex) { log("Subscription cleanup: " + ex); }
    }
    public Actor Owner => owner ?? throw new InvalidOperationException("Owner is unavailable during construction");
    public Transform Transform => Owner.Transform;
    /// <summary>Start a session-safe IEnumerator coroutine advanced by the UI/game tick.</summary>
    public Coroutine StartCoroutine(IEnumerator routine) {
        ArgumentNullException.ThrowIfNull(routine);
        var coroutine = CoroutineScheduler.Start(routine, this);
        coroutines.Add(coroutine);
        return coroutine;
    }
    public void StopCoroutine(Coroutine routine) {
        if (routine == null) return;
        routine.Stop();
        coroutines.Remove(routine);
    }
    public void StopAllCoroutines() {
        foreach (var routine in coroutines.ToArray()) routine.Stop();
        coroutines.Clear();
    }
    public virtual void OnAwake() { }
    public virtual void OnEnable() { }
    public virtual void OnStart() { }
    public virtual void OnDisable() { }
    public virtual void OnUpdate(float deltaTime) { }
    public virtual void OnFixedUpdate(float deltaTime) { }
    public virtual void OnLateUpdate(float deltaTime) { }
    public virtual void OnCollisionEnter(Collision other) { }
    public virtual void OnCollisionStay(Collision other) { }
    public virtual void OnCollisionExit(Collision other) { }
    public virtual void OnTriggerEnter(Collision other) { }
    public virtual void OnTriggerStay(Collision other) { }
    public virtual void OnTriggerExit(Collision other) { }
    public virtual void OnDestroy() { }
    public virtual void OnReloadRelease() { }
    public virtual void OnReloadRestore() { }
}

internal interface INativeAccess
{
    ulong Query(ulong owner, uint operation, ulong argument = 0, string text = "");
    bool IsActorAlive(ulong owner) => Query(owner, 21) != 0;
    void SetUIMesh(ulong renderer, UI.UIVertex[] vertices, int[] indices);
    ulong ExchangeFont(ulong owner, uint operation, byte[] packet);
    Vector3 Read(ulong component, bool box);
    void Write(ulong component, bool box, Vector3 value);
    void Log(string text);
    string LogicalName { get; }
    System.Collections.Generic.IReadOnlyList<Behaviour> GetBehaviours(ulong owner);
    UI.CanvasUpdateRegistry UIRegistry { get; }
}

public sealed partial class Actor
{
    internal readonly INativeAccess Access;
    internal readonly ulong Handle;
    internal Actor(INativeAccess access, ulong handle) { Access = access; Handle = handle; }
    public ulong Guid => Access.Query(Handle, 1);
    /// <summary>False for a destroyed or pending-destroy Actor. Main-thread/session checks still apply.</summary>
    public bool IsAlive => Access.IsActorAlive(Handle);
    public Transform Transform => GetComponent<Transform>()!;
    public bool Active { get => Access.Query(Handle, 4) != 0; set => Access.Query(Handle, 5, value ? 1ul : 0ul); }
    public bool ActiveInHierarchy => Access.Query(Handle, 18) != 0;
    internal bool CanModify => Access.Query(Handle, 21) != 0;
    public Actor? Parent {
        get { ulong id = Access.Query(Handle, 14); return id == 0 ? null : new Actor(Access, id); }
        set { if (value != null) _ = value.Guid; Access.Query(Handle, 17, value?.Handle ?? 0); }
    }
    public int ChildCount => checked((int)Access.Query(Handle, 15));
    public Actor GetChild(int index) => new(Access, Access.Query(Handle, 16, checked((ulong)index)));
    public T[] GetComponents<T>() where T : class {
        var result = new System.Collections.Generic.List<T>();
        if (typeof(Behaviour).IsAssignableFrom(typeof(T)) || typeof(T).IsInterface) {
            foreach (Behaviour value in Access.GetBehaviours(Handle)) if (value is T typed) result.Add(typed);
        } else { T? value = GetComponent<T>(); if (value != null) result.Add(value); }
        return result.ToArray();
    }
    public T AddComponent<T>() where T : NativeUIComponent => (T)NativeUIComponent.Resolve(this, typeof(T), true)!;
    public T? GetComponent<T>() where T : class
    {
        if (typeof(NativeUIComponent).IsAssignableFrom(typeof(T))) return NativeUIComponent.Resolve(this, typeof(T), false) as T;
        if (typeof(Behaviour).IsAssignableFrom(typeof(T)) || typeof(T).IsInterface) {
            foreach (Behaviour value in Access.GetBehaviours(Handle)) if (value is T typed) return typed;
            return null;
        }
        uint kind = typeof(T) == typeof(Transform) ? 2u : typeof(T) == typeof(Box) ? 3u :
            throw new NotSupportedException("Unsupported native component: " + typeof(T).FullName);
        ulong handle = Access.Query(Handle, kind);
        if (handle == 0) return null;
        return (T)(object)(kind == 2 ? (object)new Transform(this, handle) : new Box(this, handle));
    }
    public void Destroy() => Access.Query(Handle, 6);
    public void AddBehaviour<T>() where T : Behaviour => Access.Query(Handle, 7, 0, typeof(T).FullName!);
    public void RemoveBehaviour<T>() where T : Behaviour => Access.Query(Handle, 8, 0, typeof(T).FullName!);
    public Actor? Find(string name) { ulong id = Access.Query(Handle, 9, 0, name); return id == 0 ? null : new Actor(Access, id); }
    public Actor? Find(ulong guid) { ulong id = Access.Query(Handle, 12, guid); return id == 0 ? null : new Actor(Access, id); }
    public Actor Create(string name) => new(Access, Access.Query(Handle, 10, 0, name));
    public void LoadScene(string path) => Access.Query(Handle, 11, 0, path);
    public void Log(string message) { _ = Guid; Access.Log(message); }
}
public sealed class Transform
{
    public Actor Owner { get; }
    private readonly INativeAccess access;
    private readonly ulong handle;
    internal Transform(Actor owner, ulong handle) { Owner = owner; access = owner.Access; this.handle = handle; }
    public Vector3 Position { get => access.Read(handle, false); set => access.Write(handle, false, value); }
}
public sealed class Box
{
    public Actor Owner { get; }
    private readonly INativeAccess access;
    private readonly ulong handle;
    internal Box(Actor owner, ulong handle) { Owner = owner; access = owner.Access; this.handle = handle; }
    public Vector3 Size { get => access.Read(handle, true); set => access.Write(handle, true, value); }
}
public sealed class Collision
{
    private readonly Actor actor;
    internal Collision(Actor actor) { this.actor = actor; }
    public ulong ActorGuid => actor.Guid;
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class ScriptIdAttribute(string id) : Attribute
{
    public string Id { get; } = id;
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = false)]
public sealed class ScriptAliasAttribute : Attribute
{
    public string Id { get; }
    public string Assembly { get; }
    public string Type { get; }
    public ScriptAliasAttribute(string id) { Id = id; Assembly = Type = ""; }
    public ScriptAliasAttribute(string assembly, string type) { Assembly = assembly; Type = type; Id = ""; }
}
