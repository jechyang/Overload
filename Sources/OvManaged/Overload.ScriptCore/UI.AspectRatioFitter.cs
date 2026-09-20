using System;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public class AspectRatioFitter : UIBehaviour, ILayoutSelfController
{
    public enum AspectMode { None, WidthControlsHeight, HeightControlsWidth, FitInParent, EnvelopeParent }
    [SerializeField] private AspectMode m_AspectMode;
    [SerializeField] private float m_AspectRatio = 1;
    private readonly DrivenRectTransformTracker tracker = new();
    protected override void SetDirty() { if (owner != null) LayoutRebuilder.MarkLayoutForRebuild(rectTransform, true); }
    public AspectMode aspectMode {
        get => m_AspectMode;
        set {
            if (!Enum.IsDefined(value)) throw new ArgumentOutOfRangeException(nameof(value));
            if (m_AspectMode == value) return;
            tracker.Clear(); m_AspectMode = value; SetDirty();
        }
    }
    public float aspectRatio {
        get => m_AspectRatio;
        set {
            CanvasScaler.Finite(value);
            if (value <= 0) throw new ArgumentOutOfRangeException(nameof(value));
            if (m_AspectRatio == value) return;
            m_AspectRatio = value; SetDirty();
        }
    }
    public override void OnEnable() { base.OnEnable(); SetDirty(); }
    public override void OnDisable() { tracker.Clear(); SetDirty(); base.OnDisable(); }
    internal override void ReleaseNativeComponents() { tracker.Clear(); base.ReleaseNativeComponents(); }
    private void SetSize(int axis, float value) {
        CanvasScaler.Finite(value);
        if (rectTransform.rect.size[axis] != value) rectTransform.SetSizeWithCurrentAnchors((RectTransform.Axis)axis, value);
    }
    private void UpdateRect() {
        if (!IsActive()) return;
        if (!float.IsFinite(aspectRatio) || aspectRatio <= 0 || !Enum.IsDefined(aspectMode))
            throw new InvalidOperationException("Invalid aspect fitter settings");
        tracker.BeginUpdate();
        switch (aspectMode) {
            case AspectMode.WidthControlsHeight:
                tracker.Add(this, rectTransform, DrivenTransformProperties.SizeDeltaY);
                SetSize(1, rectTransform.rect.width / aspectRatio); break;
            case AspectMode.HeightControlsWidth:
                tracker.Add(this, rectTransform, DrivenTransformProperties.SizeDeltaX);
                SetSize(0, rectTransform.rect.height * aspectRatio); break;
            case AspectMode.FitInParent:
            case AspectMode.EnvelopeParent:
                tracker.Add(this, rectTransform, DrivenTransformProperties.Anchors | DrivenTransformProperties.AnchoredPosition | DrivenTransformProperties.SizeDelta);
                Vector2 parentSize = rectTransform.parent?.rect.size ?? Vector2.zero;
                if (rectTransform.anchorMin != Vector2.zero) rectTransform.anchorMin = Vector2.zero;
                if (rectTransform.anchorMax != Vector2.one) rectTransform.anchorMax = Vector2.one;
                if (rectTransform.anchoredPosition != Vector2.zero) rectTransform.anchoredPosition = Vector2.zero;
                bool useWidth = (parentSize.y * aspectRatio < parentSize.x) != (aspectMode == AspectMode.FitInParent);
                Vector2 size = useWidth ? new(parentSize.x, parentSize.x / aspectRatio) : new(parentSize.y * aspectRatio, parentSize.y);
                Vector2 delta = size - parentSize;
                if (rectTransform.sizeDelta != delta) rectTransform.sizeDelta = delta;
                break;
        }
        tracker.EndUpdate();
    }
    // The horizontal pass establishes width before vertical input is measured.
    public virtual void SetLayoutHorizontal() => UpdateRect();
    public virtual void SetLayoutVertical() => UpdateRect();
}
