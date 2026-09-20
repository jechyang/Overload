using System;
using System.Collections.Generic;
using Overload.Maths;

namespace Overload.EventSystems;

public abstract class PointerInputModule : BaseInputModule
{
    protected readonly Dictionary<int, PointerEventData> pointers = new();
    private readonly List<RaycastResult> hits = new();
    private bool canceling;
    private bool CanProcess => IsActive() && eventSystem.IsActive();
    public const int kMouseLeftId = -1, kMouseRightId = -2, kMouseMiddleId = -3;
    protected PointerEventData GetPointerData(int id) {
        if (!pointers.TryGetValue(id, out var data)) pointers.Add(id, data = new(eventSystem) { pointerId = id });
        return data;
    }
    public override bool IsPointerOverObject(int pointerId) => pointers.TryGetValue(pointerId, out var data) && ExecuteEvents.Live(data.pointerEnter);
    private static List<Actor> Chain(Actor? actor) {
        var result = new List<Actor>();
        for (var current = actor; ExecuteEvents.Live(current); current = current!.Parent) result.Add(current!);
        return result;
    }
    protected void HandlePointerExitAndEnter(PointerEventData data, Actor? target) {
        if (ExecuteEvents.Same(data.pointerEnter, target) && ExecuteEvents.Live(target)) return;
        var previous = data.hovered.ToArray(); var next = Chain(target);
        Actor? common = null;
        foreach (var old in previous) { foreach (var item in next) if (ExecuteEvents.Same(old,item)) { common = old; break; } if (common != null) break; }
        data.pointerEnter = target; data.hovered.Clear(); data.hovered.AddRange(next);
        foreach (var old in previous) { if (ExecuteEvents.Same(old,common)) break; ExecuteEvents.Execute(old,data,ExecuteEvents.pointerExitHandler); }
        foreach (var item in next) { if (ExecuteEvents.Same(item,common)) break; ExecuteEvents.Execute(item,data,ExecuteEvents.pointerEnterHandler); if (!CanProcess) break; }
        if (!ExecuteEvents.Live(data.pointerEnter)) { data.pointerEnter = null; data.hovered.Clear(); }
    }
    protected void UpdatePointer(PointerEventData data, Vector2 position) {
        data.Reset(); data.delta = position-data.position; data.position = position;
        eventSystem.RaycastAll(data,hits); data.pointerCurrentRaycast = hits.Count == 0 ? default : hits[0];
        if (data.pointerId >= -1) HandlePointerExitAndEnter(data,data.pointerCurrentRaycast.actor);
        if (data.pointerPress != null && !ExecuteEvents.CanHandleEvent<IPointerUpHandler>(data.pointerPress) &&
            !ExecuteEvents.CanHandleEvent<IPointerClickHandler>(data.pointerPress)) { data.pointerPress = null; data.eligibleForClick = false; }
        if (data.pointerDrag != null && !ExecuteEvents.CanHandleEvent<IDragHandler>(data.pointerDrag)) { data.pointerDrag = null; data.dragging = false; }
    }
    protected void ProcessPress(PointerEventData data, double time) {
        var target = data.pointerCurrentRaycast.actor;
        data.eligibleForClick = true; data.dragging = false; data.useDragThreshold = true;
        data.pressPosition = data.position; data.pointerPressRaycast = data.pointerCurrentRaycast;
        var select = ExecuteEvents.GetEventHandler<ISelectHandler>(target);
        if (!ExecuteEvents.Same(select,eventSystem.currentSelected)) eventSystem.SetSelectedObject(null,data);
        var pressed = ExecuteEvents.ExecuteHierarchy(target,data,ExecuteEvents.pointerDownHandler) ?? ExecuteEvents.GetEventHandler<IPointerClickHandler>(target);
        if (!CanProcess) { data.eligibleForClick = false; return; }
        if (!ExecuteEvents.Live(pressed)) data.eligibleForClick = false;
        data.clickCount = ExecuteEvents.Same(pressed,data.lastPress) && time-data.clickTime < 0.3 ? data.clickCount+1 : 1;
        data.clickTime = time; data.pointerPress = pressed; data.rawPointerPress = target;
        data.pointerDrag = ExecuteEvents.GetEventHandler<IDragHandler>(target);
        ExecuteEvents.Execute(data.pointerDrag,data,ExecuteEvents.initializePotentialDragHandler);
    }
    protected void ProcessDrag(PointerEventData data) {
        if (!data.IsPointerMoving() || !ExecuteEvents.Live(data.pointerDrag)) return;
        var offset = data.position-data.pressPosition;
        float threshold = Math.Max(0,eventSystem.pixelDragThreshold);
        if (!data.dragging && (!data.useDragThreshold || offset.x*offset.x+offset.y*offset.y >= threshold*threshold)) {
            ExecuteEvents.Execute(data.pointerDrag,data,ExecuteEvents.beginDragHandler);
            data.dragging = CanProcess && ExecuteEvents.CanHandleEvent<IDragHandler>(data.pointerDrag);
        }
        if (!data.dragging) return;
        if (!ExecuteEvents.Same(data.pointerPress,data.pointerDrag)) {
            var pressed = data.pointerPress; data.pointerPress = null; data.rawPointerPress = null; data.eligibleForClick = false;
            ExecuteEvents.Execute(pressed,data,ExecuteEvents.pointerUpHandler);
        }
        ExecuteEvents.Execute(data.pointerDrag,data,ExecuteEvents.dragHandler);
    }
    protected void ProcessRelease(PointerEventData data, bool canceled = false) {
        var pressed = data.pointerPress; var dragged = data.pointerDrag;
        bool wasDragging = data.dragging, click = data.eligibleForClick;
        // Revoke capture before callbacks: disabling/destroying the module is reentrant.
        data.pointerPress = null; data.rawPointerPress = null; data.pointerDrag = null;
        data.dragging = false; data.eligibleForClick = false; data.lastPress = pressed;
        ExecuteEvents.Execute(pressed,data,ExecuteEvents.pointerUpHandler);
        var target = data.pointerCurrentRaycast.actor;
        if (!canceled && CanProcess) {
            if (click && ExecuteEvents.Same(pressed,ExecuteEvents.GetEventHandler<IPointerClickHandler>(target))) ExecuteEvents.Execute(pressed,data,ExecuteEvents.pointerClickHandler);
            else if (wasDragging) ExecuteEvents.ExecuteHierarchy(target,data,ExecuteEvents.dropHandler);
        }
        if (wasDragging) ExecuteEvents.Execute(dragged,data,ExecuteEvents.endDragHandler);
    }
    public override void CancelInput() {
        if (canceling) return;
        canceling = true;
        try {
            foreach (var data in new List<PointerEventData>(pointers.Values)) { ProcessRelease(data,true); HandlePointerExitAndEnter(data,null); }
            pointers.Clear();
        } finally { canceling = false; }
    }
    protected void ProcessPointerEvents(UIInputSnapshot frame, bool touchOnly) {
        bool hasTouch = false; foreach (var e in frame.events) if (e.kind == UIInputKind.Touch) { hasTouch = true; break; }
        foreach (var e in frame.events) {
            if (!CanProcess) break;
            if (e.kind == UIInputKind.Focus && e.id == 0) { CancelInput(); continue; }
            if (e.kind == UIInputKind.Touch) {
                var data = GetPointerData(e.id); data.button = PointerEventData.InputButton.Left;
                UpdatePointer(data,e.position);
                if (e.phase == TouchPhase.Began) { data.delta = default; ProcessPress(data,frame.time); }
                else if (e.phase is TouchPhase.Ended or TouchPhase.Canceled) { ProcessRelease(data,e.phase == TouchPhase.Canceled); HandlePointerExitAndEnter(data,null); pointers.Remove(e.id); }
                else ProcessDrag(data);
            } else if (!touchOnly && !hasTouch) {
                if (e.kind == UIInputKind.PointerMove) {
                    for (int button = 0; button < 3; ++button) { var data = GetPointerData(-1-button); UpdatePointer(data,e.position); ProcessDrag(data); }
                } else if (e.kind is UIInputKind.PointerDown or UIInputKind.PointerUp && e.id >= 0 && e.id < 3) {
                    var data = GetPointerData(-1-e.id); data.button = (PointerEventData.InputButton)e.id; UpdatePointer(data,e.position);
                    if (e.kind == UIInputKind.PointerDown) ProcessPress(data,frame.time); else ProcessRelease(data);
                } else if (e.kind == UIInputKind.Scroll) {
                    var data = GetPointerData(kMouseLeftId); UpdatePointer(data,frame.mousePosition); data.scrollDelta = e.position;
                    ExecuteEvents.ExecuteHierarchy(data.pointerCurrentRaycast.actor,data,ExecuteEvents.scrollHandler);
                }
            }
        }
        // Refresh hover when geometry changes with a stationary mouse.
        if (!touchOnly && !hasTouch && CanProcess) UpdatePointer(GetPointerData(kMouseLeftId),frame.mousePosition);
    }
}

public class StandaloneInputModule : PointerInputModule
{
    [SerializeField] private float m_InputActionsPerSecond = 10, m_RepeatDelay = 0.5f;
    [SerializeField] private string m_HorizontalAxis = "Horizontal", m_VerticalAxis = "Vertical", m_SubmitButton = "Submit", m_CancelButton = "Cancel";
    public string horizontalAxis { get => m_HorizontalAxis; set => m_HorizontalAxis = value; }
    public string verticalAxis { get => m_VerticalAxis; set => m_VerticalAxis = value; }
    public string submitButton { get => m_SubmitButton; set => m_SubmitButton = value; }
    public string cancelButton { get => m_CancelButton; set => m_CancelButton = value; }
    public float inputActionsPerSecond { get => m_InputActionsPerSecond; set { if (!float.IsFinite(value) || value <= 0) throw new ArgumentOutOfRangeException(nameof(value)); m_InputActionsPerSecond = value; } }
    public float repeatDelay { get => m_RepeatDelay; set { if (!float.IsFinite(value) || value < 0) throw new ArgumentOutOfRangeException(nameof(value)); m_RepeatDelay = value; } }
    private MoveDirection previousMove = MoveDirection.None;
    private double nextMove;
    public override void Process() {
        var frame = input.snapshot;
        var selectedData = new BaseEventData(eventSystem);
        ExecuteEvents.Execute(eventSystem.currentSelected,selectedData,ExecuteEvents.updateSelectedHandler);
        if (eventSystem.sendNavigationEvents && !selectedData.used) {
            float x = input.GetAxisRaw(horizontalAxis), y = input.GetAxisRaw(verticalAxis);
            MoveDirection direction = x != 0 ? (x > 0 ? MoveDirection.Right : MoveDirection.Left) : y != 0 ? (y > 0 ? MoveDirection.Up : MoveDirection.Down) : MoveDirection.None;
            bool newDirection = direction != previousMove;
            if (direction != MoveDirection.None && (newDirection || frame.time >= nextMove)) {
                var axis = new AxisEventData(eventSystem) { moveDir = direction, moveVector = new(x,y) };
                ExecuteEvents.Execute(eventSystem.currentSelected,axis,ExecuteEvents.moveHandler);
                nextMove = frame.time + (newDirection ? Math.Max(0,repeatDelay) : 1/Math.Max(0.01f,inputActionsPerSecond));
                if (axis.used) selectedData.Use();
            }
            previousMove = direction;
            if (!selectedData.used) {
                if (input.GetButtonDown(submitButton)) ExecuteEvents.Execute(eventSystem.currentSelected,selectedData,ExecuteEvents.submitHandler);
                if (!selectedData.used && input.GetButtonDown(cancelButton)) ExecuteEvents.Execute(eventSystem.currentSelected,selectedData,ExecuteEvents.cancelHandler);
            }
        }
        ProcessPointerEvents(frame,false);
    }
    public override void CancelInput() { base.CancelInput(); previousMove = MoveDirection.None; nextMove = 0; }
}

public class TouchInputModule : PointerInputModule
{
    public bool forceModuleActive { get; set; }
    public override bool ShouldActivateModule() {
        if (!IsActive()) return false;
        if (forceModuleActive || pointers.Count > 0) return true;
        foreach (var e in input.snapshot.events) if (e.kind == UIInputKind.Touch) return true;
        return false;
    }
    public override void Process() {
        ExecuteEvents.Execute(eventSystem.currentSelected,new BaseEventData(eventSystem),ExecuteEvents.updateSelectedHandler);
        ProcessPointerEvents(input.snapshot,true);
    }
}
