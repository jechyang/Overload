using System;
using System.Text;
using Overload;
using Overload.UI;

[assembly: EnableUI]
namespace FontFixture;

public class Label : Text
{
    [SerializeField] public int awakes;
    [SerializeField] public int updates;
    [SerializeField] public int sizeUsed;
    public Label()
    {
        font = new(AssetKind.Font, "outline-fixture.ttf"); text = "A"; fontSize = 40;
        alignment = TextAnchor.LowerLeft; horizontalOverflow = HorizontalWrapMode.Overflow;
    }
    public override void OnAwake() { ++awakes; base.OnAwake(); }
    public override void OnUpdate(float delta)
    {
        ++updates;
        if (delta == 1) fontSize = 16;
        if (delta == 2) fontSize = 64;
        if (delta == 3) { font = new(AssetKind.Font, "Roboto-Regular.ttf"); text = "SDF"; }
        if (delta == 4) { font = new(AssetKind.Font, "outline-fixture.ttf"); text = "A"; fontSize = 40; resizeTextForBestFit = false; lineSpacing = 1; }
        if (delta == 5) { supportRichText = true; text = "<b>A</b><color=#ff0000>O</color>"; }
        if (delta == 6) { text = "AAAA"; resizeTextForBestFit = true; resizeTextMinSize = 10; resizeTextMaxSize = 40; }
        if (delta == 7) { text = "A\nA"; resizeTextForBestFit = false; lineSpacing = .5f; }
        base.OnUpdate(delta);
    }
    public override void GraphicUpdateComplete() { sizeUsed = fontSizeUsedForBestFit; base.GraphicUpdateComplete(); }
}
public class MultiPage : Text
{
    public MultiPage()
    {
        font = new(AssetKind.Font, "Roboto-Regular.ttf"); fontSize = 16; supportRichText = false;
        var value = new StringBuilder(); for (int cp = 33; cp < 127; ++cp) value.Append((char)cp); text = value.ToString();
    }
}
public class DefaultFont : Text
{
    public DefaultFont() { text = "A"; fontSize = 20; alignment = TextAnchor.LowerLeft; }
}
public class Fallback : Text
{
    public Fallback()
    {
        font = new(AssetKind.Font, "Roboto-Regular.ttf"); fallbackFonts = new[] { "outline-fixture.ttf" };
        text = "A中😀O"; fontSize = 30; alignment = TextAnchor.LowerLeft;
    }
}
public class Clip : RectMask2D { }
public class Stencil : Mask { public Stencil() { showMaskGraphic = false; } }
public class MaskImage : Image { }
public class GameplayOnly : Behaviour { [SerializeField] public int awakes; public override void OnAwake() => ++awakes; }
