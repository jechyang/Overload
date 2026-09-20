using System;
using System.Collections.Generic;
using Overload.Maths;

namespace Overload.EventSystems;

public enum MoveDirection { Left, Up, Right, Down, None }
public class BaseEventData(EventSystem eventSystem)
{
    public EventSystem eventSystem { get; } = eventSystem ?? throw new ArgumentNullException(nameof(eventSystem));
    public bool used { get; private set; }
    public BaseInputModule? currentInputModule => eventSystem.currentInputModule;
    public Actor? selectedObject { get => eventSystem.currentSelected; set => eventSystem.SetSelectedObject(value, this); }
    public virtual void Reset() => used = false;
    public virtual void Use() => used = true;
}
public class AxisEventData(EventSystem eventSystem) : BaseEventData(eventSystem)
{
    public Vector2 moveVector { get; set; }
    public MoveDirection moveDir { get; set; } = MoveDirection.None;
    public override void Reset() { base.Reset(); moveVector = default; moveDir = MoveDirection.None; }
}
public struct RaycastResult
{
    public Actor? actor;
    public BaseRaycaster? module;
    public float distance;
    public int index, depth, sortingLayer, sortingOrder;
    public Vector3 worldPosition, worldNormal;
    public Vector2 screenPosition;
    public bool isValid => actor != null && module != null;
    public void Clear() => this = default;
}
public class PointerEventData(EventSystem eventSystem) : BaseEventData(eventSystem)
{
    public enum InputButton { Left, Right, Middle }
    public enum FramePressState { Pressed, Released, PressedAndReleased, NotChanged }
    public Actor? pointerEnter { get; internal set; }
    public Actor? pointerPress { get; set; }
    public Actor? lastPress { get; internal set; }
    public Actor? rawPointerPress { get; set; }
    public Actor? pointerDrag { get; set; }
    public List<Actor> hovered { get; } = new();
    public RaycastResult pointerCurrentRaycast, pointerPressRaycast;
    public int pointerId, clickCount;
    public Vector2 position, delta, pressPosition, scrollDelta;
    public double clickTime;
    public bool eligibleForClick, dragging, useDragThreshold = true;
    public InputButton button;
    public bool IsPointerMoving() => delta.x != 0 || delta.y != 0;
    public bool IsScrolling() => scrollDelta.x != 0 || scrollDelta.y != 0;
}
