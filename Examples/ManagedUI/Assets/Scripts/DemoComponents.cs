using Overload.Maths;
using Overload.UI;

namespace ScriptUI;

public class ResponsiveCanvas : CanvasScaler
{
    public ResponsiveCanvas() {
        uiScaleMode = ScaleMode.ScaleWithScreenSize;
        referenceResolution = new Vector2(300, 200);
        matchWidthOrHeight = 0.5f;
    }
}
public class RedCard : LayoutElement
{
    public RedCard() { minWidth = 30; preferredWidth = 50; flexibleWidth = 1; preferredHeight = 20; }
}
public class GreenCard : LayoutElement
{
    public GreenCard() { minWidth = 40; preferredWidth = 100; flexibleWidth = 2; preferredHeight = 30; }
}

// Geometry is entirely project C#. Changing these vertices is hot-reloaded.
public class RedTriangle : Graphic
{
    protected override void OnPopulateMesh(VertexHelper helper)
    {
        helper.Clear(); var r = rectTransform.rect; Color32 red = Color.red;
        helper.AddVert(new Vector3(r.min.x, r.min.y, 0), red, new Vector2(0, 0));
        helper.AddVert(new Vector3(r.max.x, r.min.y, 0), red, new Vector2(1, 0));
        helper.AddVert(new Vector3((r.min.x + r.max.x) * 0.5f, r.max.y, 0), red, new Vector2(0.5f, 1));
        helper.AddTriangle(0, 1, 2);
    }
}

public class GreenImage : Image
{
    public GreenImage() { color = Color.green; }
}
