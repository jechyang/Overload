using System;
using Overload.Maths;

namespace Overload.UI;

public class GridLayoutGroup : LayoutGroup
{
    public enum Corner { UpperLeft, UpperRight, LowerLeft, LowerRight }
    public enum Axis { Horizontal, Vertical }
    public enum Constraint { Flexible, FixedColumnCount, FixedRowCount }
    [SerializeField] private Corner m_StartCorner;
    [SerializeField] private Axis m_StartAxis;
    [SerializeField] private Vector2 m_CellSize = new(100, 100), m_Spacing;
    [SerializeField] private Constraint m_Constraint;
    [SerializeField] private int m_ConstraintCount = 2;
    public Corner startCorner { get => m_StartCorner; set { ValidateEnum(value); SetProperty(ref m_StartCorner, value); } }
    public Axis startAxis { get => m_StartAxis; set { ValidateEnum(value); SetProperty(ref m_StartAxis, value); } }
    public Vector2 cellSize { get => m_CellSize; set { ValidateVector(value); SetProperty(ref m_CellSize, value); } }
    public Vector2 spacing { get => m_Spacing; set { ValidateVector(value); SetProperty(ref m_Spacing, value); } }
    public Constraint constraint { get => m_Constraint; set { ValidateEnum(value); SetProperty(ref m_Constraint, value); } }
    public int constraintCount { get => Math.Max(1, m_ConstraintCount); set => SetProperty(ref m_ConstraintCount, Math.Max(1, value)); }
    private static void ValidateEnum<T>(T value) where T : struct, Enum {
        if (!Enum.IsDefined(value)) throw new ArgumentOutOfRangeException(nameof(value));
    }
    private static void ValidateVector(Vector2 value) { CanvasScaler.Finite(value.x); CanvasScaler.Finite(value.y); }
    private static int Divide(int count, int by) => count / by + (count % by == 0 ? 0 : 1);
    private int Capacity(int axis) {
        float step = cellSize[axis] + spacing[axis];
        if (step <= 0) return int.MaxValue;
        double available = rectTransform.rect.size[axis] - (axis == 0 ? padding.horizontal : padding.vertical) + spacing[axis];
        return (int)Math.Clamp(Math.Floor((available + 0.001) / step), 1, int.MaxValue);
    }
    public override void CalculateLayoutInputHorizontal() {
        base.CalculateLayoutInputHorizontal();
        int min = constraint == Constraint.FixedColumnCount ? constraintCount :
            constraint == Constraint.FixedRowCount ? Divide(rectChildren.Count, constraintCount) : 1;
        int preferred = constraint == Constraint.Flexible ? (int)Math.Ceiling(Math.Sqrt(rectChildren.Count)) : min;
        SetLayoutInputForAxis(padding.horizontal + min * (cellSize.x + spacing.x) - spacing.x,
            padding.horizontal + preferred * (cellSize.x + spacing.x) - spacing.x, -1, 0);
    }
    public override void CalculateLayoutInputVertical() {
        int rows = constraint == Constraint.FixedRowCount ? constraintCount :
            Divide(rectChildren.Count, constraint == Constraint.FixedColumnCount ? constraintCount : Capacity(0));
        float size = padding.vertical + rows * (cellSize.y + spacing.y) - spacing.y;
        SetLayoutInputForAxis(size, size, -1, 1);
    }
    public override void SetLayoutHorizontal() {
        foreach (var child in rectChildren) {
            m_Tracker.Add(this, child, DrivenTransformProperties.Anchors | DrivenTransformProperties.AnchoredPosition | DrivenTransformProperties.SizeDelta);
            SetAnchors(child);
            if (child.sizeDelta != cellSize) child.sizeDelta = cellSize;
        }
    }
    public override void SetLayoutVertical() {
        int count = rectChildren.Count;
        if (count == 0) { m_Tracker.EndUpdate(); return; }
        int columns = constraint == Constraint.FixedColumnCount ? constraintCount :
            constraint == Constraint.FixedRowCount ? Divide(count, constraintCount) : Capacity(0);
        int rows = constraint == Constraint.FixedRowCount ? constraintCount :
            constraint == Constraint.FixedColumnCount ? Divide(count, constraintCount) : Capacity(1);
        int stride = startAxis == Axis.Horizontal ? columns : rows;
        int actualColumns = startAxis == Axis.Horizontal ? Math.Min(columns, count) : Math.Min(columns, Divide(count, stride));
        int actualRows = startAxis == Axis.Vertical ? Math.Min(rows, count) : Math.Min(rows, Divide(count, stride));
        float left = GetStartOffset(0, actualColumns * (cellSize.x + spacing.x) - spacing.x);
        float top = GetStartOffset(1, actualRows * (cellSize.y + spacing.y) - spacing.y);
        for (int i = 0; i < count; ++i) {
            int x = startAxis == Axis.Horizontal ? i % stride : i / stride;
            int y = startAxis == Axis.Horizontal ? i / stride : i % stride;
            if (((int)startCorner & 1) != 0) x = actualColumns - 1 - x;
            if (((int)startCorner & 2) != 0) y = actualRows - 1 - y;
            SetChildAlongAxis(rectChildren[i], 0, left + x * (cellSize.x + spacing.x), cellSize.x);
            SetChildAlongAxis(rectChildren[i], 1, top + y * (cellSize.y + spacing.y), cellSize.y);
        }
        m_Tracker.EndUpdate();
    }
}
