using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Overload.ManagedHost;

public static unsafe partial class Bootstrap
{
    // Only weak references and plain strings may outlive a project session.
    private sealed record Retired(WeakReference Context, string Directory);
    private static readonly List<Retired> retired = new();
    private static ulong collectedContexts;
    private static void SweepRetired() {
        for (int i = retired.Count - 1; i >= 0; --i)
            if (!retired[i].Context.IsAlive) { retired.RemoveAt(i); ++collectedContexts; }
    }
    private static string ShadowCopy(string file) {
        SweepRetired();
        Require(retired.Count < 64, Status.Busy,
            "64 uncollected project contexts; run CollectibleStats diagnostic. Check external events, static roots, threads/tasks; restart if retained. No further generation accepted.");
        string source = Path.GetDirectoryName(file)!;
        string root = Environment.GetEnvironmentVariable("OV_SCRIPTING_SHADOW_ROOT") ?? Path.Combine(Path.GetTempPath(), "Overload", "ManagedSessions");
        string destination = Path.Combine(root, Environment.ProcessId.ToString(), Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(destination);
        Copy(source, destination);
        return Path.Combine(destination, Path.GetFileName(file));
    }
    private static void Copy(string source, string destination) {
        foreach (string file in Directory.EnumerateFiles(source)) {
            Require((File.GetAttributes(file) & FileAttributes.ReparsePoint) == 0, Status.InvalidArgument, "Project dependency must not be a link: " + file);
            File.Copy(file, Path.Combine(destination, Path.GetFileName(file)));
        }
        foreach (string directory in Directory.EnumerateDirectories(source)) {
            string name = Path.GetFileName(directory);
            if (name is "bin" or "obj" or ".overload" or "generations" or "host") continue;
            Require((File.GetAttributes(directory) & FileAttributes.ReparsePoint) == 0, Status.InvalidArgument, "Project dependency directory must not be a link: " + directory);
            string child = Path.Combine(destination, name); Directory.CreateDirectory(child); Copy(directory, child);
        }
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void Retire(ProjectContext context) {
        retired.Add(new(new WeakReference(context, trackResurrection: true), context.ShadowDirectory));
        context.Unload();
    }
    // Explicit diagnostics only. No forced GC occurs in the frame loop or normal reload.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint CollectibleStats(uint collect, ulong* alive, ulong* collected) => Guard(default, () => {
        Require(collect <= 1 && alive != null && collected != null, Status.InvalidArgument, "Invalid collectible diagnostic");
        if (collect != 0) for (int i = 0; i < 8; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); }
        SweepRetired(); *alive = (ulong)retired.Count; *collected = collectedContexts;
        if (retired.Count != 0) Log($"ALC pending={retired.Count}; possible external events/static roots, tasks/threads or in-flight frames; shadow={retired[0].Directory}. Diagnose roots or restart.");
    });
    // Candidate objects have no lifecycle/native effects. Failure is before commit.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint PrepareReload(SessionHandle source, InstanceHandle oldId, SessionHandle destination, InstanceHandle newId) => Guard(destination, () => {
        Require(source != destination, Status.InvalidArgument, "Reload requires distinct generations");
        Instance old = FindInstance(source, oldId), next = FindInstance(destination, newId);
        Require(!old.Destroyed && !next.Awake && !next.RestorePending, Status.InvalidLifecycle, "Invalid reload states");
        HashSet<ulong> used = new();
        foreach (ProjectField field in old.Type.Fields) {
            var matches = next.Type.Fields.Where(f => f.Id == field.Id || f.Aliases.Contains(field.Id)).ToArray();
            if (matches.Length == 0) continue; // Removed fields are deliberately dropped, never converted.
            Require(matches.Length == 1 && used.Add(matches[0].Token), Status.UnsupportedField, "Ambiguous reload field " + field.Id);
            ProjectField target = matches[0];
            Require(field.Tag == target.Tag && field.Auxiliary == target.Auxiliary, Status.UnsupportedField, "Incompatible reload field " + field.Id);
            FieldValue value = default;
            try { ReadFieldValue(source, oldId, field.Token, &value); WriteFieldValue(destination, newId, target.Token, &value); }
            finally { if (value.Text.Data != null) ReleaseFieldValue(&value); }
        }
        next.RestorePending = true;
        next.RestoreAwake = old.Awake; next.RestoreStarted = old.Started; next.RestoreEnabled = old.Enabled;
    });
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint RetireReload(SessionHandle source) => Guard(source, () => {
        Session session = Lookup(source); Sessions.Remove(source.Id); session.Close(reloading: true);
    });
    private static void RestoreReload(Instance instance) {
        Require(instance.RestorePending && !instance.Destroyed, Status.InvalidLifecycle, "ReloadRestore requires successful preflight");
        instance.RestorePending = false; instance.Awake = instance.RestoreAwake;
        instance.Started = instance.RestoreStarted; instance.Enabled = instance.RestoreEnabled;
        try { if (instance.Awake) instance.Value.OnReloadRestore(); }
        catch (Exception ex) { instance.Faulted = true; throw new InvalidOperationException($"script={instance.Type.Name} actor={instance.Guid} callback=OnReloadRestore; committed, native effects are not rolled back", ex); }
    }
}
