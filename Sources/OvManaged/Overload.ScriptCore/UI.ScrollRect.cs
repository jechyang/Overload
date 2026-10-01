using System;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public class ScrollRect : UIBehaviour, IDragHandler, IInitializePotentialDragHandler, IBeginDragHandler, IEndDragHandler, IScrollHandler, ICanvasElement
{
    public enum MovementType { Unrestricted, Elastic, Clamped }
    public enum ScrollbarVisibility { Permanent, AutoHide, AutoHideAndExpandViewport }

    [SerializeField] private RectTransform? m_Content, m_Viewport;
    [SerializeField] private bool m_Horizontal = true, m_Vertical = true;
    [SerializeField] private MovementType m_MovementType = MovementType.Elastic;
    [SerializeField] private float m_Elasticity = 0.1f, m_DecelerationRate = 0.135f, m_ScrollSensitivity = 1;
    [SerializeField] private bool m_Inertia = true;
    [SerializeField] private float m_HorizontalNormalizedPosition = 1, m_VerticalNormalizedPosition = 1;
    private Vector2 m_Velocity;
    private bool m_Dragging;
    private Vector2 m_PointerStartLocalCursor;
    private Vector2 m_ContentStartPosition;
    private readonly UIEvent<Vector2> m_OnValueChanged = new();

    public UIEvent<Vector2> onValueChanged => m_OnValueChanged;
    public RectTransform? content { get => m_Content; set => m_Content = value; }
    public RectTransform? viewport { get => m_Viewport; set => m_Viewport = value; }
    public bool horizontal { get => m_Horizontal; set => m_Horizontal = value; }
    public bool vertical { get => m_Vertical; set => m_Vertical = value; }
    public MovementType movementType { get => m_MovementType; set => m_MovementType = value; }
    public float elasticity { get => m_Elasticity; set => m_Elasticity = value; }
    public bool inertia { get => m_Inertia; set => m_Inertia = value; }
    public float decelerationRate { get => m_DecelerationRate; set => m_DecelerationRate = value; }
    public float scrollSensitivity { get => m_ScrollSensitivity; set => m_ScrollSensitivity = value; }
    public Vector2 velocity {
        get => m_Velocity;
        set { m_Velocity = value; }
    }
    public float horizontalNormalizedPosition {
        get => m_HorizontalNormalizedPosition;
        set { SetNormalizedPosition(value, 0); }
    }
    public float verticalNormalizedPosition {
        get => m_VerticalNormalizedPosition;
        set { SetNormalizedPosition(value, 1); }
    }
    public bool isDragging => m_Dragging;

    public virtual void Rebuild(CanvasUpdate executing) {
        if (executing != CanvasUpdate.PreRender) return;
        UpdateBounds();
    }
    public RectTransform transform => rectTransform;
    public void LayoutComplete() { }
    public void GraphicUpdateComplete() { }

    public void OnInitializePotentialDrag(PointerEventData eventData) {
        m_Velocity = Vector2.zero;
    }
    public void OnBeginDrag(PointerEventData eventData) { }
    public void OnDrag(PointerEventData eventData) {
        if (!IsActive() || Owner.Parent == null) return;
        var parent = Owner.Parent.GetComponent<RectTransform>();
        if (parent == null) return;
        m_Dragging = true;
        Vector2 pointer = eventData.position;
        if (!m_Dragging) {
            m_PointerStartLocalCursor = pointer;
            if (m_Content != null) m_ContentStartPosition = m_Content.anchoredPosition;
        }
        Vector2 delta = pointer - m_PointerStartLocalCursor;
        Vector2 position = m_ContentStartPosition + new Vector2(m_Horizontal ? delta.x : 0, m_Vertical ? delta.y : 0);
        if (m_MovementType == MovementType.Elastic) {
            // Soft clamp outside content bounds.
            if (m_Content != null && parent != null) {
                var bounds = Bounds();
                if (position.x < bounds.min.x) position.x = bounds.min.x + (position.x - bounds.min.x) * m_Elasticity;
                if (position.x > bounds.max.x) position.x = bounds.max.x + (position.x - bounds.max.x) * m_Elasticity;
                if (position.y < bounds.min.y) position.y = bounds.min.y + (position.y - bounds.min.y) * m_Elasticity;
                if (position.y > bounds.max.y) position.y = bounds.max.y + (position.y - bounds.max.y) * m_Elasticity;
            }
        } else if (m_MovementType == MovementType.Clamped) {
            var bounds = Bounds();
            position.x = Math.Clamp(position.x, bounds.min.x, bounds.max.x);
            position.y = Math.Clamp(position.y, bounds.min.y, bounds.max.y);
        }
        if (m_Content != null) m_Content.anchoredPosition = position;
        UpdateNormalized();
    }
    public void OnEndDrag(PointerEventData eventData) {
        m_Dragging = false;
    }
    public void OnScroll(PointerEventData eventData) {
        if (!IsActive()) return;
        Vector2 delta = eventData.scrollDelta * m_ScrollSensitivity;
        if (m_Content == null) return;
        Vector2 pos = m_Content.anchoredPosition;
        if (m_Vertical) pos.y += delta.y;
        if (m_Horizontal) pos.x += delta.x;
        if (m_MovementType == MovementType.Clamped) {
            var bounds = Bounds();
            pos.x = Math.Clamp(pos.x, bounds.min.x, bounds.max.x);
            pos.y = Math.Clamp(pos.y, bounds.min.y, bounds.max.y);
        }
        m_Content.anchoredPosition = pos;
        UpdateNormalized();
    }

    private Rect Bounds() {
        // Content origin is its own pivot; clamp positions relative to viewport size.
        Vector2 view = m_Viewport?.rect.size ?? Owner.GetComponent<RectTransform>()?.rect.size ?? new Vector2(100, 100);
        Vector2 size = m_Content?.rect.size ?? view;
        float minX = Math.Min(0, view.x - size.x), maxX = 0;
        float minY = Math.Min(0, view.y - size.y), maxY = 0;
        return new Rect(new Vector2(minX, minY), new Vector2(MathF.Max(0, maxX - minX), MathF.Max(0, maxY - minY)));
    }
    private void UpdateBounds() { UpdateNormalized(); }
    private void UpdateNormalized() {
        if (m_Content == null) return;
        Vector2 size = m_Content.rect.size;
        Vector2 pos = m_Content.anchoredPosition;
        if (m_Horizontal && size.x > 0) m_HorizontalNormalizedPosition = Math.Clamp(1f + pos.x / size.x, 0, 1);
        if (m_Vertical && size.y > 0) m_VerticalNormalizedPosition = Math.Clamp(1f + pos.y / size.y, 0, 1);
        m_OnValueChanged.Invoke(new Vector2(m_HorizontalNormalizedPosition, m_VerticalNormalizedPosition));
    }
    private void SetNormalizedPosition(float value, int axis) {
        UpdateBounds();
        if (m_Content == null) return;
        Vector2 size = m_Content.rect.size;
        Vector2 pos = m_Content.anchoredPosition;
        if (axis == 0) {
            m_HorizontalNormalizedPosition = Math.Clamp(value, 0, 1);
            pos.x = (m_HorizontalNormalizedPosition - 1f) * size.x;
        } else {
            m_VerticalNormalizedPosition = Math.Clamp(value, 0, 1);
            pos.y = (m_VerticalNormalizedPosition - 1f) * size.y;
        }
        m_Content.anchoredPosition = pos;
        m_OnValueChanged.Invoke(new Vector2(m_HorizontalNormalizedPosition, m_VerticalNormalizedPosition));
    }
}
