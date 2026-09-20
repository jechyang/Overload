using System;
using System.Collections.Generic;
using Overload.UI;

namespace Overload.EventSystems;

public interface IEventSystemHandler { }
public interface IPointerEnterHandler : IEventSystemHandler { void OnPointerEnter(PointerEventData eventData); }
public interface IPointerExitHandler : IEventSystemHandler { void OnPointerExit(PointerEventData eventData); }
public interface IPointerDownHandler : IEventSystemHandler { void OnPointerDown(PointerEventData eventData); }
public interface IPointerUpHandler : IEventSystemHandler { void OnPointerUp(PointerEventData eventData); }
public interface IPointerClickHandler : IEventSystemHandler { void OnPointerClick(PointerEventData eventData); }
public interface IInitializePotentialDragHandler : IEventSystemHandler { void OnInitializePotentialDrag(PointerEventData eventData); }
public interface IBeginDragHandler : IEventSystemHandler { void OnBeginDrag(PointerEventData eventData); }
public interface IDragHandler : IEventSystemHandler { void OnDrag(PointerEventData eventData); }
public interface IEndDragHandler : IEventSystemHandler { void OnEndDrag(PointerEventData eventData); }
public interface IDropHandler : IEventSystemHandler { void OnDrop(PointerEventData eventData); }
public interface IScrollHandler : IEventSystemHandler { void OnScroll(PointerEventData eventData); }
public interface IMoveHandler : IEventSystemHandler { void OnMove(AxisEventData eventData); }
public interface ISubmitHandler : IEventSystemHandler { void OnSubmit(BaseEventData eventData); }
public interface ICancelHandler : IEventSystemHandler { void OnCancel(BaseEventData eventData); }
public interface ISelectHandler : IEventSystemHandler { void OnSelect(BaseEventData eventData); }
public interface IDeselectHandler : IEventSystemHandler { void OnDeselect(BaseEventData eventData); }
public interface IUpdateSelectedHandler : IEventSystemHandler { void OnUpdateSelected(BaseEventData eventData); }
public enum EventHandle { Unused, Used }
public enum EventTriggerType { PointerEnter, PointerExit, PointerDown, PointerUp, PointerClick, Drag, Drop, Scroll, UpdateSelected, Select, Deselect, Move, InitializePotentialDrag, BeginDrag, EndDrag, Submit, Cancel }

public static class ExecuteEvents
{
    public delegate void EventFunction<T>(T handler, BaseEventData data) where T : class, IEventSystemHandler;
    public static T ValidateEventData<T>(BaseEventData data) where T : BaseEventData => data as T ?? throw new ArgumentException("Wrong event data type");
    public static EventFunction<IPointerEnterHandler> pointerEnterHandler { get; } = (h, d) => h.OnPointerEnter(ValidateEventData<PointerEventData>(d));
    public static EventFunction<IPointerExitHandler> pointerExitHandler { get; } = (h, d) => h.OnPointerExit(ValidateEventData<PointerEventData>(d));
    public static EventFunction<IPointerDownHandler> pointerDownHandler { get; } = (h, d) => h.OnPointerDown(ValidateEventData<PointerEventData>(d));
    public static EventFunction<IPointerUpHandler> pointerUpHandler { get; } = (h, d) => h.OnPointerUp(ValidateEventData<PointerEventData>(d));
    public static EventFunction<IPointerClickHandler> pointerClickHandler { get; } = (h, d) => h.OnPointerClick(ValidateEventData<PointerEventData>(d));
    public static EventFunction<IInitializePotentialDragHandler> initializePotentialDragHandler { get; } = (h, d) => h.OnInitializePotentialDrag(ValidateEventData<PointerEventData>(d));
    public static EventFunction<IBeginDragHandler> beginDragHandler { get; } = (h, d) => h.OnBeginDrag(ValidateEventData<PointerEventData>(d));
    public static EventFunction<IDragHandler> dragHandler { get; } = (h, d) => h.OnDrag(ValidateEventData<PointerEventData>(d));
    public static EventFunction<IEndDragHandler> endDragHandler { get; } = (h, d) => h.OnEndDrag(ValidateEventData<PointerEventData>(d));
    public static EventFunction<IDropHandler> dropHandler { get; } = (h, d) => h.OnDrop(ValidateEventData<PointerEventData>(d));
    public static EventFunction<IScrollHandler> scrollHandler { get; } = (h, d) => h.OnScroll(ValidateEventData<PointerEventData>(d));
    public static EventFunction<IMoveHandler> moveHandler { get; } = (h, d) => h.OnMove(ValidateEventData<AxisEventData>(d));
    public static EventFunction<ISubmitHandler> submitHandler { get; } = (h, d) => h.OnSubmit(ValidateEventData<BaseEventData>(d));
    public static EventFunction<ICancelHandler> cancelHandler { get; } = (h, d) => h.OnCancel(ValidateEventData<BaseEventData>(d));
    public static EventFunction<ISelectHandler> selectHandler { get; } = (h, d) => h.OnSelect(ValidateEventData<BaseEventData>(d));
    public static EventFunction<IDeselectHandler> deselectHandler { get; } = (h, d) => h.OnDeselect(ValidateEventData<BaseEventData>(d));
    public static EventFunction<IUpdateSelectedHandler> updateSelectedHandler { get; } = (h, d) => h.OnUpdateSelected(ValidateEventData<BaseEventData>(d));
    internal static bool Same(Actor? a, Actor? b) => ReferenceEquals(a,b) || a != null && b != null && ReferenceEquals(a.Access.UIRegistry,b.Access.UIRegistry) && a.Handle == b.Handle;
    internal static bool Live(Actor? actor) {
        if (actor == null) return false;
        return actor.IsAlive && actor.ActiveInHierarchy;
    }
    internal static bool Active(object value) => value is UIBehaviour ui ? ui.IsActive() : value is Behaviour b && Live(b.owner);
    public static bool Execute<T>(Actor? target, BaseEventData data, EventFunction<T> functor) where T : class, IEventSystemHandler {
        ArgumentNullException.ThrowIfNull(data); ArgumentNullException.ThrowIfNull(functor);
        if (!Live(target)) return false;
        if (!ReferenceEquals(target!.Access.UIRegistry, data.eventSystem.Owner.Access.UIRegistry)) throw new InvalidOperationException("Cross-session event target");
        bool handled = false;
        foreach (T handler in target.GetComponents<T>()) {
            if (!Live(target)) break;
            // A previous callback can remove/disable this handler.
            bool present = false; foreach (T current in target.GetComponents<T>()) if (ReferenceEquals(current,handler)) { present = true; break; }
            if (!present || !Active(handler!)) continue;
            handled = true; functor(handler, data);
        }
        return handled;
    }
    public static bool CanHandleEvent<T>(Actor? actor) where T : class, IEventSystemHandler {
        if (!Live(actor)) return false;
        foreach (var h in actor!.GetComponents<T>()) if (Active(h!)) return true;
        return false;
    }
    public static Actor? GetEventHandler<T>(Actor? actor) where T : class, IEventSystemHandler {
        for (var a = actor; Live(a); a = a!.Parent) if (CanHandleEvent<T>(a)) return a;
        return null;
    }
    public static Actor? ExecuteHierarchy<T>(Actor? root, BaseEventData data, EventFunction<T> functor) where T : class, IEventSystemHandler {
        for (var actor = root; Live(actor);) {
            var parent = actor!.Parent;
            if (Execute(actor, data, functor)) return actor;
            actor = parent;
        }
        return null;
    }
}

public class EventTrigger : UIBehaviour, IPointerEnterHandler, IPointerExitHandler, IPointerDownHandler, IPointerUpHandler, IPointerClickHandler, IInitializePotentialDragHandler, IBeginDragHandler, IDragHandler, IEndDragHandler, IDropHandler, IScrollHandler, IMoveHandler, ISubmitHandler, ICancelHandler, ISelectHandler, IDeselectHandler, IUpdateSelectedHandler
{
    public sealed class TriggerEvent : UIEvent<BaseEventData> { public TriggerEvent(Behaviour? owner = null) : base(owner) { } }
    public sealed class Entry { public EventTriggerType eventID; public TriggerEvent callback = new(); }
    public List<Entry> triggers { get; } = new();
    protected virtual void Execute(EventTriggerType type, BaseEventData data) {
        foreach (var entry in triggers.ToArray()) { if (!IsActive()) break; if (entry.eventID == type && triggers.Contains(entry)) entry.callback.Invoke(data); }
    }
    public virtual void OnPointerEnter(PointerEventData data) => Execute(EventTriggerType.PointerEnter, data);
    public virtual void OnPointerExit(PointerEventData data) => Execute(EventTriggerType.PointerExit, data);
    public virtual void OnPointerDown(PointerEventData data) => Execute(EventTriggerType.PointerDown, data);
    public virtual void OnPointerUp(PointerEventData data) => Execute(EventTriggerType.PointerUp, data);
    public virtual void OnPointerClick(PointerEventData data) => Execute(EventTriggerType.PointerClick, data);
    public virtual void OnInitializePotentialDrag(PointerEventData data) => Execute(EventTriggerType.InitializePotentialDrag, data);
    public virtual void OnBeginDrag(PointerEventData data) => Execute(EventTriggerType.BeginDrag, data);
    public virtual void OnDrag(PointerEventData data) => Execute(EventTriggerType.Drag, data);
    public virtual void OnEndDrag(PointerEventData data) => Execute(EventTriggerType.EndDrag, data);
    public virtual void OnDrop(PointerEventData data) => Execute(EventTriggerType.Drop, data);
    public virtual void OnScroll(PointerEventData data) => Execute(EventTriggerType.Scroll, data);
    public virtual void OnMove(AxisEventData data) => Execute(EventTriggerType.Move, data);
    public virtual void OnSubmit(BaseEventData data) => Execute(EventTriggerType.Submit, data);
    public virtual void OnCancel(BaseEventData data) => Execute(EventTriggerType.Cancel, data);
    public virtual void OnSelect(BaseEventData data) => Execute(EventTriggerType.Select, data);
    public virtual void OnDeselect(BaseEventData data) => Execute(EventTriggerType.Deselect, data);
    public virtual void OnUpdateSelected(BaseEventData data) => Execute(EventTriggerType.UpdateSelected, data);
    public override void OnDestroy() { foreach (var entry in triggers) entry.callback.Dispose(); triggers.Clear(); }
    public override void OnReloadRelease() => OnDestroy();
    protected override void SetDirty() { }
    internal override void EnsureNativeComponents() { }
}
