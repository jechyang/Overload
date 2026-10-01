using System;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public class Button : Selectable, IPointerClickHandler, ISubmitHandler
{
    private readonly UIEvent m_OnClick = new();

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
    internal bool AnyTogglesOn() {
        foreach (var toggle in m_Toggles) if (toggle.isOn) return true;
        return false;
    }
    internal void SetAllTogglesOff() {
        foreach (var toggle in m_Toggles.ToArray()) {
            if (!m_AllowSwitchOff && toggle.isOn && AnyTogglesOn()) continue;
            toggle.SetIsOnWithoutNotify(false);
            toggle.onValueChanged.Invoke(false);
        }
    }
}

public class Toggle : Selectable, IPointerClickHandler, ISubmitHandler
{
    [SerializeField] private bool m_IsOn;
    [SerializeField] private ToggleGroup? m_Group;
    private readonly UIEvent<bool> m_OnValueChanged = new();

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
        if (m_IsOn == value) return;
        m_IsOn = value;
        if (m_Group != null && m_IsOn) m_Group.NotifyToggleOn(this, sendCallback);
        if (sendCallback) m_OnValueChanged.Invoke(m_IsOn);
        DoStateTransition(currentSelectionState, true);
    }
    public override void OnEnable() {
        base.OnEnable();
        m_Group?.Register(this);
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
}

public class Slider : Selectable, IDragHandler, IInitializePotentialDragHandler, IPointerDownHandler
{
    public enum Direction { LeftToRight, RightToLeft, BottomToTop, TopToBottom }

    [SerializeField] private RectTransform? m_FillRect, m_HandleRect;
    [SerializeField] private Direction m_Direction = Direction.LeftToRight;
    [SerializeField] private float m_MinValue, m_MaxValue = 1, m_Value = 1;
    [SerializeField] private bool m_WholeNumbers;
    private readonly UIEvent<float> m_OnValueChanged = new();
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
        clamped = Math.Clamp(clamped, m_MinValue, m_MaxValue);
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
    public override void OnPointerDown(PointerEventData eventData) {
        base.OnPointerDown(eventData);
        if (!IsInteractable()) return;
        m_Dragging = true;
        UpdateDrag(eventData);
    }
    public void OnDrag(PointerEventData eventData) {
        if (!IsInteractable()) return;
        UpdateDrag(eventData);
    }
    private void UpdateDrag(PointerEventData eventData) {
        if (Owner.Parent == null) return;
        var parent = Owner.Parent.GetComponent<RectTransform>();
        if (parent == null) return;
        // Approximate axis mapping using the parent rect in local space.
        Vector2 local = eventData.position;
        // Pointer positions are framebuffer pixels; Slider geometry is canvas-local.
        // Use handle-normalized value from axis extent of the parent rect.
        var rect = parent.rect;
        float t = m_Direction is Direction.LeftToRight or Direction.RightToLeft
            ? Math.Clamp((local.x - rect.min.x) / MathF.Max(1e-4f, rect.width), 0, 1)
            : Math.Clamp((local.y - rect.min.y) / MathF.Max(1e-4f, rect.height), 0, 1);
        if (m_Direction is Direction.RightToLeft or Direction.TopToBottom) t = 1f - t;
        normalizedValue = t;
    }
    private void UpdateVisuals() {
        float n = normalizedValue;
        if (m_FillRect != null) {
            var fill = m_FillRect;
            bool horizontal = m_Direction is Direction.LeftToRight or Direction.RightToLeft;
            fill.anchorMin = new Vector2(0, 0);
            fill.anchorMax = horizontal ? new Vector2(n, 1) : new Vector2(1, n);
            if (m_Direction == Direction.RightToLeft) fill.anchorMax = new Vector2(1 - n, 1);
            if (m_Direction == Direction.TopToBottom) fill.anchorMax = new Vector2(1, 1 - n);
            fill.offsetMin = Vector2.zero;
            fill.offsetMax = Vector2.zero;
        }
        if (m_HandleRect != null) {
            var handle = m_HandleRect;
            bool horizontal = m_Direction is Direction.LeftToRight or Direction.RightToLeft;
            if (m_Direction == Direction.RightToLeft) n = 1f - n;
            if (m_Direction == Direction.TopToBottom) n = 1f - n;
            handle.anchorMin = horizontal ? new Vector2(n, 0) : new Vector2(0, n);
            handle.anchorMax = handle.anchorMin;
        }
    }
}

public class Scrollbar : Selectable, IDragHandler, IInitializePotentialDragHandler, IPointerDownHandler
{
    public enum Direction { LeftToRight, RightToLeft, BottomToTop, TopToBottom }
    [SerializeField] private RectTransform? m_HandleRect;
    [SerializeField] private Direction m_Direction = Direction.LeftToRight;
    [SerializeField] private float m_Value, m_Size = 0.2f;
    [SerializeField] private int m_NumberOfSteps;
    private readonly UIEvent<float> m_OnValueChanged = new();

    public UIEvent<float> onValueChanged => m_OnValueChanged;
    public RectTransform? handleRect { get => m_HandleRect; set { m_HandleRect = value; UpdateVisuals(); } }
    public float value {
        get => m_Value;
        set {
            float v = Math.Clamp(value, 0, 1);
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
        set { m_NumberOfSteps = Math.Max(0, value); UpdateVisuals(); }
    }
    public void SetValueWithoutNotify(float value) {
        m_Value = Math.Clamp(value, 0, 1);
        UpdateVisuals();
    }
    public override void OnEnable() { base.OnEnable(); UpdateVisuals(); }
    public void OnInitializePotentialDrag(PointerEventData eventData) { }
    public override void OnPointerDown(PointerEventData eventData) {
        base.OnPointerDown(eventData);
        if (IsInteractable()) UpdateFromPoint(eventData.position);
    }
    public void OnDrag(PointerEventData eventData) { if (IsInteractable()) UpdateFromPoint(eventData.position); }
    private void UpdateFromPoint(Vector2 screen) {
        if (Owner.Parent == null) return;
        var parent = Owner.Parent.GetComponent<RectTransform>();
        if (parent == null) return;
        var rect = parent.rect;
        bool horizontal = m_Direction is Direction.LeftToRight or Direction.RightToLeft;
        float t = horizontal
            ? Math.Clamp((screen.x - rect.min.x) / MathF.Max(1e-4f, rect.width), 0, 1)
            : Math.Clamp((screen.y - rect.min.y) / MathF.Max(1e-4f, rect.height), 0, 1);
        if (m_Direction is Direction.RightToLeft or Direction.TopToBottom) t = 1f - t;
        if (m_NumberOfSteps > 1) t = MathF.Round(t * (m_NumberOfSteps - 1)) / (m_NumberOfSteps - 1);
        value = t;
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
