using Overload.EventSystems;

namespace Overload.UI;

public class LayoutElement : UIBehaviour, ILayoutElement, ILayoutIgnorer
{
    [SerializeField] private bool m_IgnoreLayout;
    [SerializeField] private float m_MinWidth = -1, m_MinHeight = -1, m_PreferredWidth = -1, m_PreferredHeight = -1;
    [SerializeField] private float m_FlexibleWidth = -1, m_FlexibleHeight = -1;
    [SerializeField] private int m_LayoutPriority = 1;
    public virtual bool ignoreLayout { get => m_IgnoreLayout; set { m_IgnoreLayout = value; SetDirty(); } }
    public virtual float minWidth { get => m_MinWidth; set { m_MinWidth = CanvasScaler.Finite(value); SetDirty(); } }
    public virtual float minHeight { get => m_MinHeight; set { m_MinHeight = CanvasScaler.Finite(value); SetDirty(); } }
    public virtual float preferredWidth { get => m_PreferredWidth; set { m_PreferredWidth = CanvasScaler.Finite(value); SetDirty(); } }
    public virtual float preferredHeight { get => m_PreferredHeight; set { m_PreferredHeight = CanvasScaler.Finite(value); SetDirty(); } }
    public virtual float flexibleWidth { get => m_FlexibleWidth; set { m_FlexibleWidth = CanvasScaler.Finite(value); SetDirty(); } }
    public virtual float flexibleHeight { get => m_FlexibleHeight; set { m_FlexibleHeight = CanvasScaler.Finite(value); SetDirty(); } }
    public virtual int layoutPriority { get => m_LayoutPriority; set { m_LayoutPriority = value; SetDirty(); } }
    public virtual void CalculateLayoutInputHorizontal() { }
    public virtual void CalculateLayoutInputVertical() { }
}

public class ContentSizeFitter : UIBehaviour, ILayoutSelfController
{
    public enum FitMode { Unconstrained, MinSize, PreferredSize }
    [SerializeField] private FitMode m_HorizontalFit, m_VerticalFit;
    public FitMode horizontalFit { get => m_HorizontalFit; set { m_HorizontalFit = value; SetDirty(); } }
    public FitMode verticalFit { get => m_VerticalFit; set { m_VerticalFit = value; SetDirty(); } }
    protected virtual void HandleSelfFittingAlongAxis(int axis) {
        FitMode mode = axis == 0 ? horizontalFit : verticalFit;
        if (mode == FitMode.Unconstrained) return;
        float size = mode == FitMode.MinSize ? LayoutUtility.GetMinSize(rectTransform, axis) : LayoutUtility.GetPreferredSize(rectTransform, axis);
        rectTransform.SetSizeWithCurrentAnchors((RectTransform.Axis)axis, size);
    }
    public virtual void SetLayoutHorizontal() => HandleSelfFittingAlongAxis(0);
    public virtual void SetLayoutVertical() => HandleSelfFittingAlongAxis(1);
}
