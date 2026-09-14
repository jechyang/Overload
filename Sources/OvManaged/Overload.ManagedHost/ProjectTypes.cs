using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Overload.ManagedHost;

public static unsafe partial class Bootstrap
{
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    private struct TypeResult
    {
        internal uint Size, Flags;
        internal ulong Token;
        internal Utf8 Id, Assembly, Type;
        internal uint AliasCount, FieldCount;
    }
    private sealed record ProjectType(ulong Token, string Id, string Assembly, string Name, List<string> Aliases, Type Type);
    private static ulong nextType;
    // Separate ownership receipt prevents releasing copied, forged or mixed type structs.
    private static readonly Dictionary<nuint, TypeResult> TypeResults = new();

    private sealed class ProjectContext(string file) : AssemblyLoadContext(isCollectible: true)
    {
        private readonly AssemblyDependencyResolver resolver = new(file);
        private readonly string directory = Path.GetDirectoryName(file)!;
        protected override Assembly? Load(AssemblyName name)
        {
            foreach (Assembly shared in new[] { typeof(Overload.Behaviour).Assembly, typeof(Bootstrap).Assembly })
                if (name.Name == shared.GetName().Name) return shared;
            string? dependency = resolver.ResolveAssemblyToPath(name);
            // Direct compiler fixtures and simple project libraries need no deps.json.
            // Resolve their adjacent dependencies only inside the explicit project directory.
            if (dependency == null) {
                string adjacent = Path.Combine(directory, name.Name + ".dll");
                if (File.Exists(adjacent)) dependency = adjacent;
            }
            return dependency == null ? null : LoadFromAssemblyPath(dependency);
        }
    }
    private static string Part(string text) => Encoding.GetByteCount(text) + ":" + text;
    // Alias wire encoding: I<len>:<id> or T<len>:<logical><len>:<metadata name>.
    private static string Tuple(string assembly, string type) => "T" + Part(assembly) + Part(type);
    private static string Explicit(string id) => "I" + Part(id);
    private static void IdentityText(string text)
    {
        Require(!string.IsNullOrWhiteSpace(text) && !text.Any(char.IsControl), Status.InvalidArgument, "Identity must be nonempty XML-safe text");
        _ = Encoding.GetByteCount(text);
    }
    private static Session LoadProject(string file, string logical)
    {
        IdentityText(logical);
        Session result = new() { LogicalName = logical };
        if (file.Length == 0) return result; // S1 bootstrap remains supported.
        Require(Path.IsPathFullyQualified(file) && File.Exists(file), Status.InvalidArgument, "Missing absolute project assembly: " + file);
        result.Project = new ProjectContext(file);
        try {
            Assembly assembly = result.Project.LoadFromAssemblyPath(file);
            Dictionary<string, string> keys = new(StringComparer.Ordinal);
            foreach (Type type in assembly.GetTypes().OrderBy(t => t.FullName, StringComparer.Ordinal)) {
                if (!type.IsSubclassOf(typeof(Overload.Behaviour))) continue;
                if (type.IsAbstract || type.ContainsGenericParameters || !type.IsVisible || type.GetConstructor(Type.EmptyTypes) == null) {
                    Log($"Skipped Behaviour {type.FullName}: requires public concrete closed type and public parameterless constructor");
                    continue;
                }
                foreach (MethodInfo method in type.GetMethods(BindingFlags.Public | BindingFlags.Instance))
                    Require(!method.Name.StartsWith("On", StringComparison.Ordinal) ||
                        !method.IsDefined(typeof(System.Runtime.CompilerServices.AsyncStateMachineAttribute), false),
                        Status.InvalidArgument, "async lifecycle is unsupported: " + type.FullName + "." + method.Name);
                string name = type.FullName!;
                IdentityText(name);
                string id = type.GetCustomAttribute<Overload.ScriptIdAttribute>()?.Id ?? "";
                if (type.IsDefined(typeof(Overload.ScriptIdAttribute), false)) IdentityText(id);
                string primary = id.Length == 0 ? Tuple(logical, name) : Explicit(id);
                List<string> aliases = new();
                if (id.Length != 0) aliases.Add(Tuple(logical, name));
                foreach (Overload.ScriptAliasAttribute alias in type.GetCustomAttributes<Overload.ScriptAliasAttribute>()) {
                    if (alias.Id.Length != 0) { IdentityText(alias.Id); aliases.Add(Explicit(alias.Id)); }
                    else { IdentityText(alias.Assembly); IdentityText(alias.Type); aliases.Add(Tuple(alias.Assembly, alias.Type)); }
                }
                foreach (string key in new[] { primary }.Concat(aliases)) {
                    Require(!keys.ContainsKey(key), Status.DuplicateIdentity,
                        $"Duplicate ScriptId/alias {key}: {keys.GetValueOrDefault(key)} and {name}");
                    keys.Add(key, name);
                }
                result.Types.Add(new ProjectType(checked(++nextType), id, logical, name, aliases, type));
            }
            return result;
        } catch { result.Close(); throw; }
    }
    private static void GetTypeResult(SessionHandle session, uint index, TypeResult* output)
    {
        Require(output != null, Status.InvalidArgument, "Null type output");
        *output = default;
        Session value = Lookup(session);
        Require(index < value.Types.Count, Status.TypeNotFound, "Type index out of range");
        ProjectType type = value.Types[(int)index];
        TypeResult info = new() { Size = (uint)sizeof(TypeResult), Token = type.Token, AliasCount = (uint)type.Aliases.Count };
        try {
            info.Id = Allocate(type.Id); info.Assembly = Allocate(type.Assembly); info.Type = Allocate(type.Name);
            TypeResults.Add((nuint)info.Type.Data, info);
            *output = info;
        } catch { Free(info.Id); Free(info.Assembly); Free(info.Type); throw; }
    }
    private static void Free(Utf8 value)
    {
        if (value.Data == null) return;
        Require(Buffers.Remove((nuint)value.Data, out ulong length) && length == value.Length, Status.BufferOwnership, "Invalid owned type member");
        NativeMemory.Free(value.Data);
    }
    private static void ReleaseTypeResult(TypeResult* output)
    {
        Require(output != null, Status.InvalidArgument, "Null type result");
        Require(TypeResults.TryGetValue((nuint)output->Type.Data, out TypeResult original) &&
            new ReadOnlySpan<byte>(output, sizeof(TypeResult)).SequenceEqual(new ReadOnlySpan<byte>(&original, sizeof(TypeResult))),
            Status.BufferOwnership, "Unknown/copied/modified type result");
        // Verify all members before releasing any of them.
        foreach (Utf8 field in new[] { original.Id, original.Assembly, original.Type })
            Require(field.Data == null || (Buffers.TryGetValue((nuint)field.Data, out ulong size) && size == field.Length), Status.BufferOwnership, "Type member already released");
        TypeResults.Remove((nuint)output->Type.Data);
        Free(output->Id); Free(output->Assembly); Free(output->Type);
        *output = default;
    }
}
