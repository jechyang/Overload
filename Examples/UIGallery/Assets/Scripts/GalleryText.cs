using Overload.EventSystems;
using Overload.Maths;
using Overload.UI;

namespace UIGallery;

public class TitleText : Text
{
    public TitleText()
    {
        text = "Overload UGUI Gallery";
        fontSize = 22;
        fontStyle = FontStyle.Bold;
        alignment = TextAnchor.MiddleCenter;
        color = new Color(0.95f, 0.96f, 1f, 1f);
    }
}

public class SectionHeader : Text
{
    public SectionHeader()
    {
        text = "Section";
        fontSize = 13;
        fontStyle = FontStyle.Bold;
        alignment = TextAnchor.MiddleLeft;
        color = new Color(0.75f, 0.82f, 0.95f, 1f);
        horizontalOverflow = HorizontalWrapMode.Wrap;
    }
}

public class Caption : Text
{
    public Caption()
    {
        text = "Caption";
        fontSize = 11;
        alignment = TextAnchor.MiddleCenter;
        color = new Color(0.85f, 0.88f, 0.94f, 1f);
        horizontalOverflow = HorizontalWrapMode.Wrap;
        verticalOverflow = VerticalWrapMode.Truncate;
    }
}

public class BodyCopy : Text
{
    public BodyCopy()
    {
        text = "Body";
        fontSize = 12;
        alignment = TextAnchor.UpperLeft;
        color = new Color(0.88f, 0.90f, 0.94f, 1f);
        horizontalOverflow = HorizontalWrapMode.Wrap;
        verticalOverflow = VerticalWrapMode.Truncate;
    }
}

public class RichLine : Text
{
    public RichLine()
    {
        supportRichText = true;
        text = "<b>Bold</b> <color=#ffcc66>amber</color> <size=16>16</size> <i>it</i>";
        fontSize = 13;
        alignment = TextAnchor.MiddleCenter;
    }
}

public class CjkLine : Text
{
    public CjkLine()
    {
        text = "中文排版 + emoji \U0001F600";
        fontSize = 13;
        alignment = TextAnchor.MiddleCenter;
        supportRichText = false;
    }
}

public class BestFitLine : Text
{
    public BestFitLine()
    {
        text = "BEST FIT to box";
        fontSize = 26;
        resizeTextForBestFit = true;
        resizeTextMinSize = 10;
        resizeTextMaxSize = 26;
        alignment = TextAnchor.MiddleCenter;
    }
}

public class ClipCopy : Text
{
    public ClipCopy()
    {
        text = "RectMask2D clips this long copy. Wrap keeps the panel readable while overflow is cut at the mask edge.";
        fontSize = 12;
        horizontalOverflow = HorizontalWrapMode.Wrap;
        verticalOverflow = VerticalWrapMode.Overflow;
        alignment = TextAnchor.UpperLeft;
    }
}

public class OutlineLabel : Text
{
    public OutlineLabel()
    {
        text = "Outline";
        fontSize = 18;
        fontStyle = FontStyle.Bold;
        alignment = TextAnchor.MiddleCenter;
        color = new Color(0.95f, 0.85f, 0.35f, 1f);
    }
}

public class ShadowLabel : Text
{
    public ShadowLabel()
    {
        text = "Shadow";
        fontSize = 18;
        fontStyle = FontStyle.Bold;
        alignment = TextAnchor.MiddleCenter;
        color = new Color(0.55f, 0.85f, 0.70f, 1f);
    }
}

public class TitleOutline : Outline
{
    public TitleOutline()
    {
        effectColor = new Color(0f, 0f, 0f, 0.7f);
        effectDistance = new Vector2(1f, -1f);
        useGraphicAlpha = true;
    }
}

public class SoftOutline : Outline
{
    public SoftOutline()
    {
        effectColor = new Color(0.05f, 0.05f, 0.12f, 0.6f);
        effectDistance = new Vector2(1.5f, -1.5f);
        useGraphicAlpha = true;
    }
}

public class DropShadow : Shadow
{
    public DropShadow()
    {
        effectColor = new Color(0f, 0f, 0f, 0.55f);
        effectDistance = new Vector2(2f, -2f);
        useGraphicAlpha = true;
    }
}

public class ClipWindow : RectMask2D { }

public class StencilWindow : Mask
{
    public StencilWindow() { showMaskGraphic = true; }
}

public class DimGroup : CanvasGroup
{
    public DimGroup()
    {
        alpha = 0.55f;
        blocksRaycasts = true;
        interactable = true;
    }
}

public class EventSystemRoot : EventSystem { }

public class InputModule : StandaloneInputModule { }

public class PulseTarget : Image,
    IPointerEnterHandler,
    IPointerExitHandler,
    IPointerDownHandler,
    IPointerUpHandler,
    IPointerClickHandler
{
    private static readonly Color Normal = new(0.28f, 0.45f, 0.85f, 1f);
    private static readonly Color Hover = new(0.35f, 0.70f, 0.55f, 1f);
    private static readonly Color Pressed = new(0.85f, 0.45f, 0.30f, 1f);
    private static readonly Color Clicked = new(0.95f, 0.80f, 0.25f, 1f);

    public PulseTarget()
    {
        color = Normal;
        raycastTarget = true;
    }

    public void OnPointerEnter(PointerEventData eventData) => color = Hover;
    public void OnPointerExit(PointerEventData eventData) => color = Normal;
    public void OnPointerDown(PointerEventData eventData) => color = Pressed;
    public void OnPointerUp(PointerEventData eventData) => color = Hover;
    public void OnPointerClick(PointerEventData eventData) => color = Clicked;
}

public class Hint : Text
{
    public Hint()
    {
        text = "Hover / click the blue panel";
        fontSize = 11;
        alignment = TextAnchor.MiddleCenter;
        color = new Color(0.80f, 0.85f, 0.95f, 1f);
    }
}
