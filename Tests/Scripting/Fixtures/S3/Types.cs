using System;
using System.IO;
using System.Threading;
using Overload;
using Overload.Maths;

namespace S3;

public class Trace : Behaviour
{
    private readonly int initialized = 73;
    protected void Event(string name) => File.AppendAllText(Environment.GetEnvironmentVariable("OV_S3_TRACE")!,
        GetType().Name + ":" + Owner.Guid + ":" + name + "\n");
    public override void OnAwake() { if (initialized != 73 || Owner.Guid == 0) throw new Exception("not prepared"); Event("awake"); }
    public override void OnEnable() => Event("enable");
    public override void OnStart() => Event("start");
    public override void OnDisable() => Event("disable");
    public override void OnUpdate(float dt) => Event("update");
    public override void OnFixedUpdate(float dt) => Event("fixed");
    public override void OnLateUpdate(float dt) => Event("late");
    public override void OnDestroy() { _ = Transform.Position; Event("destroy"); }
    public override void OnCollisionEnter(Collision other) { if (other.ActorGuid == Owner.Guid) throw new Exception("self contact"); Event("collision-enter"); }
    public override void OnCollisionStay(Collision other) { _ = other.ActorGuid; Event("collision-stay"); }
    public override void OnCollisionExit(Collision other) { _ = other.ActorGuid; Event("collision-exit"); }
    public override void OnTriggerEnter(Collision other) { _ = other.ActorGuid; Event("trigger-enter"); }
    public override void OnTriggerStay(Collision other) { _ = other.ActorGuid; Event("trigger-stay"); }
    public override void OnTriggerExit(Collision other) { _ = other.ActorGuid; Event("trigger-exit"); }
}
public sealed class Fault : Trace
{
    public override void OnUpdate(float dt) { Event("fault"); throw new InvalidOperationException("S3 isolated failure"); }
    public override void OnDestroy() { base.OnDestroy(); throw new InvalidOperationException("S3 cleanup failure"); }
}
public sealed class BadConstructor : Behaviour
{
    public BadConstructor() { _ = Owner.Guid; }
}
public sealed class SelfRemove : Trace
{
    public override void OnUpdate(float dt) { Event("remove"); Owner.RemoveBehaviour<SelfRemove>(); }
}
public sealed class Added : Trace { }
public sealed class RemoveInAwake : Trace
{
    public override void OnAwake() { base.OnAwake(); Owner.RemoveBehaviour<RemoveInAwake>(); }
}
public sealed class RemoveInCollision : Trace
{
    public override void OnCollisionEnter(Collision other) { base.OnCollisionEnter(other); Owner.Destroy(); }
}
public sealed class Mutate : Trace
{
    private bool done;
    public override void OnUpdate(float dt) {
        if (done) return; done = true;
        Event("mutate");
        Owner.RemoveBehaviour<Trace>();
        Owner.AddBehaviour<Added>();
        Owner.Find("victim")!.Destroy();
        var created = Owner.Create("created"); created.AddBehaviour<Added>();
    }
}
public sealed class SelfDestroy : Trace
{
    public override void OnUpdate(float dt) { Event("self-destroy"); Owner.Destroy(); }
}
public sealed class DisableSelf : Trace
{
    private bool done;
    public override void OnUpdate(float dt) { Event("update"); if (!done) { done = true; Owner.Active = false; } }
}
public sealed class Cache : Trace
{
    internal static Box? OldBox;
    internal static Actor? OldActor;
    public override void OnAwake() { base.OnAwake(); OldBox = Owner.GetComponent<Box>(); OldActor = Owner; }
}
public sealed class Stale : Trace
{
    public override void OnAwake() {
        base.OnAwake();
        bool box = false, actor = false;
        try { _ = Cache.OldBox!.Size; } catch { box = true; }
        try { _ = Cache.OldActor!.Guid; } catch { actor = true; }
        if (!box || !actor) throw new Exception("stale handle accepted");
        Transform.Position = new Vector3(91, 92, 93); Event("stale-rejected");
    }
}
public sealed class Worker : Trace
{
    public override void OnAwake() {
        base.OnAwake(); bool rejected = false;
        var thread = new Thread(() => { try { _ = Transform.Position; } catch { rejected = true; } });
        thread.Start(); thread.Join();
        if (!rejected) throw new Exception("worker accepted");
        Event("worker-rejected");
    }
}
public sealed class Switch : Trace
{
    public override void OnUpdate(float dt) { Event("switch"); Owner.LoadScene(Environment.GetEnvironmentVariable("OV_S3_SCENE")!); }
}
