using Overload;
using Overload.Maths;
using Overload.UI;

[assembly: EnableUI]

namespace UIGallery;

// Thin project subclasses of production Overload.UI / EventSystems types.
// Constructors configure defaults so the scene stays readable; fields remain
// editable through the shared Inspector ABI.

public class GalleryCanvas : CanvasScaler
{
    public GalleryCanvas()
    {
        uiScaleMode = ScaleMode.ScaleWithScreenSize;
        referenceResolution = new Vector2(960, 540);
        matchWidthOrHeight = 0.5f;
    }
}

// Stacks Header / Body / Footer so they do not all stretch to the full canvas.
public class RootStack : VerticalLayoutGroup
{
    public int passes;
    public int verticalPasses;
    public RootStack()
    {
        padding = new RectOffset(0, 0, 0, 0);
        spacing = 0;
        childControlWidth = true;
        childControlHeight = true;
        childForceExpandWidth = true;
        childForceExpandHeight = false;
        childAlignment = TextAnchor.UpperLeft;
    }
    public override void SetLayoutHorizontal() { ++passes; base.SetLayoutHorizontal(); }
    public override void SetLayoutVertical() { ++verticalPasses; base.SetLayoutVertical(); }
}

public class HeaderBar : LayoutElement
{
    public HeaderBar()
    {
        minHeight = 48;
        preferredHeight = 48;
        flexibleHeight = 0;
    }
}

public class FooterBar : LayoutElement
{
    public FooterBar()
    {
        minHeight = 56;
        preferredHeight = 56;
        flexibleHeight = 0;
    }
}

public class BodyFlexible : LayoutElement
{
    public BodyFlexible()
    {
        minHeight = 120;
        flexibleHeight = 1;
        flexibleWidth = 1;
    }
}

public class PanelBox : LayoutElement
{
    public PanelBox()
    {
        minWidth = 160;
        preferredWidth = 220;
        minHeight = 80;
        preferredHeight = 120;
        flexibleHeight = 1;
    }
}

public class GridBox : LayoutElement
{
    public GridBox()
    {
        minWidth = 170;
        preferredWidth = 180;
        minHeight = 130;
        preferredHeight = 140;
    }
}

public class ClipBox : LayoutElement
{
    public ClipBox()
    {
        minWidth = 180;
        preferredWidth = 220;
        minHeight = 64;
        preferredHeight = 70;
    }
}

public class HeaderLayout : HorizontalLayoutGroup
{
    public HeaderLayout()
    {
        padding = new RectOffset(12, 12, 8, 8);
        spacing = 8;
        childControlWidth = true;
        childControlHeight = true;
        childForceExpandWidth = true;
        childForceExpandHeight = true;
        childAlignment = TextAnchor.MiddleCenter;
    }
}

public class ColumnLayout : VerticalLayoutGroup
{
    public ColumnLayout()
    {
        padding = new RectOffset(10, 10, 10, 10);
        spacing = 6;
        childControlWidth = true;
        childControlHeight = true;
        childForceExpandWidth = true;
        childForceExpandHeight = false;
        childAlignment = TextAnchor.UpperLeft;
    }
}

public class RowLayout : HorizontalLayoutGroup
{
    public RowLayout()
    {
        padding = new RectOffset(8, 8, 8, 8);
        spacing = 6;
        childControlWidth = true;
        childControlHeight = true;
        childForceExpandWidth = false;
        childForceExpandHeight = false;
        childAlignment = TextAnchor.MiddleLeft;
    }
}

public class DemoGrid : GridLayoutGroup
{
    public int passes;
    public DemoGrid()
    {
        padding = new RectOffset(8, 8, 8, 8);
        cellSize = new Vector2(48, 36);
        spacing = new Vector2(6, 6);
        startCorner = Corner.UpperLeft;
        startAxis = Axis.Horizontal;
        constraint = Constraint.FixedColumnCount;
        constraintCount = 3;
    }
    public override void SetLayoutHorizontal() { ++passes; base.SetLayoutHorizontal(); }
}

public class FitContent : ContentSizeFitter
{
    public FitContent()
    {
        horizontalFit = FitMode.PreferredSize;
        verticalFit = FitMode.PreferredSize;
    }
}

public class CardSize : LayoutElement
{
    public CardSize()
    {
        minWidth = 40;
        preferredWidth = 72;
        flexibleWidth = 1;
        preferredHeight = 28;
    }
}

public class FixedLabel : LayoutElement
{
    public FixedLabel()
    {
        minWidth = 80;
        preferredWidth = 110;
        preferredHeight = 20;
    }
}

public class TallCard : LayoutElement
{
    public TallCard()
    {
        minWidth = 48;
        preferredWidth = 64;
        preferredHeight = 40;
    }
}

public class TileSize : LayoutElement
{
    public TileSize()
    {
        minWidth = 40;
        preferredWidth = 56;
        preferredHeight = 34;
    }
}

public class SquareAspect : AspectRatioFitter
{
    public SquareAspect()
    {
        aspectMode = AspectMode.FitInParent;
        aspectRatio = 1f;
    }
}
