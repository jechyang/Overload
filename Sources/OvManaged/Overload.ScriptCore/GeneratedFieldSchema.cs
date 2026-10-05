using System;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace Overload;

/// <summary>Compiled field identity and access. The XML codecs remain shared with legacy assemblies.</summary>
public sealed record GeneratedFieldDescriptor(string Id, string Name, string DisplayName, string[] Aliases,
    Type FieldType, bool IsPublic, Func<object, object?> Read, Action<object, object?> Write);

public interface IFieldSchemaProvider
{
    // Null means this declaration needs compatibility discovery; an empty array means no serialized fields.
    GeneratedFieldDescriptor[]? GetDeclaredFields(Type type);
}

public static class GeneratedFieldSchema
{
    private sealed record Provider(IFieldSchemaProvider? Value);
    // Cache engine assemblies only. ProjectField already owns descriptors for the
    // lifetime of its session, so collectible projects need no process-wide cache.
    private static readonly ConditionalWeakTable<Assembly, Provider> Providers = new();
    public static bool IsGenerated(Type type) => ProviderFor(type.Assembly)?.GetDeclaredFields(type) != null;
    private static Provider CreateProvider(Assembly assembly) => new(assembly.GetType("Overload.Generated.__FieldSchemaProvider", false) is Type provider
        ? Activator.CreateInstance(provider) as IFieldSchemaProvider : null);
    private static IFieldSchemaProvider? ProviderFor(Assembly assembly) =>
        (assembly.IsCollectible ? CreateProvider(assembly) : Providers.GetValue(assembly, CreateProvider)).Value;

    public static GeneratedFieldDescriptor[] DeclaredFields(Type type)
    {
        if (ProviderFor(type.Assembly)?.GetDeclaredFields(type) is { } generated) return generated;
        // Compatibility for precompiled scripts and declarations the compiler cannot access legally.
        return type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly)
            .Where(f => !f.IsInitOnly && !f.IsLiteral && !f.IsDefined(typeof(NonSerializedAttribute)) &&
                (f.IsPublic || f.IsDefined(typeof(SerializeFieldAttribute))))
            .OrderBy(f => f.MetadataToken).Select(f => {
                string id = f.GetCustomAttribute<FieldIdAttribute>()?.Id ?? f.Name;
                var aliases = f.GetCustomAttributes<FormerlySerializedAsAttribute>().Select(a => a.Name).ToList();
                if (id != f.Name && !aliases.Contains(f.Name, StringComparer.Ordinal)) aliases.Add(f.Name);
                return new GeneratedFieldDescriptor(id, f.Name, f.GetCustomAttribute<InspectorNameAttribute>()?.DisplayName ?? f.Name,
                    aliases.ToArray(), f.FieldType, f.IsPublic, f.GetValue, f.SetValue);
            }).ToArray();
    }
}
