using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Linq;
using System.Buffers.Binary;
using Vector3 = Overload.Maths.Vector3;

namespace Overload.ManagedHost;

public static unsafe partial class Bootstrap
{
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    private struct InstanceInfo { internal uint Size, Flags; internal ulong Type, Context, Owner, Guid; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    private struct LifecycleCall { internal uint Size, Event; internal InstanceHandle Instance; internal float Delta; internal uint Flags; internal ulong Other; }
    private sealed class Instance(Behaviour value, ulong guid, ProjectType type)
    {
        internal readonly Behaviour Value = value;
        internal readonly ulong Guid = guid;
        internal readonly ProjectType Type = type;
        internal bool Awake, Started, Enabled, Faulted, Destroyed;
        internal bool RestorePending, RestoreAwake, RestoreStarted, RestoreEnabled;
    }
    private static ulong nextInstance;
    private static bool constructing;
    private static delegate* unmanaged[Cdecl]<ulong, ulong, uint, ulong, Utf8, ulong*, uint> gameplay;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint ConfigureGameplay(uint version, uint size,
        delegate* unmanaged[Cdecl]<ulong, ulong, uint, ulong, Utf8, ulong*, uint> entry) => Guard(default, () => {
        Require(version == 1 && size == 8 && entry != null, Status.AbiMismatch, "Gameplay bridge version/size mismatch");
        Require(gameplay == null, Status.Busy, "Gameplay bridge already configured");
        gameplay = entry;
    });

    private sealed class Access(Session session) : INativeAccess
    {
        public string LogicalName => session.LogicalName;
        public UI.CanvasUpdateRegistry UIRegistry { get { Check(); return session.UIRegistry; } }
        public IReadOnlyList<Behaviour> GetBehaviours(ulong owner) {
            _ = Query(owner, 1); // Validate the captured native owner and current session.
            return session.Instances.Values.Where(i => !i.Destroyed && !i.Faulted && i.Value.owner?.Handle == owner).Select(i => i.Value).ToArray();
        }
        private void Check() {
            Require(Environment.CurrentManagedThreadId == mainThread && services.IsMain != null && services.IsMain() == 1,
                Status.WrongThread, "Gameplay API requires Main");
            Require(!session.Closed && session.Core != null && gameplay != null, Status.SessionNotFound, "Closed/unbound gameplay session");
            Require(!constructing, Status.InvalidLifecycle, "Native APIs are unavailable during construction");
        }
        public ulong Query(ulong owner, uint operation, ulong argument = 0, string text = "") {
            Check(); ++nativeCalls;
            Require(!session.Closing || operation <= 4 || operation is 12 or 13 or 14 or 15 or 16 or 18 or 19 or 21 or 22 or 30 or 40 or 42 or 44 or 48 or 49 or 51 or 53 or 61 or 62 || operation == 50 && argument == 0 && text == "release" || operation == 52 && argument == 0, Status.SessionClosing, "Session closing rejects mutations");
            // Behaviour names are metadata names; native receives the explicit logical assembly too.
            if (operation is 7 or 8) text = LogicalName + "\n" + text;
            // Native context survives hot reload; driven ownership is a managed generation identity.
            if (operation is >= 60 and <= 62) text = session.Identity.ToString(System.Globalization.CultureInfo.InvariantCulture) + "\n" + text;
            byte[] bytes = Encoding.GetBytes(text);
            ulong result = 0;
            fixed (byte* p = bytes) Overload.Script.Bridge.Check((int)gameplay(services.Context, owner, operation, argument,
                new Utf8 { Data = p, Length = (ulong)bytes.Length }, &result));
            return result;
        }
        public bool IsActorAlive(ulong owner) {
            Check(); ++nativeCalls;
            ulong result = 0;
            uint status = gameplay(services.Context, owner, 21, 0, default, &result);
            if (status == 2) return false; // OV_SCRIPT_INVALID_HANDLE only.
            Overload.Script.Bridge.Check((int)status);
            return result != 0;
        }
        public void SetUIMesh(ulong renderer, UI.UIVertex[] vertices, int[] indices) {
            Check(); ++nativeCalls;
            Require(!session.Closing, Status.SessionClosing, "Session closing rejects UI mesh upload");
            byte[] payload = UI.UIMeshEncoding.Encode(vertices, indices);
            ulong result = 0;
            fixed (byte* p = payload) Overload.Script.Bridge.Check((int)gameplay(services.Context, renderer, 45, 0,
                new Utf8 { Data = p, Length = (ulong)payload.Length }, &result));
        }
        public ulong ExchangeFont(ulong owner, uint operation, byte[] packet) {
            Check(); ++nativeCalls;
            ArgumentNullException.ThrowIfNull(packet);
            Require(operation is >= 100 and <= 103 && packet.Length is >= 64 and <= 1024 * 1024,
                Status.InvalidArgument, "Invalid font binary exchange");
            Require(!session.Closing || operation == 102, Status.SessionClosing, "Session closing permits only font release");
            ulong result = 0;
            fixed (byte* p = packet) Overload.Script.Bridge.Check((int)gameplay(services.Context, owner, operation, session.Identity,
                new Utf8 { Data = p, Length = (ulong)packet.Length }, &result));
            return result;
        }
        public Vector3 Read(ulong component, bool box) {
            Check(); ++nativeCalls;
            var v = box ? session.Core!.OvCore_ECS_Components_CPhysicalBox_get_size(component) :
                session.Core!.OvCore_ECS_Components_CTransform_get_position(component);
            return new(v.x, v.y, v.z);
        }
        public void Write(ulong component, bool box, Vector3 value) {
            Check(); ++nativeCalls; var v = new Overload.Script.Vector3 { x = value.X, y = value.Y, z = value.Z };
            if (box) session.Core!.OvCore_ECS_Components_CPhysicalBox_set_size(component, v);
            else session.Core!.OvCore_ECS_Components_CTransform_set_position(component, v);
        }
        public void Log(string text) { Check(); Bootstrap.Log(text); }
    }
    private static Instance FindInstance(SessionHandle s, InstanceHandle id) {
        Session session = Lookup(s);
        Require(id.Generation == s.Generation && session.Instances.ContainsKey(id.Id), Status.InstanceNotFound, "Unknown instance or stale session generation");
        return session.Instances[id.Id];
    }
    private static void ReleaseSessionFonts(Session session) {
        if (session.Core == null || gameplay == null || session.Identity == 0) return;
        byte[] packet = new byte[64];
        BinaryPrimitives.WriteUInt32LittleEndian(packet, 0x3146544f);
        BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(4), 1);
        BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(8), 64);
        BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(12), 64);
        new Access(session).ExchangeFont(0, 102, packet);
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint TickUI(SessionHandle handle, ulong sceneAnchor, float deltaTime, float unscaledDeltaTime) => Guard(handle, () => {
        Require(float.IsFinite(deltaTime) && float.IsFinite(unscaledDeltaTime) && deltaTime >= 0 && unscaledDeltaTime >= 0,
            Status.InvalidArgument, "Invalid UI frame deltas");
        Session session = Lookup(handle);
        Require(!session.Closing, Status.SessionClosing, "UI tick during session close");
        var access = new Access(session); _ = access.Query(sceneAnchor, 1);
        UI.GraphicRebuildTracker.Update(new Actor(access, sceneAnchor), deltaTime, unscaledDeltaTime);
    });
    private static void Create(SessionHandle s, InstanceInfo* info, InstanceHandle* output) {
        if (output != null) *output = default;
        Require(info != null && output != null && info->Size == 40 && info->Flags == 0 && info->Context == services.Context,
            Status.InvalidArgument, "Invalid instance descriptor/context");
        Session session = Lookup(s);
        ProjectType type = session.Types.Find(t => t.Token == info->Type) ?? throw new Failure(Status.TypeNotFound, "Unknown type token");
        Access access = new(session);
        Require(access.Query(info->Owner, 1) == info->Guid, Status.InvalidArgument, "Owner GUID/handle mismatch");
        Behaviour value;
        try { constructing = true; value = (Behaviour)Activator.CreateInstance(type.Type)!; }
        catch (Exception ex) { throw new InvalidOperationException($"Construct script={type.Name} actor={info->Guid}", ex); }
        finally { constructing = false; }
        value.owner = new Actor(access, info->Owner);
        if (value is EventSystems.UIBehaviour ui) ui.EnsureNativeComponents();
        ulong id = checked(++nextInstance);
        session.Instances.Add(id, new Instance(value, info->Guid, type));
        *output = new InstanceHandle { Id = id, Generation = s.Generation };
    }
    private static void Destroy(Instance instance, bool reloading = false) {
        if (instance.Destroyed) return;
        instance.Destroyed = true; instance.Enabled = false;
        try { if (instance.Awake) { if (reloading) instance.Value.OnReloadRelease(); else instance.Value.OnDestroy(); } }
        catch (Exception ex) { Log($"Destroy script={instance.Value.GetType().FullName} actor={instance.Guid}: {ex}"); }
        finally {
            try { if (instance.Value is EventSystems.UIBehaviour ui) ui.ReleaseNativeComponents(); }
            catch (Exception ex) { Log($"Release native UI script={instance.Value.GetType().FullName} actor={instance.Guid}: {ex}"); }
            finally { instance.Value.ReleaseSubscriptions(Log); instance.Value.owner = null; }
        }
    }
    private static void Invoke(SessionHandle s, LifecycleCall* call) {
        Require(call != null && call->Size == 40 && call->Flags == 0 && float.IsFinite(call->Delta), Status.InvalidArgument, "Invalid lifecycle call");
        Instance i = FindInstance(s, call->Instance);
        ++lifecycleCalls;
        uint e = call->Event;
        Require(e >= 1 && e <= 15, Status.InvalidLifecycle, "Unknown/reserved lifecycle");
        if (e == 15) { RestoreReload(i); return; }
        if (e == 14) { Destroy(i); return; }
        Require(!i.Destroyed, Status.InvalidLifecycle, "Destroyed instance");
        Require(!i.Faulted, Status.InstanceFaulted, "Faulted instance");
        if (e == 1 && i.Awake || e == 2 && i.Started || e == 3 && i.Enabled || e == 4 && !i.Enabled) return;
        Require(e == 1 || i.Awake, Status.InvalidLifecycle, "Awake must precede lifecycle");
        if (e >= 5 && (!i.Started || !i.Enabled || i.Value is EventSystems.UIBehaviour { enabled: false })) return;
        try {
            Collision? other = e >= 8 ? new Collision(new Actor(new Access(Lookup(s)), call->Other)) : null;
            if (other != null) _ = other.ActorGuid;
            switch (e) {
                case 1: i.Awake = true; i.Value.OnAwake(); break;
                case 2: i.Started = true; i.Value.OnStart(); break;
                case 3: i.Enabled = true; if (i.Value is not EventSystems.UIBehaviour { enabled: false }) i.Value.OnEnable(); break;
                case 4: i.Enabled = false; if (i.Value is not EventSystems.UIBehaviour { enabled: false }) i.Value.OnDisable(); break;
                case 5: i.Value.OnUpdate(call->Delta); break;
                case 6: i.Value.OnFixedUpdate(call->Delta); break;
                case 7: i.Value.OnLateUpdate(call->Delta); break;
                case 8: i.Value.OnCollisionEnter(other!); break;
                case 9: i.Value.OnCollisionStay(other!); break;
                case 10: i.Value.OnCollisionExit(other!); break;
                case 11: i.Value.OnTriggerEnter(other!); break;
                case 12: i.Value.OnTriggerStay(other!); break;
                case 13: i.Value.OnTriggerExit(other!); break;
            }
        } catch (Exception ex) {
            i.Faulted = true;
            string[] names = { "", "OnAwake", "OnStart", "OnEnable", "OnDisable", "OnUpdate", "OnFixedUpdate", "OnLateUpdate",
                "OnCollisionEnter", "OnCollisionStay", "OnCollisionExit", "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit" };
            throw new InvalidOperationException($"script={i.Value.GetType().FullName} actor={i.Guid} callback={names[e]}", ex);
        }
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint UpdateUI(SessionHandle handle, ulong sceneAnchor, float width, float height) => Guard(handle, () => {
        Require(float.IsFinite(width) && float.IsFinite(height) && width > 0 && height > 0, Status.InvalidArgument, "Invalid UI viewport");
        Session session = Lookup(handle);
        Require(!session.Closing, Status.SessionClosing, "UI rebuild during session close");
        var access = new Access(session);
        var components = session.Instances.Values.Where(i => !i.Destroyed && !i.Faulted && i.Value.owner != null &&
            access.Query(sceneAnchor, 19, i.Value.Owner.Handle) != 0).Select(i => i.Value).ToArray();
        session.UIRegistry.Update(new Actor(access, sceneAnchor), new(width, height), components, ex => Log("UI rebuild: " + ex));
    });
#if S1_ACCEPTANCE
    private static Actor? heldActor;
    private static Box? heldBox;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint HoldFacade(SessionHandle s, InstanceHandle instance) => Guard(s, () => {
        heldActor = FindInstance(s, instance).Value.Owner;
        heldBox = heldActor.GetComponent<Box>();
    });
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint ProbeFacade(uint component) => Guard(default, () => {
        if (component == 0) _ = heldActor!.Guid;
        else _ = heldBox!.Size;
    });
#endif
}
