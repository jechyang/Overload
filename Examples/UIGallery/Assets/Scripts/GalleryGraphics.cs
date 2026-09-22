using Overload.Maths;
using Overload.UI;

namespace UIGallery;

public class PanelImage : Image
{
    public PanelImage() { color = new Color(0.16f, 0.18f, 0.24f, 1f); }
}

public class SoftImage : Image
{
    public SoftImage() { color = new Color(0.35f, 0.40f, 0.55f, 0.85f); }
}

public class BlueTile : Image
{
    public BlueTile() { color = new Color(0.25f, 0.55f, 0.95f, 1f); }
}

public class TealTile : Image
{
    public TealTile() { color = new Color(0.20f, 0.65f, 0.60f, 1f); }
}

public class OrangeTile : Image
{
    public OrangeTile() { color = new Color(0.90f, 0.50f, 0.25f, 1f); }
}

public class PurpleTile : Image
{
    public PurpleTile() { color = new Color(0.50f, 0.40f, 0.80f, 1f); }
}

public class SlateTile : Image
{
    public SlateTile() { color = new Color(0.35f, 0.40f, 0.55f, 0.9f); }
}

public class CoralTile : Image
{
    public CoralTile() { color = new Color(0.85f, 0.40f, 0.45f, 1f); }
}

// Custom Graphic: project C# OnPopulateMesh, hot-reloadable geometry.
public class CheckerGraphic : Graphic
{
    protected override void OnPopulateMesh(VertexHelper helper)
    {
        helper.Clear();
        Rect r = rectTransform.rect;
        const int cells = 4;
        float cw = r.width / cells, ch = r.height / 2;
        for (int y = 0; y < 2; ++y)
        for (int x = 0; x < cells; ++x)
        {
            Color32 tint = ((x + y) & 1) == 0
                ? (Color32)new Color(0.92f, 0.55f, 0.20f, 1f)
                : (Color32)new Color(0.20f, 0.22f, 0.30f, 1f);
            int i = helper.currentVertCount;
            float x0 = r.min.x + x * cw, y0 = r.min.y + y * ch;
            helper.AddVert(new Vector3(x0, y0, 0), tint, new Vector2(0, 0));
            helper.AddVert(new Vector3(x0 + cw, y0, 0), tint, new Vector2(1, 0));
            helper.AddVert(new Vector3(x0 + cw, y0 + ch, 0), tint, new Vector2(1, 1));
            helper.AddVert(new Vector3(x0, y0 + ch, 0), tint, new Vector2(0, 1));
            helper.AddTriangle(i, i + 1, i + 2);
            helper.AddTriangle(i + 2, i + 3, i);
        }
    }
}
