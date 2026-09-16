using System;
using System.Runtime.CompilerServices;
using Vector3 = Overload.Maths.Vector3;

[assembly: InternalsVisibleTo("Overload.ManagedHost")]

namespace Overload;

public abstract class Behaviour
{
    internal Actor? owner;
    private readonly System.Collections.Generic.List<IDisposable> subscriptions = new();
    private bool subscriptionsClosed;
    protected T TrackSubscription<T>(T subscription) where T : IDisposable {
        if (subscriptionsClosed) { subscription.Dispose(); throw new InvalidOperationException("Behaviour subscription scope closed"); }
        subscriptions.Add(subscription); return subscription;
    }
    internal void ReleaseSubscriptions(Action<string> log) {
        subscriptionsClosed = true;
        foreach (IDisposable subscription in subscriptions) try { subscription.Dispose(); } catch (Exception ex) { log("Subscription cleanup: " + ex); }
        subscriptions.Clear();
    }
    public Actor Owner => owner ?? throw new InvalidOperationException("Owner is unavailable during construction");
    public Transform Transform => Owner.Transform;
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
    Vector3 Read(ulong component, bool box);
    void Write(ulong component, bool box, Vector3 value);
    void Log(string text);
    string LogicalName { get; }
}

public sealed class Actor
{
    internal readonly INativeAccess Access;
    internal readonly ulong Handle;
    internal Actor(INativeAccess access, ulong handle) { Access = access; Handle = handle; }
    public ulong Guid => Access.Query(Handle, 1);
    public Transform Transform => GetComponent<Transform>()!;
    public bool Active { get => Access.Query(Handle, 4) != 0; set => Access.Query(Handle, 5, value ? 1ul : 0ul); }
    public T? GetComponent<T>() where T : class
    {
        uint kind = typeof(T) == typeof(Transform) ? 2u : typeof(T) == typeof(Box) ? 3u :
            throw new NotSupportedException("S3 components: Transform and Box");
        ulong handle = Access.Query(Handle, kind);
        if (handle == 0) return null;
        return (T)(object)(kind == 2 ? (object)new Transform(Access, handle) : new Box(Access, handle));
    }
    public void Destroy() => Access.Query(Handle, 6);
    public void AddBehaviour<T>() where T : Behaviour => Access.Query(Handle, 7, 0, typeof(T).FullName!);
    public void RemoveBehaviour<T>() where T : Behaviour => Access.Query(Handle, 8, 0, typeof(T).FullName!);
    public Actor? Find(string name) { ulong id = Access.Query(Handle, 9, 0, name); return id == 0 ? null : new Actor(Access, id); }
    public Actor Create(string name) => new(Access, Access.Query(Handle, 10, 0, name));
    public void LoadScene(string path) => Access.Query(Handle, 11, 0, path);
    public void Log(string message) { _ = Guid; Access.Log(message); }
}
public sealed class Transform
{
    private readonly INativeAccess access;
    private readonly ulong handle;
    internal Transform(INativeAccess access, ulong handle) { this.access = access; this.handle = handle; }
    public Vector3 Position { get => access.Read(handle, false); set => access.Write(handle, false, value); }
}
public sealed class Box
{
    private readonly INativeAccess access;
    private readonly ulong handle;
    internal Box(INativeAccess access, ulong handle) { this.access = access; this.handle = handle; }
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
