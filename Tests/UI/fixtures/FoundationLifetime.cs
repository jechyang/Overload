using System;
using Overload;
using Overload.UI;

namespace UIFixture.Foundation;

public class EventSource : Behaviour
{
    public UIEvent<int> Changed { get; }
    public EventSource() => Changed = new(this);
}

public class EventReceiver : Behaviour
{
    public int received;
    public void Receive(int value) => received += value;
}

public class EventLifetimeProbe : Behaviour
{
    public int passed, received;
    private EventReceiver? retainedReceiver;
    private UIEvent<int>? retainedEvent, live;
    private WeakReference? retiredReceiver;
    private UISessionRegistry<EventReceiver>? registry;
    private static void Check(bool condition, string message) {
        if (!condition) throw new InvalidOperationException(message);
    }
    private void Receive(int value) => received += value;
    private void BindLive() {
        live = Owner.Find("LiveSource")!.GetComponent<EventSource>()!.Changed;
        live.AddListener(Receive);
    }
    public override void OnReloadRestore() => BindLive();
    public override void OnUpdate(float deltaTime) {
        switch ((int)deltaTime) {
            case 1:
                retainedEvent = Owner.Find("EventSource")!.GetComponent<EventSource>()!.Changed;
                retainedReceiver = Owner.Find("EventReceiver")!.GetComponent<EventReceiver>()!;
                registry = UISessionRegistry<EventReceiver>.For(Owner);
                registry.Register(retainedReceiver, retainedReceiver);
                Check(registry.Count == 1 && ReferenceEquals(registry, UISessionRegistry<EventReceiver>.For(retainedReceiver.Owner)), "registry belongs to the session");
                retainedEvent.AddListener(retainedReceiver.Receive);
                retainedEvent.Invoke(2);
                Check(retainedReceiver.received == 2, "bound event dispatch");
                passed = 1; break;
            case 2:
                Check(registry!.Count == 0, "destroyed receiver must leave the session registry immediately");
                retainedEvent!.Invoke(7);
                Check(retainedReceiver!.received == 2, "destroyed receiver must unsubscribe");
                retiredReceiver = new(retainedReceiver); retainedReceiver = null;
                passed = 2; break;
            case 3:
                retainedEvent!.Invoke(7);
                bool disposed = false;
                try { retainedEvent.AddListener(Receive); }
                catch (ObjectDisposedException) { disposed = true; }
                Check(disposed, "event source disposal closes retained event object");
                retainedEvent = null;
                passed = 3; break;
            case 4:
                BindLive(); live!.Invoke(1); Check(received == 1, "live listener before reload");
                passed = 4; break;
            case 5:
                live!.Invoke(1); passed = 5; break;
            case 6:
                GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
                Check(retiredReceiver != null && !retiredReceiver.IsAlive, "live event must not retain removed Behaviour");
                passed = 6; break;
        }
    }
}
