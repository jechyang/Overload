using System;
using System.Reflection;

namespace Overload;

internal interface IComponentReference { ulong ActorGuid { get; } string ComponentType { get; } }

/// <summary>A persistent, scene-relative component identity. It never retains a live component.</summary>
public readonly record struct ComponentRef<T>(ulong ActorGuid, string? ComponentType = null) : IComponentReference where T : class
{
    string IComponentReference.ComponentType => ComponentType ?? ComponentIdentity.Of(typeof(T));
    public T? Resolve(Actor owner)
    {
        ArgumentNullException.ThrowIfNull(owner);
        Actor? target = new ActorRef(ActorGuid).Resolve(owner);
        if (target == null) return null;
        string identity = ComponentType ?? ComponentIdentity.Of(typeof(T));
        T? found = null;
        foreach (T component in target.GetComponents<T>()) if (ComponentIdentity.Of(component.GetType()) == identity) {
            if (found != null) throw new InvalidOperationException("Ambiguous component reference identity");
            found = component;
        }
        return found;
    }
    public static ComponentRef<T> From(Actor owner, T component)
    {
        ArgumentNullException.ThrowIfNull(owner); ArgumentNullException.ThrowIfNull(component);
        Actor target = component switch {
            Behaviour behaviour => behaviour.Owner,
            NativeUIComponent native => native.Owner,
            Transform transform => transform.Owner,
            Box box => box.Owner,
            _ => throw new NotSupportedException("Not an Overload component: " + typeof(T).FullName)
        };
        if (!ReferenceEquals(owner.Access.UIRegistry, target.Access.UIRegistry) ||
            owner.Access.Query(owner.Handle, 19, target.Handle) == 0)
            throw new InvalidOperationException("Component references must belong to the same scene and session");
        // A caller cannot obtain a persistent identity from a removed component wrapper.
        if (component is NativeUIComponent nativeUI) nativeUI.ValidateReference();
        else if (component is Transform transform) _ = transform.Position;
        else if (component is Box box) _ = box.Size;
        return new(target.Guid, ComponentIdentity.Of(component.GetType()));
    }
}

internal static class ComponentIdentity
{
    internal static string Of(Type type)
    {
        if (typeof(Behaviour).IsAssignableFrom(type) || type.IsInterface) {
            string? id = type.GetCustomAttribute<ScriptIdAttribute>(false)?.Id;
            return string.IsNullOrEmpty(id) ? "managed:" + type.Assembly.GetName().Name + ":" + type.FullName : "script-id:" + id;
        }
        if (typeof(NativeUIComponent).IsAssignableFrom(type) || type == typeof(Transform) || type == typeof(Box))
            return "native:" + type.FullName;
        throw new NotSupportedException("Unsupported component reference type: " + type.FullName);
    }
}
