using System;
using Overload.Maths;

namespace Overload.UI;

/// <summary>
/// Sprite slice/UV metadata in texture pixels. Overload keeps this on the Image
/// (or a thin project subclass) until a dedicated Sprite asset type is added;
/// C# still owns all Image mesh generation.
/// </summary>
public enum ImageType { Simple, Sliced, Tiled, Filled }
public enum FillMethod { Horizontal, Vertical, Radial90, Radial180, Radial360 }

public class Image : MaskableGraphic, ILayoutElement, ICanvasRaycastFilter
{
    [SerializeField] private AssetRef m_Sprite = new(AssetKind.Unknown, "");
    [SerializeField] private ImageType m_Type;
    [SerializeField] private bool m_PreserveAspect, m_FillCenter = true, m_UseSpriteMesh;
    [SerializeField] private FillMethod m_FillMethod = FillMethod.Radial360;
    [SerializeField] private int m_FillOrigin;
    [SerializeField] private float m_FillAmount = 1;
    [SerializeField] private bool m_FillClockwise = true;
    [SerializeField] private float m_AlphaHitTestMinimumThreshold;
    [SerializeField] private float m_PixelsPerUnitMultiplier = 1;
    // Sprite rect / border / pivot in source-texture pixels (Unity Sprite-equivalent).
    [SerializeField] private float m_SpriteRectX, m_SpriteRectY, m_SpriteRectW = 100, m_SpriteRectH = 100;
    [SerializeField] private float m_BorderL, m_BorderB, m_BorderR, m_BorderT;
    [SerializeField] private float m_PivotX = 0.5f, m_PivotY = 0.5f;
    [SerializeField] private float m_TextureWidth = 100, m_TextureHeight = 100;
    [SerializeField] private float m_SpritePixelsPerUnit = 100;
    [SerializeField] private float m_PadL, m_PadB, m_PadR, m_PadT;

    public AssetRef sprite {
        get => m_Sprite;
        set {
            if (value.Kind is not (AssetKind.Unknown or AssetKind.Texture))
                throw new ArgumentException("Image sprite must reference a texture");
            if (value == m_Sprite) return;
            m_Sprite = value; SetAllDirty();
        }
    }
    public override AssetRef mainTexture => m_Sprite;
    public ImageType type { get => m_Type; set { if (m_Type == value) return; m_Type = value; SetVerticesDirty(); } }
    public bool preserveAspect { get => m_PreserveAspect; set { if (m_PreserveAspect == value) return; m_PreserveAspect = value; SetVerticesDirty(); } }
    public bool fillCenter { get => m_FillCenter; set { if (m_FillCenter == value) return; m_FillCenter = value; SetVerticesDirty(); } }
    public bool useSpriteMesh { get => m_UseSpriteMesh; set { if (m_UseSpriteMesh == value) return; m_UseSpriteMesh = value; SetVerticesDirty(); } }
    public FillMethod fillMethod {
        get => m_FillMethod;
        set {
            if (m_FillMethod == value) return;
            m_FillMethod = value; m_FillOrigin = 0; SetVerticesDirty();
        }
    }
    public int fillOrigin {
        get => m_FillOrigin;
        set {
            int max = m_FillMethod switch {
                FillMethod.Horizontal or FillMethod.Vertical => 1,
                FillMethod.Radial90 => 3,
                FillMethod.Radial180 => 3,
                _ => 3
            };
            if (value < 0 || value > max) throw new ArgumentOutOfRangeException(nameof(value));
            if (m_FillOrigin == value) return;
            m_FillOrigin = value; SetVerticesDirty();
        }
    }
    public float fillAmount {
        get => m_FillAmount;
        set {
            if (!float.IsFinite(value)) throw new ArgumentException("fillAmount must be finite");
            value = Math.Clamp(value, 0, 1);
            if (m_FillAmount == value) return;
            m_FillAmount = value; SetVerticesDirty();
        }
    }
    public bool fillClockwise { get => m_FillClockwise; set { if (m_FillClockwise == value) return; m_FillClockwise = value; SetVerticesDirty(); } }
    public float alphaHitTestMinimumThreshold {
        get => m_AlphaHitTestMinimumThreshold;
        set {
            if (!float.IsFinite(value) || value < 0 || value > 1) throw new ArgumentOutOfRangeException(nameof(value));
            m_AlphaHitTestMinimumThreshold = value;
        }
    }
    public float pixelsPerUnitMultiplier {
        get => m_PixelsPerUnitMultiplier;
        set {
            if (!float.IsFinite(value) || value <= 0) throw new ArgumentOutOfRangeException(nameof(value));
            if (m_PixelsPerUnitMultiplier == value) return;
            m_PixelsPerUnitMultiplier = value; SetAllDirty();
        }
    }
    public Rect spriteRect {
        get => new(new(m_SpriteRectX, m_SpriteRectY), new(m_SpriteRectW, m_SpriteRectH));
        set {
            if (value.size.x <= 0 || value.size.y <= 0) throw new ArgumentException("spriteRect must be positive");
            m_SpriteRectX = value.min.x; m_SpriteRectY = value.min.y;
            m_SpriteRectW = value.size.x; m_SpriteRectH = value.size.y;
            SetAllDirty();
        }
    }
    /// <summary>Sprite border in texture pixels: left, bottom, right, top.</summary>
    public Vector4 border {
        get => new(m_BorderL, m_BorderB, m_BorderR, m_BorderT);
        set {
            if (value.X < 0 || value.Y < 0 || value.Z < 0 || value.W < 0) throw new ArgumentException("border must be non-negative");
            m_BorderL = value.X; m_BorderB = value.Y; m_BorderR = value.Z; m_BorderT = value.W;
            SetVerticesDirty();
        }
    }
    public Vector2 spritePivot {
        get => new(m_PivotX, m_PivotY);
        set {
            if (!float.IsFinite(value.x) || !float.IsFinite(value.y)) throw new ArgumentException("pivot must be finite");
            m_PivotX = Math.Clamp(value.x, 0, 1); m_PivotY = Math.Clamp(value.y, 0, 1);
            SetVerticesDirty();
        }
    }
    public Vector2 textureSize {
        get => new(m_TextureWidth, m_TextureHeight);
        set {
            if (value.x <= 0 || value.y <= 0) throw new ArgumentException("textureSize must be positive");
            m_TextureWidth = value.x; m_TextureHeight = value.y; SetVerticesDirty();
        }
    }
    public Vector4 spritePadding {
        get => new(m_PadL, m_PadB, m_PadR, m_PadT);
        set {
            if (value.X < 0 || value.Y < 0 || value.Z < 0 || value.W < 0) throw new ArgumentException("padding must be non-negative");
            m_PadL = value.X; m_PadB = value.Y; m_PadR = value.Z; m_PadT = value.W; SetVerticesDirty();
        }
    }
    public float spritePixelsPerUnit {
        get => m_SpritePixelsPerUnit;
        set {
            if (!float.IsFinite(value) || value <= 0) throw new ArgumentOutOfRangeException(nameof(value));
            m_SpritePixelsPerUnit = value; SetAllDirty();
        }
    }
    public bool hasBorder => m_BorderL > 0 || m_BorderB > 0 || m_BorderR > 0 || m_BorderT > 0;
    /// <summary>sprite PPU / 100 * multiplier (canvas reference PPU is applied via CanvasScaler).</summary>
    public override float pixelsPerUnit => m_SpritePixelsPerUnit / 100f * m_PixelsPerUnitMultiplier;

    private Vector4 OuterUv {
        get {
            if (m_TextureWidth <= 0 || m_TextureHeight <= 0) return new(0, 0, 1, 1);
            return new(
                m_SpriteRectX / m_TextureWidth,
                m_SpriteRectY / m_TextureHeight,
                (m_SpriteRectX + m_SpriteRectW) / m_TextureWidth,
                (m_SpriteRectY + m_SpriteRectH) / m_TextureHeight);
        }
    }
    private Vector4 InnerUv {
        get {
            if (m_TextureWidth <= 0 || m_TextureHeight <= 0) return new(0, 0, 1, 1);
            float x0 = (m_SpriteRectX + m_BorderL) / m_TextureWidth;
            float y0 = (m_SpriteRectY + m_BorderB) / m_TextureHeight;
            float x1 = (m_SpriteRectX + m_SpriteRectW - m_BorderR) / m_TextureWidth;
            float y1 = (m_SpriteRectY + m_SpriteRectH - m_BorderT) / m_TextureHeight;
            return new(x0, y0, x1, y1);
        }
    }

    public override void SetNativeSize() {
        if (m_SpriteRectW <= 0 || m_SpriteRectH <= 0) return;
        float w = m_SpriteRectW / pixelsPerUnit, h = m_SpriteRectH / pixelsPerUnit;
        rectTransform.anchorMax = rectTransform.anchorMin;
        rectTransform.sizeDelta = new Vector2(w, h);
        SetAllDirty();
    }

    public void CalculateLayoutInputHorizontal() { }
    public void CalculateLayoutInputVertical() { }
    public float minWidth => 0;
    public float minHeight => 0;
    public float preferredWidth => m_SpriteRectW <= 0 ? 0
        : (m_Type is ImageType.Sliced or ImageType.Tiled
            ? (m_SpriteRectW - m_BorderL - m_BorderR) / pixelsPerUnit
            : m_SpriteRectW / pixelsPerUnit);
    public float preferredHeight => m_SpriteRectH <= 0 ? 0
        : (m_Type is ImageType.Sliced or ImageType.Tiled
            ? (m_SpriteRectH - m_BorderB - m_BorderT) / pixelsPerUnit
            : m_SpriteRectH / pixelsPerUnit);
    public float flexibleWidth => -1;
    public float flexibleHeight => -1;
    public int layoutPriority => 0;

    public bool IsRaycastLocationValid(Vector2 screenPoint, Actor? eventCamera) {
        if (m_AlphaHitTestMinimumThreshold <= 0) return true;
        if (!rectTransform.rect.Contains(screenPoint)) return false;
        // Without a CPU-readable sprite, treat threshold as a full-rect hit only
        // when threshold is 0. Readable-texture sampling is a later platform pass.
        return false;
    }

    protected override void OnPopulateMesh(VertexHelper helper) {
        if (m_SpriteRectW <= 0 || m_SpriteRectH <= 0 || m_Type == ImageType.Simple && !hasBorder && m_Sprite.Path.Length == 0) {
            base.OnPopulateMesh(helper);
            return;
        }
        switch (m_Type) {
            case ImageType.Simple:
                GenerateSimpleSprite(helper, m_PreserveAspect);
                break;
            case ImageType.Sliced:
                GenerateSlicedSprite(helper);
                break;
            case ImageType.Tiled:
                GenerateTiledSprite(helper);
                break;
            case ImageType.Filled:
                GenerateFilledSprite(helper, m_PreserveAspect);
                break;
        }
    }

    private Vector4 GetDrawingDimensions(bool shouldPreserveAspect) {
        Vector4 padding = new(m_PadL, m_PadB, m_PadR, m_PadT);
        float sizeX = m_SpriteRectW, sizeY = m_SpriteRectH;
        Rect r = rectTransform.rect;
        if (shouldPreserveAspect && sizeX > 0 && sizeY > 0) {
            float spriteRatio = sizeX / sizeY, rectRatio = r.width / r.height;
            if (spriteRatio > rectRatio) {
                float old = r.width;
                float width = r.height * spriteRatio;
                r = new Rect(new Vector2(r.min.x + (old - width) * rectTransform.pivot.x, r.min.y), new Vector2(width, r.size.y));
            } else if (spriteRatio < rectRatio) {
                float old = r.height;
                float height = r.width / spriteRatio;
                r = new Rect(new Vector2(r.min.x, r.min.y + (old - height) * rectTransform.pivot.y), new Vector2(r.size.x, height));
            }
        }
        float spriteW = MathF.Max(1, MathF.Round(sizeX)), spriteH = MathF.Max(1, MathF.Round(sizeY));
        var v = new Vector4(
            padding.X / spriteW,
            padding.Y / spriteH,
            (spriteW - padding.Z) / spriteW,
            (spriteH - padding.W) / spriteH);
        return new(
            r.min.x + r.size.x * v.X,
            r.min.y + r.size.y * v.Y,
            r.min.x + r.size.x * v.Z,
            r.min.y + r.size.y * v.W);
    }

    private void GenerateSimpleSprite(VertexHelper vh, bool lPreserveAspect) {
        Vector4 v = GetDrawingDimensions(lPreserveAspect);
        Vector4 uv = OuterUv;
        Color32 tint = color;
        vh.Clear();
        vh.AddVert(new Vector3(v.X, v.Y, 0), tint, new Vector2(uv.X, uv.Y));
        vh.AddVert(new Vector3(v.X, v.W, 0), tint, new Vector2(uv.X, uv.W));
        vh.AddVert(new Vector3(v.Z, v.W, 0), tint, new Vector2(uv.Z, uv.W));
        vh.AddVert(new Vector3(v.Z, v.Y, 0), tint, new Vector2(uv.Z, uv.Y));
        vh.AddTriangle(0, 1, 2);
        vh.AddTriangle(2, 3, 0);
    }

    private Vector4 GetAdjustedBorders(Vector4 border, Rect adjustedRect) {
        Rect originalRect = rectTransform.rect;
        // border: x=left, y=bottom, z=right, w=top  (Maths.Vector4)
        Span<float> b = stackalloc float[4];
        b[0] = border.X; b[1] = border.Y; b[2] = border.Z; b[3] = border.W;
        for (int axis = 0; axis <= 1; axis++) {
            float original = axis == 0 ? originalRect.size.x : originalRect.size.y;
            float adjusted = axis == 0 ? adjustedRect.size.x : adjustedRect.size.y;
            if (original != 0) {
                float ratio = adjusted / original;
                b[axis] *= ratio;
                b[axis + 2] *= ratio;
            }
            float combined = b[axis] + b[axis + 2];
            if (adjusted < combined && combined != 0) {
                float ratio = adjusted / combined;
                b[axis] *= ratio;
                b[axis + 2] *= ratio;
            }
        }
        return new(b[0], b[1], b[2], b[3]);
    }

    private void GenerateSlicedSprite(VertexHelper toFill) {
        if (!hasBorder) {
            GenerateSimpleSprite(toFill, false);
            return;
        }
        Vector4 outer = OuterUv, inner = InnerUv;
        Vector4 padding = new(m_PadL, m_PadB, m_PadR, m_PadT);
        Vector4 border = this.border;
        Rect rect = rectTransform.rect;
        float ppu = MathF.Max(1e-4f, pixelsPerUnit);
        Vector4 adjustedBorders = GetAdjustedBorders(new(border.X / ppu, border.Y / ppu, border.Z / ppu, border.W / ppu), rect);
        padding = new(padding.X / ppu, padding.Y / ppu, padding.Z / ppu, padding.W / ppu);

        Span<float> vx = stackalloc float[4], vy = stackalloc float[4];
        Span<float> ux = stackalloc float[4], uy = stackalloc float[4];
        vx[0] = padding.X; vy[0] = padding.Y;
        vx[3] = rect.width - padding.Z; vy[3] = rect.height - padding.W;
        vx[1] = adjustedBorders.X; vy[1] = adjustedBorders.Y;
        vx[2] = rect.width - adjustedBorders.Z; vy[2] = rect.height - adjustedBorders.W;
        for (int i = 0; i < 4; ++i) {
            vx[i] += rect.min.x;
            vy[i] += rect.min.y;
        }
        ux[0] = outer.X; uy[0] = outer.Y;
        ux[1] = inner.X; uy[1] = inner.Y;
        ux[2] = inner.Z; uy[2] = inner.W;
        ux[3] = outer.Z; uy[3] = outer.W;
        toFill.Clear();
        for (int x = 0; x < 3; ++x) {
            int x2 = x + 1;
            for (int y = 0; y < 3; ++y) {
                if (!m_FillCenter && x == 1 && y == 1) continue;
                int y2 = y + 1;
                AddQuad(toFill,
                    new Vector2(vx[x], vy[y]), new Vector2(vx[x2], vy[y2]),
                    color,
                    new Vector2(ux[x], uy[y]), new Vector2(ux[x2], uy[y2]));
            }
        }
    }

    private void GenerateTiledSprite(VertexHelper toFill) {
        Vector4 outer = OuterUv, inner = InnerUv, border = this.border;
        float spriteW = m_SpriteRectW, spriteH = m_SpriteRectH;
        Rect rect = rectTransform.rect;
        float ppu = MathF.Max(1e-4f, pixelsPerUnit);
        float tileWidth = (spriteW - border.X - border.Z) / ppu;
        float tileHeight = (spriteH - border.Y - border.W) / ppu;
        border = GetAdjustedBorders(new(border.X / ppu, border.Y / ppu, border.Z / ppu, border.W / ppu), rect);
        Vector2 uvMin = new(inner.X, inner.Y), uvMax = new(inner.Z, inner.W);
        float xMin = border.X, xMax = rect.width - border.Z;
        float yMin = border.Y, yMax = rect.height - border.W;
        toFill.Clear();
        if (tileWidth <= 0) tileWidth = xMax - xMin;
        if (tileHeight <= 0) tileHeight = yMax - yMin;

        // Mesh-quad tiling (borders or non-repeat wrap). Cap tile counts to stay
        // under the 65535 vertex budget like the reference implementation.
        long nTilesW = 0, nTilesH = 0;
        if (m_FillCenter) {
            nTilesW = (long)Math.Ceiling((xMax - xMin) / tileWidth);
            nTilesH = (long)Math.Ceiling((yMax - yMin) / tileHeight);
            double nVertices = hasBorder ? (nTilesW + 2.0) * (nTilesH + 2.0) * 4.0 : nTilesW * nTilesH * 4.0;
            if (nVertices > 65000.0) {
                double maxTiles = 65000.0 / 4.0;
                double imageRatio = hasBorder ? (nTilesW + 2.0) / (nTilesH + 2.0) : (double)nTilesW / Math.Max(1, nTilesH);
                double imageRatio2 = 1.0 / Math.Max(1e-6, imageRatio);
                nTilesW = (long)Math.Ceiling(Math.Sqrt(maxTiles * imageRatio));
                nTilesH = (long)Math.Ceiling(Math.Sqrt(maxTiles * imageRatio2));
                if (nTilesW < 1) nTilesW = 1;
                if (nTilesH < 1) nTilesH = 1;
                tileWidth = (xMax - xMin) / nTilesW;
                tileHeight = (yMax - yMin) / nTilesH;
            }
        }

        if (m_FillCenter) {
            for (long y = 0; y < nTilesH; ++y) {
                float y1 = yMin + y * tileHeight, y2 = Math.Min(yMax, y1 + tileHeight);
                if (y1 >= y2) continue;
                for (long x = 0; x < nTilesW; ++x) {
                    float x1 = xMin + x * tileWidth, x2 = Math.Min(xMax, x1 + tileWidth);
                    if (x1 >= x2) continue;
                    Vector2 clipped = uvMax;
                    if (x2 - x1 < tileWidth) clipped = new Vector2(uvMin.x + (uvMax.x - uvMin.x) * ((x2 - x1) / tileWidth), clipped.y);
                    if (y2 - y1 < tileHeight) clipped = new Vector2(clipped.x, uvMin.y + (uvMax.y - uvMin.y) * ((y2 - y1) / tileHeight));
                    AddQuad(toFill, new Vector2(rect.min.x + x1, rect.min.y + y1), new Vector2(rect.min.x + x2, rect.min.y + y2), color, uvMin, clipped);
                }
            }
        }
        if (hasBorder) {
            // Four edges + four corners, using outer/inner UV like the reference.
            float adjustedL = border.X, adjustedB = border.Y, adjustedR = border.Z, adjustedT = border.W;
            void Edge(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1) =>
                AddQuad(toFill, new Vector2(rect.min.x + x0, rect.min.y + y0), new Vector2(rect.min.x + x1, rect.min.y + y1),
                    color, new Vector2(u0, v0), new Vector2(u1, v1));
            // bottom edge
            Edge(xMin, 0, xMax, adjustedB, inner.X, outer.Y, inner.Z, inner.Y);
            // top edge
            Edge(xMin, rect.height - adjustedT, xMax, rect.height, inner.X, inner.W, inner.Z, outer.W);
            // left edge
            Edge(0, yMin, adjustedL, yMax, outer.X, inner.Y, inner.X, inner.W);
            // right edge
            Edge(rect.width - adjustedR, yMin, rect.width, yMax, inner.Z, inner.Y, outer.Z, inner.W);
            // corners
            Edge(0, 0, adjustedL, adjustedB, outer.X, outer.Y, inner.X, inner.Y);
            Edge(rect.width - adjustedR, 0, rect.width, adjustedB, inner.Z, outer.Y, outer.Z, inner.Y);
            Edge(0, rect.height - adjustedT, adjustedL, rect.height, outer.X, inner.W, inner.X, outer.W);
            Edge(rect.width - adjustedR, rect.height - adjustedT, rect.width, rect.height, inner.Z, inner.W, outer.Z, outer.W);
        }
    }

    private void GenerateFilledSprite(VertexHelper toFill, bool preserveAspect) {
        toFill.Clear();
        if (m_FillAmount < 0.001f) return;
        Vector4 v = GetDrawingDimensions(preserveAspect);
        Vector4 outer = OuterUv;
        float tx0 = outer.X, ty0 = outer.Y, tx1 = outer.Z, ty1 = outer.W;
        if (m_FillMethod is FillMethod.Horizontal or FillMethod.Vertical) {
            if (m_FillMethod == FillMethod.Horizontal) {
                float fill = (tx1 - tx0) * m_FillAmount;
                if (m_FillOrigin == 1) {
                    v = new(v.Z - (v.Z - v.X) * m_FillAmount, v.Y, v.Z, v.W);
                    tx0 = tx1 - fill;
                } else {
                    v = new(v.X, v.Y, v.X + (v.Z - v.X) * m_FillAmount, v.W);
                    tx1 = tx0 + fill;
                }
            } else {
                float fill = (ty1 - ty0) * m_FillAmount;
                if (m_FillOrigin == 1) {
                    v = new(v.X, v.W - (v.W - v.Y) * m_FillAmount, v.Z, v.W);
                    ty0 = ty1 - fill;
                } else {
                    v = new(v.X, v.Y, v.Z, v.Y + (v.W - v.Y) * m_FillAmount);
                    ty1 = ty0 + fill;
                }
            }
        }
        Span<float> xy = stackalloc float[8], uv = stackalloc float[8];
        xy[0] = v.X; xy[1] = v.Y;
        xy[2] = v.X; xy[3] = v.W;
        xy[4] = v.Z; xy[5] = v.W;
        xy[6] = v.Z; xy[7] = v.Y;
        uv[0] = tx0; uv[1] = ty0;
        uv[2] = tx0; uv[3] = ty1;
        uv[4] = tx1; uv[5] = ty1;
        uv[6] = tx1; uv[7] = ty0;
        if (m_FillAmount < 1f && m_FillMethod is not (FillMethod.Horizontal or FillMethod.Vertical)) {
            if (m_FillMethod == FillMethod.Radial90) {
                if (RadialCut(xy, uv, m_FillAmount, m_FillClockwise, m_FillOrigin)) AddQuad(toFill, xy, color, uv);
            } else if (m_FillMethod == FillMethod.Radial180) {
                for (int side = 0; side < 2; ++side) {
                    float fx0, fx1, fy0, fy1;
                    int even = m_FillOrigin > 1 ? 1 : 0;
                    if (m_FillOrigin == 0 || m_FillOrigin == 2) {
                        fy0 = 0f; fy1 = 1f;
                        if (side == even) { fx0 = 0f; fx1 = 0.5f; } else { fx0 = 0.5f; fx1 = 1f; }
                    } else {
                        fx0 = 0f; fx1 = 1f;
                        if (side == even) { fy0 = 0f; fy1 = 0.5f; } else { fy0 = 0.5f; fy1 = 1f; }
                    }
                    FillQuadrant(xy, uv, v, outer, fx0, fy0, fx1, fy1);
                    float val = m_FillAmount * 2f - side;
                    if (RadialCut(xy, uv, Math.Clamp(val, 0, 1), m_FillClockwise, (side + m_FillOrigin + 3) % 4))
                        AddQuad(toFill, xy, color, uv);
                }
            } else { // Radial360
                for (int corner = 0; corner < 4; ++corner) {
                    float fx0, fx1, fy0, fy1;
                    if (corner < 2) { fx0 = 0f; fx1 = 0.5f; } else { fx0 = 0.5f; fx1 = 1f; }
                    if (corner is 0 or 3) { fy0 = 0f; fy1 = 0.5f; } else { fy0 = 0.5f; fy1 = 1f; }
                    FillQuadrant(xy, uv, v, outer, fx0, fy0, fx1, fy1);
                    float val = m_FillClockwise
                        ? m_FillAmount * 4f - ((corner + m_FillOrigin) % 4)
                        : m_FillAmount * 4f - (3 - ((corner + m_FillOrigin) % 4));
                    if (RadialCut(xy, uv, Math.Clamp(val, 0, 1), m_FillClockwise, (corner + 2) % 4))
                        AddQuad(toFill, xy, color, uv);
                }
            }
        } else {
            AddQuad(toFill, xy, color, uv);
        }
    }

    private static void FillQuadrant(Span<float> xy, Span<float> uv, Vector4 v, Vector4 outer, float fx0, float fy0, float fx1, float fy1) {
        float x0 = v.X + (v.Z - v.X) * fx0, x1 = v.X + (v.Z - v.X) * fx1;
        float y0 = v.Y + (v.W - v.Y) * fy0, y1 = v.Y + (v.W - v.Y) * fy1;
        float u0 = outer.X + (outer.Z - outer.X) * fx0, u1 = outer.X + (outer.Z - outer.X) * fx1;
        float w0 = outer.Y + (outer.W - outer.Y) * fy0, w1 = outer.Y + (outer.W - outer.Y) * fy1;
        xy[0] = x0; xy[1] = y0; xy[2] = x0; xy[3] = y1; xy[4] = x1; xy[5] = y1; xy[6] = x1; xy[7] = y0;
        uv[0] = u0; uv[1] = w0; uv[2] = u0; uv[3] = w1; uv[4] = u1; uv[5] = w1; uv[6] = u1; uv[7] = w0;
    }

    private static void AddQuad(VertexHelper vh, Span<float> xy, Color32 tint, Span<float> uv) {
        int start = vh.currentVertCount;
        vh.AddVert(new Vector3(xy[0], xy[1], 0), tint, new Vector2(uv[0], uv[1]));
        vh.AddVert(new Vector3(xy[2], xy[3], 0), tint, new Vector2(uv[2], uv[3]));
        vh.AddVert(new Vector3(xy[4], xy[5], 0), tint, new Vector2(uv[4], uv[5]));
        vh.AddVert(new Vector3(xy[6], xy[7], 0), tint, new Vector2(uv[6], uv[7]));
        vh.AddTriangle(start, start + 1, start + 2);
        vh.AddTriangle(start + 2, start + 3, start);
    }

    private static void AddQuad(VertexHelper vh, Vector2 posMin, Vector2 posMax, Color32 tint, Vector2 uvMin, Vector2 uvMax) {
        int start = vh.currentVertCount;
        vh.AddVert(new Vector3(posMin.x, posMin.y, 0), tint, new Vector2(uvMin.x, uvMin.y));
        vh.AddVert(new Vector3(posMin.x, posMax.y, 0), tint, new Vector2(uvMin.x, uvMax.y));
        vh.AddVert(new Vector3(posMax.x, posMax.y, 0), tint, new Vector2(uvMax.x, uvMax.y));
        vh.AddVert(new Vector3(posMax.x, posMin.y, 0), tint, new Vector2(uvMax.x, uvMin.y));
        vh.AddTriangle(start, start + 1, start + 2);
        vh.AddTriangle(start + 2, start + 3, start);
    }

    private static bool RadialCut(Span<float> xy, Span<float> uv, float fill, bool invert, int corner) {
        if (fill < 0.001f) return false;
        if ((corner & 1) == 1) invert = !invert;
        if (!invert && fill > 0.999f) return true;
        float angle = Math.Clamp(fill, 0, 1);
        if (invert) angle = 1f - angle;
        angle *= 90f * (MathF.PI / 180f);
        float cos = MathF.Cos(angle), sin = MathF.Sin(angle);
        RadialCut(xy, cos, sin, invert, corner);
        RadialCut(uv, cos, sin, invert, corner);
        return true;
    }

    private static void RadialCut(Span<float> data, float cos, float sin, bool invert, int corner) {
        // data is 4 corners as pairs (x,y) or (u,v) at indices 0..7.
        int i0 = corner, i1 = (corner + 1) % 4, i2 = (corner + 2) % 4, i3 = (corner + 3) % 4;
        static void SetX(Span<float> d, int corner, float v) => d[corner * 2] = v;
        static void SetY(Span<float> d, int corner, float v) => d[corner * 2 + 1] = v;
        static float GetX(Span<float> d, int corner) => d[corner * 2];
        static float GetY(Span<float> d, int corner) => d[corner * 2 + 1];
        static float Lerp(float a, float b, float t) => a + (b - a) * t;
        if ((corner & 1) == 1) {
            if (sin > cos) {
                cos /= sin; sin = 1f;
                if (invert) {
                    SetX(data, i1, Lerp(GetX(data, i0), GetX(data, i2), cos));
                    SetX(data, i2, GetX(data, i1));
                }
            } else if (cos > sin) {
                sin /= cos; cos = 1f;
                if (!invert) {
                    SetY(data, i2, Lerp(GetY(data, i0), GetY(data, i2), sin));
                    SetY(data, i3, GetY(data, i2));
                }
            } else {
                cos = 1f; sin = 1f;
            }
            if (!invert) SetX(data, i3, Lerp(GetX(data, i0), GetX(data, i2), cos));
            else SetY(data, i1, Lerp(GetY(data, i0), GetY(data, i2), sin));
        } else {
            if (cos > sin) {
                sin /= cos; cos = 1f;
                if (!invert) {
                    SetY(data, i1, Lerp(GetY(data, i0), GetY(data, i2), sin));
                    SetY(data, i2, GetY(data, i1));
                }
            } else if (sin > cos) {
                cos /= sin; sin = 1f;
                if (invert) {
                    SetX(data, i2, Lerp(GetX(data, i0), GetX(data, i2), cos));
                    SetX(data, i3, GetX(data, i2));
                }
            } else {
                cos = 1f; sin = 1f;
            }
            if (invert) SetY(data, i3, Lerp(GetY(data, i0), GetY(data, i2), sin));
            else SetX(data, i1, Lerp(GetX(data, i0), GetX(data, i2), cos));
        }
    }
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
    public override void SetNativeSize() {
        // RawImage has no intrinsic size; keep current rect.
    }
}
