using System;
using Overload.Maths;

namespace Overload.UI;

public class Image : MaskableGraphic
{
    [SerializeField] private AssetRef m_Sprite = new(AssetKind.Unknown, "");
    public AssetRef sprite {
        get => m_Sprite;
        set { if (value.Kind is not (AssetKind.Unknown or AssetKind.Texture)) throw new ArgumentException("Image sprite must reference a texture"); if (value == m_Sprite) return; m_Sprite = value; SetAllDirty(); }
    }
    public override AssetRef mainTexture => m_Sprite;
}

public class RawImage : MaskableGraphic
{
    [SerializeField] private AssetRef m_Texture = new(AssetKind.Unknown, "");
    [SerializeField] private float m_UVMinX, m_UVMinY, m_UVWidth = 1, m_UVHeight = 1;
    public AssetRef texture {
        get => m_Texture;
        set { if (value.Kind is not (AssetKind.Unknown or AssetKind.Texture)) throw new ArgumentException("RawImage texture must reference a texture"); if (value == m_Texture) return; m_Texture = value; SetAllDirty(); }
    }
    public Rect uvRect {
        get => new(new(m_UVMinX, m_UVMinY), new(m_UVWidth, m_UVHeight));
        set { m_UVMinX = value.min.x; m_UVMinY = value.min.y; m_UVWidth = value.size.x; m_UVHeight = value.size.y; SetVerticesDirty(); }
    }
    public override AssetRef mainTexture => m_Texture;
    protected override void OnPopulateMesh(VertexHelper helper) {
        Rect rect = rectTransform.rect, uv = uvRect; Color32 tint = color;
        helper.Clear();
        helper.AddVert(new Vector3(rect.min.x, rect.min.y, 0), tint, uv.min);
        helper.AddVert(new Vector3(rect.min.x, rect.max.y, 0), tint, new Vector2(uv.min.x, uv.max.y));
        helper.AddVert(new Vector3(rect.max.x, rect.max.y, 0), tint, uv.max);
        helper.AddVert(new Vector3(rect.max.x, rect.min.y, 0), tint, new Vector2(uv.max.x, uv.min.y));
        helper.AddTriangle(0, 1, 2); helper.AddTriangle(2, 3, 0);
    }
}
