using System;
using System.Collections.Generic;
using System.Linq;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public enum TextAnchor { UpperLeft, UpperCenter, UpperRight, MiddleLeft, MiddleCenter, MiddleRight, LowerLeft, LowerCenter, LowerRight }
public record struct RectOffset(int left, int right, int top, int bottom)
{
    public int horizontal => checked(left + right);
    public int vertical => checked(top + bottom);
}
public abstract class LayoutGroup : UIBehaviour, ILayoutElement, ILayoutGroup
{
    [SerializeField] private int m_PaddingLeft, m_PaddingRight, m_PaddingTop, m_PaddingBottom;
    [SerializeField] private TextAnchor m_ChildAlignment;
    protected readonly List<RectTransform> rectChildren = new();
    protected readonly DrivenRectTransformTracker m_Tracker = new();
    public override void OnEnable() { base.OnEnable(); SetDirty(); }
    public override void OnDisable() { m_Tracker.Clear(); SetDirty(); base.OnDisable(); }
    internal override void ReleaseNativeComponents() { m_Tracker.Clear(); base.ReleaseNativeComponents(); }
    protected override void SetDirty() { if (owner != null) LayoutRebuilder.MarkLayoutForRebuild(rectTransform, true); }
    protected void SetProperty<T>(ref T field, T value) {
        if (EqualityComparer<T>.Default.Equals(field, value)) return;
        field = value; SetDirty();
    }
    private readonly float[] minimum = new float[2], preferred = new float[2], flexible = new float[2];
    public RectOffset padding {
        get => new(m_PaddingLeft, m_PaddingRight, m_PaddingTop, m_PaddingBottom);
        set { if (padding == value) return; m_PaddingLeft = value.left; m_PaddingRight = value.right; m_PaddingTop = value.top; m_PaddingBottom = value.bottom; SetDirty(); }
    }
    public TextAnchor childAlignment { get => m_ChildAlignment; set { if (!Enum.IsDefined(value)) throw new ArgumentOutOfRangeException(nameof(value)); SetProperty(ref m_ChildAlignment, value); } }
    public virtual float minWidth => minimum[0];
    public virtual float minHeight => minimum[1];
    public virtual float preferredWidth => preferred[0];
    public virtual float preferredHeight => preferred[1];
    public virtual float flexibleWidth => flexible[0];
    public virtual float flexibleHeight => flexible[1];
    public virtual int layoutPriority => 0;
    public virtual void CalculateLayoutInputHorizontal() {
        m_Tracker.BeginUpdate();
        rectChildren.Clear();
        for (int i = 0; i < Owner.ChildCount; ++i) {
            Actor child = Owner.GetChild(i);
            if (!child.ActiveInHierarchy || child.GetComponent<RectTransform>() is not RectTransform rect) continue;
            var ignorers = child.GetComponents<ILayoutIgnorer>();
            if (ignorers.Length == 0 || ignorers.Any(value => !value.ignoreLayout)) rectChildren.Add(rect);
        }
    }
    public abstract void CalculateLayoutInputVertical();
    public abstract void SetLayoutHorizontal();
    public abstract void SetLayoutVertical();
    protected void SetLayoutInputForAxis(float totalMin, float totalPreferred, float totalFlexible, int axis) {
        minimum[axis] = totalMin; preferred[axis] = totalPreferred; flexible[axis] = totalFlexible;
    }
    protected float GetTotalMinSize(int axis) => minimum[axis];
    protected float GetTotalPreferredSize(int axis) => preferred[axis];
    protected float GetTotalFlexibleSize(int axis) => flexible[axis];
    protected float GetAlignmentOnAxis(int axis) => (axis == 0 ? (int)childAlignment % 3 : (int)childAlignment / 3) * 0.5f;
    protected float GetStartOffset(int axis, float requiredSpaceWithoutPadding) {
        float start = axis == 0 ? padding.left : padding.top;
        float totalPadding = axis == 0 ? padding.horizontal : padding.vertical;
        return start + (rectTransform.rect.size[axis] - totalPadding - requiredSpaceWithoutPadding) * GetAlignmentOnAxis(axis);
    }
    protected static void SetAnchors(RectTransform child, int axis = -1) {
        Vector2 low = child.anchorMin, high = child.anchorMax;
        if (axis != 1) low.x = high.x = 0;
        if (axis != 0) low.y = high.y = 1;
        if (child.anchorMin != low) child.anchorMin = low;
        if (child.anchorMax != high) child.anchorMax = high;
    }
    protected void SetChildAlongAxis(RectTransform child, int axis, float position) => PlaceChild(child, axis, position, null);
    protected void SetChildAlongAxis(RectTransform child, int axis, float position, float size) => PlaceChild(child, axis, position, size);
    private void PlaceChild(RectTransform child, int axis, float position, float? size) {
        var properties = DrivenTransformProperties.Anchors | (axis == 0 ? DrivenTransformProperties.AnchoredPositionX : DrivenTransformProperties.AnchoredPositionY);
        if (size.HasValue) properties |= axis == 0 ? DrivenTransformProperties.SizeDeltaX : DrivenTransformProperties.SizeDeltaY;
        m_Tracker.Add(this, child, properties);
        SetAnchors(child, axis);
        Vector2 delta = child.sizeDelta;
        if (size.HasValue && delta[axis] != size.Value) { delta[axis] = size.Value; child.sizeDelta = delta; }
        Vector2 anchored = child.anchoredPosition;
        float value = axis == 0 ? position + delta[axis] * child.pivot[axis] : -position - delta[axis] * (1 - child.pivot[axis]);
        if (anchored[axis] != value) { anchored[axis] = value; child.anchoredPosition = anchored; }
    }
}

public abstract class HorizontalOrVerticalLayoutGroup : LayoutGroup
{
    [SerializeField] private float m_Spacing;
    [SerializeField] private bool m_ChildControlWidth = true, m_ChildControlHeight = true;
    [SerializeField] private bool m_ChildForceExpandWidth = true, m_ChildForceExpandHeight = true;
    public float spacing { get => m_Spacing; set => SetProperty(ref m_Spacing, CanvasScaler.Finite(value)); }
    public bool childControlWidth { get => m_ChildControlWidth; set => SetProperty(ref m_ChildControlWidth, value); }
    public bool childControlHeight { get => m_ChildControlHeight; set => SetProperty(ref m_ChildControlHeight, value); }
    public bool childForceExpandWidth { get => m_ChildForceExpandWidth; set => SetProperty(ref m_ChildForceExpandWidth, value); }
    public bool childForceExpandHeight { get => m_ChildForceExpandHeight; set => SetProperty(ref m_ChildForceExpandHeight, value); }
    protected virtual void GetChildSizes(RectTransform child, int axis, bool controlSize, bool forceExpand,
        out float min, out float preferred, out float flexible) {
        min = controlSize ? LayoutUtility.GetMinSize(child, axis) : child.sizeDelta[axis];
        preferred = controlSize ? LayoutUtility.GetPreferredSize(child, axis) : min;
        flexible = controlSize ? LayoutUtility.GetFlexibleSize(child, axis) : 0;
        if (forceExpand) flexible = Math.Max(1, flexible);
    }
    protected virtual void CalcAlongAxis(int axis, bool isVertical) {
        float pad = axis == 0 ? padding.horizontal : padding.vertical;
        bool control = axis == 0 ? childControlWidth : childControlHeight;
        bool expand = axis == 0 ? childForceExpandWidth : childForceExpandHeight;
        bool cross = isVertical != (axis == 1);
        float totalMin = pad, totalPreferred = pad, totalFlexible = 0;
        foreach (var child in rectChildren) {
            GetChildSizes(child, axis, control, expand, out float min, out float preferred, out float flexible);
            if (cross) {
                totalMin = Math.Max(totalMin, min + pad); totalPreferred = Math.Max(totalPreferred, preferred + pad); totalFlexible = Math.Max(totalFlexible, flexible);
            } else { totalMin += min + spacing; totalPreferred += preferred + spacing; totalFlexible += flexible; }
        }
        if (!cross && rectChildren.Count > 0) { totalMin -= spacing; totalPreferred -= spacing; }
        SetLayoutInputForAxis(totalMin, Math.Max(totalMin, totalPreferred), totalFlexible, axis);
    }
    protected virtual void SetChildrenAlongAxis(int axis, bool isVertical) {
        float size = rectTransform.rect.size[axis], pad = axis == 0 ? padding.horizontal : padding.vertical;
        bool control = axis == 0 ? childControlWidth : childControlHeight;
        bool expand = axis == 0 ? childForceExpandWidth : childForceExpandHeight;
        float alignment = GetAlignmentOnAxis(axis);
        void Place(RectTransform child, float position, float available) {
            float ownSize = control ? available : child.sizeDelta[axis];
            if (control) SetChildAlongAxis(child, axis, position, ownSize);
            else SetChildAlongAxis(child, axis, position + (available - ownSize) * alignment);
        }
        if (isVertical != (axis == 1)) {
            foreach (var child in rectChildren) {
                GetChildSizes(child, axis, control, expand, out float min, out float preferred, out float flexible);
                float space = size - pad, max = flexible > 0 ? size : preferred;
                if (space < min) space = min; else if (space > max) space = max;
                Place(child, GetStartOffset(axis, space), space);
            }
            return;
        }
        float pos = axis == 0 ? padding.left : padding.top;
        float totalMin = GetTotalMinSize(axis), totalPreferred = GetTotalPreferredSize(axis), totalFlexible = GetTotalFlexibleSize(axis);
        if (totalFlexible == 0 && totalPreferred < size) pos = GetStartOffset(axis, totalPreferred - pad);
        float blend = totalMin == totalPreferred ? 0 : Math.Clamp((size - totalMin) / (totalPreferred - totalMin), 0, 1);
        float surplus = size > totalPreferred && totalFlexible > 0 ? (size - totalPreferred) / totalFlexible : 0;
        foreach (var child in rectChildren) {
            GetChildSizes(child, axis, control, expand, out float min, out float preferred, out float flexible);
            float space = min + (preferred - min) * blend + flexible * surplus;
            Place(child, pos, space); pos += space + spacing;
        }
    }
}
public class HorizontalLayoutGroup : HorizontalOrVerticalLayoutGroup
{
    public override void CalculateLayoutInputHorizontal() { base.CalculateLayoutInputHorizontal(); CalcAlongAxis(0, false); }
    public override void CalculateLayoutInputVertical() => CalcAlongAxis(1, false);
    public override void SetLayoutHorizontal() => SetChildrenAlongAxis(0, false);
    public override void SetLayoutVertical() { SetChildrenAlongAxis(1, false); m_Tracker.EndUpdate(); }
}
public class VerticalLayoutGroup : HorizontalOrVerticalLayoutGroup
{
    public override void CalculateLayoutInputHorizontal() { base.CalculateLayoutInputHorizontal(); CalcAlongAxis(0, true); }
    public override void CalculateLayoutInputVertical() => CalcAlongAxis(1, true);
    public override void SetLayoutHorizontal() => SetChildrenAlongAxis(0, true);
    public override void SetLayoutVertical() { SetChildrenAlongAxis(1, true); m_Tracker.EndUpdate(); }
}
