using System;
using Overload.EventSystems;

namespace Overload.UI;

public class LayoutElement : UIBehaviour, ILayoutElement, ILayoutIgnorer
{
    [SerializeField] private bool m_IgnoreLayout;
    [SerializeField] private float m_MinWidth = -1, m_MinHeight = -1, m_PreferredWidth = -1, m_PreferredHeight = -1;
    [SerializeField] private float m_FlexibleWidth = -1, m_FlexibleHeight = -1;
    [SerializeField] private int m_LayoutPriority = 1;
    public virtual bool ignoreLayout { get => m_IgnoreLayout; set { var next = value; if (m_IgnoreLayout == next) return; m_IgnoreLayout = next; SetDirty(); } }
    public virtual float minWidth { get => m_MinWidth; set { var next = CanvasScaler.Finite(value); if (m_MinWidth == next) return; m_MinWidth = next; SetDirty(); } }
    public virtual float minHeight { get => m_MinHeight; set { var next = CanvasScaler.Finite(value); if (m_MinHeight == next) return; m_MinHeight = next; SetDirty(); } }
    public virtual float preferredWidth { get => m_PreferredWidth; set { var next = CanvasScaler.Finite(value); if (m_PreferredWidth == next) return; m_PreferredWidth = next; SetDirty(); } }
    public virtual float preferredHeight { get => m_PreferredHeight; set { var next = CanvasScaler.Finite(value); if (m_PreferredHeight == next) return; m_PreferredHeight = next; SetDirty(); } }
    public virtual float flexibleWidth { get => m_FlexibleWidth; set { var next = CanvasScaler.Finite(value); if (m_FlexibleWidth == next) return; m_FlexibleWidth = next; SetDirty(); } }
    public virtual float flexibleHeight { get => m_FlexibleHeight; set { var next = CanvasScaler.Finite(value); if (m_FlexibleHeight == next) return; m_FlexibleHeight = next; SetDirty(); } }
    public virtual int layoutPriority { get => m_LayoutPriority; set { var next = value; if (m_LayoutPriority == next) return; m_LayoutPriority = next; SetDirty(); } }
    protected override void SetDirty() { if (owner != null) LayoutRebuilder.MarkLayoutForRebuild(rectTransform, true); }
    protected override void OnRectTransformDimensionsChange() {
        if (!CanvasUpdateRegistry.IsRebuildingLayout(Owner)) SetDirty();
    }
    public virtual void CalculateLayoutInputHorizontal() { }
    public virtual void CalculateLayoutInputVertical() { }
}

public class ContentSizeFitter : UIBehaviour, ILayoutSelfController
{
    public enum FitMode { Unconstrained, MinSize, PreferredSize }
    [SerializeField] private FitMode m_HorizontalFit, m_VerticalFit;
    private readonly DrivenRectTransformTracker tracker = new();
    protected override void SetDirty() { if (owner != null) LayoutRebuilder.MarkLayoutForRebuild(rectTransform, true); }
    protected override void OnRectTransformDimensionsChange() {
        if (!CanvasUpdateRegistry.IsRebuildingLayout(Owner)) SetDirty();
    }

    public override void OnEnable() { base.OnEnable(); SetDirty(); }
    public override void OnDisable() { tracker.Clear(); SetDirty(); base.OnDisable(); }
    internal override void ReleaseNativeComponents() { tracker.Clear(); base.ReleaseNativeComponents(); }
    public FitMode horizontalFit { get => m_HorizontalFit; set { var next = value; if (m_HorizontalFit == next) return; m_HorizontalFit = next; if (next == FitMode.Unconstrained) tracker.Release(DrivenTransformProperties.SizeDeltaX); SetDirty(); } }
    public FitMode verticalFit { get => m_VerticalFit; set { var next = value; if (m_VerticalFit == next) return; m_VerticalFit = next; if (next == FitMode.Unconstrained) tracker.Release(DrivenTransformProperties.SizeDeltaY); SetDirty(); } }
    protected virtual void HandleSelfFittingAlongAxis(int axis) {
        FitMode mode = axis == 0 ? horizontalFit : verticalFit;
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        if (mode == FitMode.Unconstrained) return;
        tracker.Add(this, rectTransform, axis == 0 ? DrivenTransformProperties.SizeDeltaX : DrivenTransformProperties.SizeDeltaY);
        float size = mode == FitMode.MinSize ? LayoutUtility.GetMinSize(rectTransform, axis) : LayoutUtility.GetPreferredSize(rectTransform, axis);
        if (rectTransform.rect.size[axis] != size) rectTransform.SetSizeWithCurrentAnchors((RectTransform.Axis)axis, size);
    }
    public virtual void SetLayoutHorizontal() {
        tracker.BeginUpdate();
        var wanted = (horizontalFit == FitMode.Unconstrained ? DrivenTransformProperties.None : DrivenTransformProperties.SizeDeltaX) |
            (verticalFit == FitMode.Unconstrained ? DrivenTransformProperties.None : DrivenTransformProperties.SizeDeltaY);
        tracker.Retain(rectTransform, wanted); HandleSelfFittingAlongAxis(0);
    }
    public virtual void SetLayoutVertical() { HandleSelfFittingAlongAxis(1); tracker.EndUpdate(); }
}
