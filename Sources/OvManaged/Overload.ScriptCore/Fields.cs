using System;

namespace Overload;

[AttributeUsage(AttributeTargets.Field)]
public sealed class SerializeFieldAttribute : Attribute { }
[AttributeUsage(AttributeTargets.Field)]
public sealed class InspectorNameAttribute(string displayName) : Attribute { public string DisplayName { get; } = displayName; }
[AttributeUsage(AttributeTargets.Field)]
public sealed class FieldIdAttribute(string id) : Attribute { public string Id { get; } = id; }
[AttributeUsage(AttributeTargets.Field, AllowMultiple = true)]
public sealed class FormerlySerializedAsAttribute(string name) : Attribute { public string Name { get; } = name; }

// Persistent identities only. Resolution always uses the caller's scene and validated anchor.
public readonly record struct ActorRef(ulong Guid)
{
    public static ActorRef From(Actor owner, Actor target)
    {
        ArgumentNullException.ThrowIfNull(owner); ArgumentNullException.ThrowIfNull(target);
        if (!ReferenceEquals(owner.Access.UIRegistry, target.Access.UIRegistry) ||
            owner.Access.Query(owner.Handle, 19, target.Handle) == 0)
            throw new InvalidOperationException("Actor references must belong to the same scene and session");
        return new(target.Guid);
    }
    public Actor? Resolve(Actor owner)
    {
        ulong handle = owner.Access.Query(owner.Handle, 12, Guid);
        return handle == 0 ? null : new Actor(owner.Access, handle);
    }
}
public enum AssetKind : uint { Unknown, Model, Texture, Shader, Material, Sound, Scene, Script, Font, Prefab }
public readonly record struct AssetRef(AssetKind Kind, string Path)
{
    // Missing assets keep their identity; resolution does not load or instantiate resources.
    public bool Resolve(Actor owner) => owner.Access.Query(owner.Handle, 13, (ulong)Kind, Path ?? "") != 0;
}
