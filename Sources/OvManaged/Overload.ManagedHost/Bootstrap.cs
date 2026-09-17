using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using CoreApi = Overload.Generated.OvCore.Api;

namespace Overload.ManagedHost;

// Resident/default ALC. Project metadata is owned only by its session.
public static unsafe partial class Bootstrap
{
    static Bootstrap()
    {
        // The resident contract has one explicit deployment location. Never accept
        // a project-local ScriptCore copy with a different type identity.
        System.Runtime.Loader.AssemblyLoadContext.Default.LoadFromAssemblyPath(
            Path.Combine(Path.GetDirectoryName(typeof(Bootstrap).Assembly.Location)!, "Overload.ScriptCore.dll"));
    }
    internal const string Signature = "1f7e282e2f1353962d004dfe3f9b88a52fe17311d6b4d51491d8622a529a1b10";
    private sealed class Session
    {
        internal CoreApi? Core;
        internal nuint Entry;
        internal ProjectContext? Project;
        internal readonly List<ProjectType> Types = new();
        internal readonly Dictionary<ulong, Instance> Instances = new();
        internal readonly UI.CanvasUpdateRegistry UIRegistry = new();
        internal bool Closed;
        internal bool Closing;
        internal string LogicalName = "";
        internal void Close(bool reloading = false) {
            Closing = true;
            foreach (Instance instance in Instances.Values) Destroy(instance, reloading);
            Instances.Clear(); UIRegistry.Clear(); Closed = true;
            Core = null; Entry = 0; Types.Clear();
            if (Project != null) Retire(Project); Project = null;
        }
    }
    private sealed class Failure(Status status, string message) : Exception(message)
    { internal readonly Status Status = status; }
    private static readonly UTF8Encoding Encoding = new(false, true);
    private static readonly Dictionary<ulong, Session> Sessions = new();
    private static readonly Dictionary<nuint, ulong> Buffers = new();
    [ThreadStatic] private static Dictionary<SessionHandle, string>? errors;
    private static BootstrapServices services;
    private static int mainThread;
    private static bool busy;
    private static ulong nextId, attachmentStart;

    private static void Require(bool condition, Status status, string message)
    { if (!condition) throw new Failure(status, message); }
    private static string Decode(Utf8 text)
    {
        Require(text.Length <= int.MaxValue && (text.Data != null || text.Length == 0), Status.InvalidArgument, "Invalid UTF-8 pointer/length");
        return Encoding.GetString(new ReadOnlySpan<byte>(text.Data, (int)text.Length));
    }
    private static uint Error(SessionHandle session, Exception error)
    {
        (errors ??= new())[session] = error.ToString();
        return (uint)(error is Failure failure ? failure.Status : Status.ManagedException);
    }
    private static uint Guard(SessionHandle session, Action action, bool requireAttached = true)
    {
        bool entered = false;
        try {
            Require(Environment.CurrentManagedThreadId == Volatile.Read(ref mainThread), Status.WrongThread, "Host entry requires captured engine Main");
            Require(!requireAttached || (services.Context != 0 && services.IsMain != null && services.IsMain() == 1), Status.WrongThread, "Native Main/context is not attached");
            Require(!busy, Status.Busy, "Reentrant Host ABI calls are rejected at the S1 boundary");
            busy = entered = true;
            action();
            errors?.Remove(session);
            return 0;
        } catch (Exception ex) { return Error(session, ex); }
        finally { if (entered) busy = false; }
    }
    private static Session Lookup(SessionHandle handle)
    {
        Require(handle.Id != 0 && handle.Id == handle.Generation && Sessions.ContainsKey(handle.Id), Status.SessionNotFound, "Unknown/closed session or stale generation");
        return Sessions[handle.Id];
    }
    private static void Log(string text)
    {
        byte[] bytes = Encoding.GetBytes(text);
        fixed (byte* p = bytes) services.Log(0, new Utf8 { Data = p, Length = (ulong)bytes.Length });
    }
    private static Utf8 Allocate(string text)
    {
        byte[] bytes = Encoding.GetBytes(text);
        if (bytes.Length == 0) return default;
        byte* memory = (byte*)NativeMemory.Alloc((nuint)bytes.Length);
        try {
            bytes.CopyTo(new Span<byte>(memory, bytes.Length));
            Buffers.Add((nuint)memory, (ulong)bytes.Length);
            return new Utf8 { Data = memory, Length = (ulong)bytes.Length };
        } catch { NativeMemory.Free(memory); throw; }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint GetHostApi(uint version, uint size, byte* signature, HostApi* output)
    {
        try {
            // output is a HostApi*, not an untyped size-byte buffer. Clear every slot,
            // even when the requested contract size is wrong or a prior table is reused.
            if (output != null) *output = default;
            Require(output != null && signature != null, Status.InvalidArgument, "Null Host ABI handshake argument");
            Require(version == 1 && size == sizeof(HostApi) && new ReadOnlySpan<byte>(signature, 32).SequenceEqual(Convert.FromHexString(Signature)), Status.AbiMismatch, "Host ABI version/size/signature mismatch");
            Require(sizeof(HostApi) == 184 && sizeof(SessionInfo) == 64 && sizeof(BootstrapServices) == 32, Status.AbiMismatch, "Host ABI layout mismatch");
            int thread = Environment.CurrentManagedThreadId;
            Interlocked.CompareExchange(ref mainThread, thread, 0);
            Require(thread == mainThread, Status.WrongThread, "Bootstrap already belongs to another Main thread");
            HostApi table = new() {
                Version = 1, Size = (uint)sizeof(HostApi), ReleaseUtf8 = &ReleaseUtf8,
                ReleaseValue = &ReleaseValue, TakeError = &TakeError, CreateSession = &CreateSession,
                CloseSession = &CloseSession, Bind = &Bind, TypeCount = &TypeCount, TypeInfo = &TypeInfo,
                TypeAlias = &TypeAlias, ReleaseType = &ReleaseType, FieldInfo = &FieldInfo,
                FieldAlias = &FieldAlias, ReleaseField = &ReleaseField, CreateInstance = &CreateInstance,
                DestroyInstance = &DestroyInstance, Dispatch = &Dispatch, ReadField = &ReadField, WriteField = &WriteField
            };
            Convert.FromHexString(Signature).CopyTo(new Span<byte>(table.Signature, 32));
            *output = table;
            errors?.Remove(default);
            return 0;
        } catch (Exception ex) { return Error(default, ex); }
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint Configure(BootstrapServices* input) => Guard(default, () => {
        Require(input != null && input->Version == 1 && input->Size == sizeof(BootstrapServices), Status.AbiMismatch, "Bootstrap services version/size mismatch");
        Require(services.Context == 0, Status.Busy, "Resident host already attached");
        Require(input->Context != 0 && input->IsMain != null && input->Log != null && input->IsMain() == 1, Status.InvalidArgument, "Missing native context/Main/log services");
        services = *input;
        attachmentStart = nextId;
    }, false);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint Detach() => Guard(default, () => {
        Require(Buffers.Count == 0 && TypeResults.Count == 0 && FieldResults.Count == 0 && FieldValues.Count == 0, Status.BufferOwnership, "Release owned Host ABI results before detach");
        foreach (Session session in Sessions.Values) session.Close();
        Sessions.Clear();
        services = default;
        gameplay = null;
        errors?.Clear();
    });
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint CreateSession(SessionInfo* info, SessionHandle* output) => Guard(default, () => {
        if (output != null) *output = default;
        Require(info != null && output != null && info->Size == sizeof(SessionInfo) && info->Flags == 0 && info->Generation == 0, Status.InvalidArgument, "Session requires size=64, flags=0, host-assigned generation=0");
        string root = Decode(info->ProjectRoot), assembly = Decode(info->AssemblyPath), logical = Decode(info->LogicalName);
        Require(Path.IsPathFullyQualified(root) && Directory.Exists(root) && logical.Length != 0, Status.InvalidArgument, "Session requires an existing absolute project root and logical name");
        Session session = LoadProject(assembly, logical);
        ulong id = checked(nextId + 1);
        Sessions.Add(id, session);
        nextId = id;
        *output = new SessionHandle(id, id);
        Log($"session {id} ready; logical={logical}; context={services.Context}; Main={mainThread}");
    });
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint CloseSession(SessionHandle session) => Guard(session, () => {
        Require(session.Id > attachmentStart && session.Id <= nextId && session.Id == session.Generation, Status.SessionNotFound, "Unknown session generation");
        if (Sessions.Remove(session.Id, out Session? value)) {
            value.Close();
            Log($"session {session.Id} closed; native bindings and project metadata released");
        }
    });
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint Bind(SessionHandle handle, Utf8 module, uint version, uint size, byte* signature, nuint entry) => Guard(handle, () => {
        Session session = Lookup(handle);
        Require(Decode(module) == "OvCore", Status.NativeAbiMismatch, "S1 resident module is OvCore");
        Require(signature != null && entry != 0 && version == 1 && size == CoreApi.TableSize && new ReadOnlySpan<byte>(signature, 32).SequenceEqual(Convert.FromHexString(CoreApi.Signature)), Status.NativeAbiMismatch, "R6 module version/size/signature mismatch");
        Require(session.Entry == 0 || session.Entry == entry, Status.Busy, "Cannot replace a bound native module");
        try {
            // A second, real native handshake; never trust just the caller's metadata.
            CoreApi api = new((nint)entry, services.Context);
            session.Core = api;
            session.Entry = entry;
        } catch (Overload.Script.BridgeException ex) {
            throw new Failure(Status.NativeAbiMismatch, "R6 entry rejected the resident wrapper contract: " + ex);
        }
        Log($"session {handle.Id}: R6 OvCore handshake accepted");
    });
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint ReleaseUtf8(Utf8* value) => Guard(default, () => {
        Require(value != null, Status.InvalidArgument, "Null owned UTF-8 output");
        if (value->Data == null) Require(value->Length == 0, Status.BufferOwnership, "Invalid empty owned buffer");
        else {
            Require(Buffers.TryGetValue((nuint)value->Data, out ulong size) && size == value->Length, Status.BufferOwnership, "Unknown/already released owned buffer");
            Buffers.Remove((nuint)value->Data);
            NativeMemory.Free(value->Data);
        }
        *value = default;
    }, false);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint TakeError(SessionHandle session, Utf8* output)
    {
        // Taking consumes only this thread/session's error, including WrongThread diagnostics.
        // Release remains Main-only: workers must never allocate an owned result.
        return Guard(session, () => {
            Require(output != null, Status.InvalidArgument, "Null error output");
            *output = default;
            if (errors != null && errors.TryGetValue(session, out string? error)) *output = Allocate(error);
        }, false);
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint TypeCount(SessionHandle s, uint* count) => Guard(s, () => {
        Require(count != null, Status.InvalidArgument, "Null count"); *count = 0;
        *count = checked((uint)Lookup(s).Types.Count);
    });
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint ReleaseValue(void* p) => Guard(default, () => ReleaseFieldValue((FieldValue*)p), false);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint ReleaseType(void* p) => Guard(default, () => ReleaseTypeResult((TypeResult*)p), false);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint ReleaseField(void* p) => Guard(default, () => ReleaseFieldResult((FieldResult*)p), false);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint TypeInfo(SessionHandle s, uint index, void* p) => Guard(s, () => GetTypeResult(s, index, (TypeResult*)p));
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint TypeAlias(SessionHandle s, ulong token, uint index, Utf8* p) => Guard(s, () => {
        Require(p != null, Status.InvalidArgument, "Null alias output"); *p = default;
        ProjectType type = Lookup(s).Types.Find(t => t.Token == token) ?? throw new Failure(Status.TypeNotFound, "Unknown type token");
        Require(index < type.Aliases.Count, Status.InvalidArgument, "Alias index out of range");
        *p = Allocate(type.Aliases[(int)index]);
    });
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint FieldInfo(SessionHandle s, ulong type, uint index, void* p) => Guard(s, () => GetFieldResult(s,type,index,(FieldResult*)p));
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint FieldAlias(SessionHandle s, ulong type, ulong field, uint index, Utf8* p) => Guard(s, () => {
        Require(p != null, Status.InvalidArgument, "Null alias output"); *p = default;
        var f = FindField(FindType(s,type),field); Require(index < f.Aliases.Count, Status.InvalidArgument, "Field alias index out of range");
        *p = Allocate(f.Aliases[(int)index]);
    });
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint CreateInstance(SessionHandle s, void* info, InstanceHandle* p) => Guard(s, () => Create(s, (InstanceInfo*)info, p));
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint DestroyInstance(SessionHandle s, InstanceHandle instance) => Guard(s, () => {
        Session session = Lookup(s);
        Instance value = FindInstance(s, instance);
        try { Destroy(value); } finally { session.Instances.Remove(instance.Id); }
    });
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint Dispatch(SessionHandle s, void* call) => Guard(s, () => Invoke(s, (LifecycleCall*)call));
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint ReadField(SessionHandle s, InstanceHandle instance, ulong field, void* p) => Guard(s, () => ReadFieldValue(s,instance,field,(FieldValue*)p));
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint WriteField(SessionHandle s, InstanceHandle instance, ulong field, void* p) => Guard(s, () => WriteFieldValue(s,instance,field,(FieldValue*)p));

#if S1_ACCEPTANCE
    // Compiled only into the acceptance artifact, never the production ManagedHost.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint Smoke(SessionHandle handle, ulong box, ulong transform, uint fail) => Guard(handle, () => {
        Session session = Lookup(handle);
        Require(session.Core != null, Status.NativeAbiMismatch, "R6 module is not bound");
        if (fail != 0) throw new InvalidOperationException("S1 deliberate managed exception");
        session.Core!.OvCore_ECS_Components_CPhysicalBox_set_size(box, new Overload.Script.Vector3 { x = 3, y = 4, z = 5 });
        session.Core.OvCore_ECS_Components_CTransform_set_position(transform, new Overload.Script.Vector3 { x = 6, y = 7, z = 8 });
        Log("CLR -> ManagedHost -> R6 modified native Box/Transform");
    });
#endif
}
