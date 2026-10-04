using System;
using System.Collections.Generic;

namespace Overload;

public sealed partial class Actor
{
    /// <summary>Clone a hierarchy with persistent component references remapped; runtime listeners are never copied.</summary>
    public Actor Clone(Actor? parent = null) {
        if(parent!=null && (!ReferenceEquals(Access.UIRegistry,parent.Access.UIRegistry)||Access.Query(Handle,19,parent.Handle)==0))throw new InvalidOperationException("Cross-scene clone parent");
        return new Actor(Access,Access.Query(Handle,29,parent?.Handle??0));
    }
    public int GetSiblingIndex() => checked((int)Access.Query(Handle, 22));
    public void SetSiblingIndex(int index)
    {
        if (index < 0) throw new ArgumentOutOfRangeException(nameof(index));
        Access.Query(Handle, 23, (ulong)index);
        if (Parent?.GetComponent<RectTransform>() is RectTransform parent)
            UI.LayoutRebuilder.MarkLayoutForRebuild(parent);
    }
    public void SetAsFirstSibling() => SetSiblingIndex(0);
    public void SetAsLastSibling() => SetSiblingIndex(int.MaxValue);

    /// <summary>Returns an ordered snapshot, including this Actor before its descendants.</summary>
    public T[] GetComponentsInChildren<T>(bool includeInactive = false) where T : class
    {
        var result = new List<T>(); var pending = new Stack<Actor>(); pending.Push(this);
        while (pending.Count != 0) {
            Actor actor = pending.Pop();
            if (!includeInactive && !actor.ActiveInHierarchy) continue;
            result.AddRange(actor.GetComponents<T>());
            for (int i = actor.ChildCount - 1; i >= 0; --i) pending.Push(actor.GetChild(i));
        }
        return result.ToArray();
    }
    public T[] GetComponentsInParent<T>(bool includeInactive = false) where T : class
    {
        var result = new List<T>();
        for (Actor? actor = this; actor != null; actor = actor.Parent)
            if (includeInactive || actor.ActiveInHierarchy) result.AddRange(actor.GetComponents<T>());
        return result.ToArray();
    }
}
