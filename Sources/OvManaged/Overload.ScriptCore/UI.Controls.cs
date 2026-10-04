using System;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public class Button : Selectable, IPointerClickHandler, ISubmitHandler
{
    private readonly UIEvent m_OnClick;
    public Button() { m_OnClick = new(this); }

    public UIEvent onClick => m_OnClick;

    public virtual void OnPointerClick(PointerEventData eventData) {
        if (eventData.button != PointerEventData.InputButton.Left) return;
        if (!IsInteractable()) return;
        m_OnClick.Invoke();
    }
    public virtual void OnSubmit(BaseEventData eventData) {
        if (!IsInteractable()) return;
        m_OnClick.Invoke();
        DoStateTransition(SelectionState.Pressed, true);
        EvaluateAndTransitionToSelectionState(true);
    }
}

public class ToggleGroup : UIBehaviour
{
    [SerializeField] private bool m_AllowSwitchOff;
    private readonly System.Collections.Generic.List<Toggle> m_Toggles = new();

    public bool allowSwitchOff {
        get => m_AllowSwitchOff;
        set => m_AllowSwitchOff = value;
    }
    internal void Register(Toggle toggle) {
        if (!m_Toggles.Contains(toggle)) m_Toggles.Add(toggle);
    }
    internal void Unregister(Toggle toggle) => m_Toggles.Remove(toggle);
    internal void NotifyToggleOn(Toggle toggle, bool sendCallback = true) {
        foreach (var other in m_Toggles.ToArray()) {
            if (other == toggle || !other.isOn) continue;
            other.SetIsOnWithoutNotify(false);
            if (sendCallback) other.onValueChanged.Invoke(false);
        }
    }
    internal bool HasOtherOn(Toggle current) { foreach (var toggle in m_Toggles) if (toggle != current && toggle.isOn && toggle.IsActive()) return true; return false; }
    internal bool AnyTogglesOn() {
        foreach (var toggle in m_Toggles) if (toggle.isOn) return true;
        return false;
    }
    public void SetAllTogglesOff() {
        bool previous=m_AllowSwitchOff;m_AllowSwitchOff=true;
        try { foreach (var toggle in m_Toggles.ToArray()) toggle.isOn=false; } finally { m_AllowSwitchOff=previous; }
    }
}

public class Toggle : Selectable, IPointerClickHandler, ISubmitHandler
{
    [SerializeField] private bool m_IsOn;
    [SerializeField, FieldId("m_Group")] private ComponentRef<ToggleGroup> m_GroupRef;
    private ToggleGroup? m_Group { get => ResolveUIReference(m_GroupRef); set => m_GroupRef = value == null ? default : ComponentRef<ToggleGroup>.From(Owner, value); }
    private readonly UIEvent<bool> m_OnValueChanged;
    public Toggle() { m_OnValueChanged = new(this); }
    [SerializeField] private ComponentRef<Graphic> m_Graphic;
    public Graphic? graphic { get => ResolveUIReference(m_Graphic); set { m_Graphic=value==null?default:ComponentRef<Graphic>.From(Owner,value);UpdateCheckmark(); } }
    private void UpdateCheckmark() { if (graphic is Graphic image) image.canvasRenderer.SetAlpha(m_IsOn?1:0); }

    public UIEvent<bool> onValueChanged => m_OnValueChanged;
    public bool isOn {
        get => m_IsOn;
        set => Set(value, true);
    }
    public ToggleGroup? group {
        get => m_Group;
        set {
            m_Group?.Unregister(this);
            m_Group = value;
            m_Group?.Register(this);
            if (m_Group != null && m_IsOn) m_Group.NotifyToggleOn(this);
        }
    }
    public void SetIsOnWithoutNotify(bool value) => Set(value, false);
    private void Set(bool value, bool sendCallback) {
        if (!value && m_Group != null && !m_Group.allowSwitchOff && !m_Group.HasOtherOn(this)) value=true;
        if (m_IsOn == value) { UpdateCheckmark(); return; }
        m_IsOn = value;
        UpdateCheckmark();
        if (m_Group != null && m_IsOn) m_Group.NotifyToggleOn(this, sendCallback);
        if (sendCallback) m_OnValueChanged.Invoke(m_IsOn);
        DoStateTransition(currentSelectionState, true);
    }
    public override void OnEnable() {
        base.OnEnable();
        m_Group?.Register(this);
        UpdateCheckmark();
        if (m_IsOn && m_Group != null) m_Group.NotifyToggleOn(this, false);
    }
    public override void OnDisable() {
        m_Group?.Unregister(this);
        base.OnDisable();
    }
    public virtual void OnPointerClick(PointerEventData eventData) {
        if (eventData.button != PointerEventData.InputButton.Left) return;
        if (!IsInteractable()) return;
        ToggleNow();
    }
    public virtual void OnSubmit(BaseEventData eventData) {
        if (!IsInteractable()) return;
        ToggleNow();
    }
    private void ToggleNow() {
        if (m_Group != null && !m_Group.allowSwitchOff && m_IsOn) return;
        isOn = !m_IsOn;
    }
    protected override void SetDirty() { base.SetDirty(); if (owner != null) UpdateCheckmark(); }
}

public class Slider : Selectable, IEndDragHandler, IDragHandler, IInitializePotentialDragHandler, IPointerDownHandler
{
    public enum Direction { LeftToRight, RightToLeft, BottomToTop, TopToBottom }

    [SerializeField, FieldId("m_FillRect")] private ComponentRef<RectTransform> m_FillRectRef;
    private RectTransform? m_FillRect { get => ResolveUIReference(m_FillRectRef); set => m_FillRectRef = value == null ? default : ComponentRef<RectTransform>.From(Owner, value); }
    [SerializeField, FieldId("m_HandleRect")] private ComponentRef<RectTransform> m_HandleRectRef;
    private RectTransform? m_HandleRect { get => ResolveUIReference(m_HandleRectRef); set => m_HandleRectRef = value == null ? default : ComponentRef<RectTransform>.From(Owner, value); }
    [SerializeField] private Direction m_Direction = Direction.LeftToRight;
    [SerializeField] private float m_MinValue, m_MaxValue = 1, m_Value = 1;
    [SerializeField] private bool m_WholeNumbers;
    private readonly UIEvent<float> m_OnValueChanged;
    public Slider() { m_OnValueChanged=new(this); }
    private bool m_Dragging;

    public bool isDragging => m_Dragging;
    public UIEvent<float> onValueChanged => m_OnValueChanged;
    public RectTransform? fillRect { get => m_FillRect; set { m_FillRect = value; UpdateVisuals(); } }
    public RectTransform? handleRect { get => m_HandleRect; set { m_HandleRect = value; UpdateVisuals(); } }
    public Direction direction {
        get => m_Direction;
        set {
            if (m_Direction == value) return;
            m_Direction = value;
            UpdateVisuals();
        }
    }
    public float minValue {
        get => m_MinValue;
        set { if (m_MinValue == value) return; m_MinValue = value; Set(m_Value); UpdateVisuals(); }
    }
    public float maxValue {
        get => m_MaxValue;
        set { if (m_MaxValue == value) return; m_MaxValue = value; Set(m_Value); UpdateVisuals(); }
    }
    public bool wholeNumbers {
        get => m_WholeNumbers;
        set { if (m_WholeNumbers == value) return; m_WholeNumbers = value; Set(m_Value); UpdateVisuals(); }
    }
    public float value {
        get => m_WholeNumbers ? MathF.Round(m_Value) : m_Value;
        set => Set(value);
    }
    public float normalizedValue {
        get {
            if (MathF.Abs(m_MaxValue - m_MinValue) < 1e-6f) return 0;
            return Math.Clamp((value - m_MinValue) / (m_MaxValue - m_MinValue), 0, 1);
        }
        set => this.value = m_MinValue + (m_MaxValue - m_MinValue) * Math.Clamp(value, 0, 1);
    }
    public void SetValueWithoutNotify(float input) => Set(input, false);
    private void Set(float input, bool sendCallback = true) {
        float clamped = m_WholeNumbers ? MathF.Round(input) : input;
        clamped = Math.Clamp(CanvasScaler.Finite(clamped), m_MinValue, Math.Max(m_MinValue, m_MaxValue));
        if (m_Value == clamped) return;
        m_Value = clamped;
        UpdateVisuals();
        if (sendCallback) m_OnValueChanged.Invoke(value);
    }
    public override void OnEnable() {
        base.OnEnable();
        UpdateVisuals();
    }
    public void OnInitializePotentialDrag(PointerEventData eventData) { }
    protected override void SetDirty() { base.SetDirty(); if (owner != null) { Set(m_Value, false); UpdateVisuals(); } }
    public override void OnPointerDown(PointerEventData eventData) {
        base.OnPointerDown(eventData);
        if (!IsInteractable() || eventData.button != PointerEventData.InputButton.Left) return;
        m_Dragging = true;
        UpdateDrag(eventData);
    }
    public void OnDrag(PointerEventData eventData) {
        if (!IsInteractable()) return;
        UpdateDrag(eventData);
    }
    public override void OnPointerUp(PointerEventData e) { base.OnPointerUp(e); m_Dragging=false; }
    public void OnEndDrag(PointerEventData e) { m_Dragging=false; }
    public override void OnDisable() { m_Dragging=false; base.OnDisable(); }
    public override void OnMove(AxisEventData e) {
        bool horizontal=m_Direction is Direction.LeftToRight or Direction.RightToLeft;
        if(IsInteractable() && (horizontal && e.moveDir is MoveDirection.Left or MoveDirection.Right || !horizontal && e.moveDir is MoveDirection.Up or MoveDirection.Down)) {
            float step=wholeNumbers?1:(maxValue-minValue)*0.1f;
            bool increase=e.moveDir is MoveDirection.Right or MoveDirection.Up;
            if(m_Direction is Direction.RightToLeft or Direction.TopToBottom)increase=!increase;
            value+=increase?step:-step;e.Use();
        } else base.OnMove(e);
    }
    private void UpdateDrag(PointerEventData eventData) {
        if(eventData.button!=PointerEventData.InputButton.Left)return;
        var area=m_HandleRect?.parent??m_FillRect?.parent??rectTransform;
        if(!RectTransformUtility.ScreenPointToLocalPointInRectangle(area,eventData.position,null,out var local))return;
        var rect=area.rect;
        int axis=m_Direction is Direction.LeftToRight or Direction.RightToLeft?0:1;
        float t=Math.Clamp((local[axis]-rect.min[axis])/Math.Max(0.0001f,rect.size[axis]),0,1);
        if(m_Direction is Direction.RightToLeft or Direction.TopToBottom)t=1-t;
        normalizedValue=t;
    }
    private void UpdateVisuals() {
        float n = normalizedValue;
        if (m_FillRect != null) {
            var fill = m_FillRect;
            bool horizontal = m_Direction is Direction.LeftToRight or Direction.RightToLeft;
            fill.anchorMin = new Vector2(0, 0);
            fill.anchorMax = horizontal ? new Vector2(n, 1) : new Vector2(1, n);
            if (m_Direction == Direction.RightToLeft) { fill.anchorMin=new Vector2(1-n,0);fill.anchorMax=Vector2.one; }
            if (m_Direction == Direction.TopToBottom) { fill.anchorMin=new Vector2(0,1-n);fill.anchorMax=Vector2.one; }
            fill.offsetMin = Vector2.zero;
            fill.offsetMax = Vector2.zero;
        }
        if (m_HandleRect != null) {
            var handle = m_HandleRect;
            bool horizontal = m_Direction is Direction.LeftToRight or Direction.RightToLeft;
            if (m_Direction == Direction.RightToLeft) n = 1f - n;
            if (m_Direction == Direction.TopToBottom) n = 1f - n;
            handle.anchorMin = horizontal ? new Vector2(n, 0.5f) : new Vector2(0.5f, n);
            handle.anchorMax = handle.anchorMin;
        }
    }
}

public class Scrollbar : Selectable, IBeginDragHandler, IEndDragHandler, IDragHandler, IInitializePotentialDragHandler, IPointerDownHandler
{
    public enum Direction { LeftToRight, RightToLeft, BottomToTop, TopToBottom }
    [SerializeField, FieldId("m_HandleRect")] private ComponentRef<RectTransform> m_HandleRectRef;
    private RectTransform? m_HandleRect { get => ResolveUIReference(m_HandleRectRef); set => m_HandleRectRef = value == null ? default : ComponentRef<RectTransform>.From(Owner, value); }
    [SerializeField] private Direction m_Direction = Direction.LeftToRight;
    [SerializeField] private float m_Value, m_Size = 0.2f;
    [SerializeField] private int m_NumberOfSteps;
    private readonly UIEvent<float> m_OnValueChanged;
    public Scrollbar() { m_OnValueChanged=new(this); }

    private float dragOffset;
    private bool dragging, paging;
    private Vector2 heldPoint;
    private float pageDelay;
    public Direction direction { get=>m_Direction;set{m_Direction=value;UpdateVisuals();} }
    public UIEvent<float> onValueChanged => m_OnValueChanged;
    public RectTransform? handleRect { get => m_HandleRect; set { m_HandleRect = value; UpdateVisuals(); } }
    public float value {
        get => m_Value;
        set {
            float v = Quantize(value);
            if (m_Value == v) return;
            m_Value = v;
            UpdateVisuals();
            m_OnValueChanged.Invoke(m_Value);
        }
    }
    public float size {
        get => m_Size;
        set { m_Size = Math.Clamp(value, 0, 1); UpdateVisuals(); }
    }
    public int numberOfSteps {
        get => m_NumberOfSteps;
        set { m_NumberOfSteps = Math.Max(0, value); this.value=m_Value;UpdateVisuals(); }
    }
    public void SetValueWithoutNotify(float value) {
        m_Value = Quantize(value);
        UpdateVisuals();
    }
    public override void OnEnable() { base.OnEnable(); UpdateVisuals(); }
    protected override void SetDirty() { base.SetDirty(); if (owner != null) { m_Size=Math.Clamp(CanvasScaler.Finite(m_Size),0,1);SetValueWithoutNotify(m_Value); } }
    public void OnInitializePotentialDrag(PointerEventData eventData) { }
    private float Quantize(float value) {
        value=Math.Clamp(CanvasScaler.Finite(value),0,1);
        return m_NumberOfSteps>1?MathF.Round(value*(m_NumberOfSteps-1))/(m_NumberOfSteps-1):value;
    }
    private int Axis=>m_Direction is Direction.LeftToRight or Direction.RightToLeft?0:1;
    private bool Reverse=>m_Direction is Direction.RightToLeft or Direction.TopToBottom;
    public override void OnPointerDown(PointerEventData e) {
        if(e.button!=PointerEventData.InputButton.Left||!IsInteractable())return;
        base.OnPointerDown(e);heldPoint=e.position;pageDelay=0.4f;
        paging=m_HandleRect==null||!RectTransformUtility.RectangleContainsScreenPoint(m_HandleRect,e.position);
        if(paging)Page(e.position);
    }
    public override void OnPointerUp(PointerEventData e){base.OnPointerUp(e);paging=false;dragging=false;}
    public void OnBeginDrag(PointerEventData e){
        if(e.button!=PointerEventData.InputButton.Left||!IsInteractable())return;
        var area=m_HandleRect?.parent??rectTransform;
        if(!RectTransformUtility.ScreenPointToLocalPointInRectangle(area,e.position,null,out var p))return;
        float extent=area.rect.size[Axis];float start=(Reverse?1-value:value)*(1-size);
        dragOffset=0;
        if(m_HandleRect!=null&&RectTransformUtility.RectangleContainsScreenPoint(m_HandleRect,e.position))dragOffset=(p[Axis]-area.rect.min[Axis])/Math.Max(extent,0.0001f)-(start+size*0.5f);
        dragging=true;paging=false;
    }
    public void OnEndDrag(PointerEventData e){dragging=false;paging=false;}
    public void OnDrag(PointerEventData e){if(dragging&&IsInteractable())UpdateFromPoint(e.position);}
    private void UpdateFromPoint(Vector2 screen){
        var area=m_HandleRect?.parent??rectTransform;
        if(!RectTransformUtility.ScreenPointToLocalPointInRectangle(area,screen,null,out var p))return;
        float t=((p[Axis]-area.rect.min[Axis])/Math.Max(0.0001f,area.rect.size[Axis])-dragOffset-size*0.5f)/Math.Max(0.0001f,1-size);
        value=Reverse?1-t:t;
    }
    private void Page(Vector2 screen){
        var area=m_HandleRect?.parent??rectTransform;
        if(!RectTransformUtility.ScreenPointToLocalPointInRectangle(area,screen,null,out var p))return;
        float t=(p[Axis]-area.rect.min[Axis])/Math.Max(0.0001f,area.rect.size[Axis]);
        float start=(Reverse?1-value:value)*(1-size);float step=t<start?-size:t>start+size?size:0;
        value+=Reverse?-step:step;
    }
    public override void OnUpdate(float dt){base.OnUpdate(dt);if(paging&&IsInteractable()){pageDelay-=dt;if(pageDelay<=0){Page(heldPoint);pageDelay=0.1f;}}}
    public override void OnDisable(){paging=dragging=false;base.OnDisable();}
    public override void OnMove(AxisEventData e){
        bool matching=Axis==0?e.moveDir is MoveDirection.Left or MoveDirection.Right:e.moveDir is MoveDirection.Up or MoveDirection.Down;
        if(IsInteractable()&&matching){float step=m_NumberOfSteps>1?1f/(m_NumberOfSteps-1):0.1f;bool increase=e.moveDir is MoveDirection.Right or MoveDirection.Up;value+=(increase!=Reverse)?step:-step;e.Use();}else base.OnMove(e);
    }
    private void UpdateVisuals() {
        if (m_HandleRect == null) return;
        bool horizontal = m_Direction is Direction.LeftToRight or Direction.RightToLeft;
        float start = m_Value * (1f - m_Size);
        if (m_Direction is Direction.RightToLeft or Direction.TopToBottom) start = 1f - m_Size - start;
        m_HandleRect.anchorMin = horizontal ? new Vector2(start, 0) : new Vector2(0, start);
        m_HandleRect.anchorMax = horizontal ? new Vector2(start + m_Size, 1) : new Vector2(1, start + m_Size);
        m_HandleRect.offsetMin = Vector2.zero;
        m_HandleRect.offsetMax = Vector2.zero;
    }
}
