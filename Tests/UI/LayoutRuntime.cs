using Overload;
using Overload.UI;
using Overload.Maths;
[assembly: EnableUI]
namespace LayoutRuntime;
public class Grid : GridLayoutGroup
{
    public int corner, axis, constraintMode, alignment, passes;
    public Grid() { cellSize = new(40,30); spacing = new(5,7); padding = new(10,10,10,10); constraintCount = 2; }
    public override void CalculateLayoutInputHorizontal() {
        startCorner = (Corner)corner; startAxis = (Axis)axis; constraint = (Constraint)constraintMode; childAlignment = (TextAnchor)alignment;
        ++passes; base.CalculateLayoutInputHorizontal();
    }
}
public class Aspect : AspectRatioFitter
{
    public Aspect() { aspectRatio = 2; aspectMode = AspectMode.FitInParent; }
}
public class Content : ContentSizeFitter
{
    public Content() { horizontalFit = FitMode.PreferredSize; verticalFit = FitMode.MinSize; }
}
public class Sizes : LayoutElement
{
    public Sizes() { preferredWidth = 70; minHeight = 25; }
}
