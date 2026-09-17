using System;
using System.Collections.Generic;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public readonly record struct Color(float r, float g, float b, float a)
{
    public static Color white => new(1, 1, 1, 1);
    public static Color black => new(0, 0, 0, 1);
    public static Color red => new(1, 0, 0, 1);
    public static Color green => new(0, 1, 0, 1);
    public static Color blue => new(0, 0, 1, 1);
    public static implicit operator Color32(Color value) => new(
        Byte(value.r), Byte(value.g), Byte(value.b), Byte(value.a));
    private static byte Byte(float value) => (byte)Math.Clamp((int)MathF.Round(value * 255), 0, 255);
}

public readonly record struct Color32(byte r, byte g, byte b, byte a)
{
    public static implicit operator Color(Color32 value) => new(value.r / 255f, value.g / 255f, value.b / 255f, value.a / 255f);
}

public struct UIVertex
{
    public Vector3 position;
    public Color32 color;
    public Vector2 uv0;
    public static UIVertex simpleVert => new() { color = new(255, 255, 255, 255) };
}

public sealed class VertexHelper : IDisposable
{
    private readonly List<UIVertex> vertices = new();
    private readonly List<int> indices = new();
    public int currentVertCount => vertices.Count;
    public int currentIndexCount => indices.Count;
    public void Clear() { vertices.Clear(); indices.Clear(); }
    public void Dispose() => Clear();
    public void AddVert(Vector3 position, Color32 color, Vector2 uv0) => vertices.Add(new UIVertex { position = position, color = color, uv0 = uv0 });
    public void AddVert(UIVertex vertex) => vertices.Add(vertex);
    public void AddTriangle(int index0, int index1, int index2) {
        if ((uint)index0 >= vertices.Count || (uint)index1 >= vertices.Count || (uint)index2 >= vertices.Count)
            throw new ArgumentOutOfRangeException(nameof(index0), "Triangle index is outside the vertex stream");
        indices.Add(index0); indices.Add(index1); indices.Add(index2);
    }
    public void AddUIVertexQuad(UIVertex[] quad) {
        if (quad == null || quad.Length != 4) throw new ArgumentException("A UI quad requires four vertices", nameof(quad));
        int start = vertices.Count; vertices.AddRange(quad);
        AddTriangle(start, start + 1, start + 2); AddTriangle(start + 2, start + 3, start);
    }
    public void AddUIVertexStream(IReadOnlyList<UIVertex> stream, IReadOnlyList<int> triangles) {
        if (stream == null || triangles == null) throw new ArgumentNullException(stream == null ? nameof(stream) : nameof(triangles));
        int start = vertices.Count;
        for (int i = 0; i < stream.Count; ++i) vertices.Add(stream[i]);
        for (int i = 0; i < triangles.Count; ++i) {
            int index = triangles[i];
            if ((uint)index >= stream.Count) throw new ArgumentOutOfRangeException(nameof(triangles));
            indices.Add(start + index);
        }
        if (triangles.Count % 3 != 0) throw new ArgumentException("Triangle index count must be divisible by three", nameof(triangles));
    }
    public void PopulateUIVertex(ref UIVertex vertex, int index) => vertex = vertices[index];
    public void SetUIVertex(UIVertex vertex, int index) => vertices[index] = vertex;
    internal UIVertex[] Vertices() => vertices.ToArray();
    internal int[] Indices() => indices.ToArray();
}

public abstract class Graphic : UIBehaviour, ICanvasElement
{
    [SerializeField] private float m_ColorR = 1, m_ColorG = 1, m_ColorB = 1, m_ColorA = 1;
    [SerializeField] private bool m_RaycastTarget = true;
    [SerializeField] private AssetRef m_Material = new(AssetKind.Unknown, "");
    [NonSerialized] private bool verticesDirty = true, materialDirty = true;
    [NonSerialized] private CanvasRenderer? cachedRenderer;

    public virtual Color color {
        get => new(m_ColorR, m_ColorG, m_ColorB, m_ColorA);
        set { if (value == color) return; m_ColorR = value.r; m_ColorG = value.g; m_ColorB = value.b; m_ColorA = value.a; SetVerticesDirty(); }
    }
    public virtual bool raycastTarget { get => m_RaycastTarget; set { if (m_RaycastTarget == value) return; m_RaycastTarget = value; SetMaterialDirty(); } }
    public virtual AssetRef material {
        get => m_Material;
        set { if (value == m_Material) return; if (value.Kind is not (AssetKind.Unknown or AssetKind.Material)) throw new ArgumentException("Graphic material must be a Material AssetRef"); m_Material = value; SetMaterialDirty(); }
    }
    public virtual AssetRef mainTexture => new(AssetKind.Unknown, "");
    public CanvasRenderer canvasRenderer => cachedRenderer ??= Owner.GetComponent<CanvasRenderer>() ?? throw new InvalidOperationException("Graphic requires CanvasRenderer");
    RectTransform ICanvasElement.transform => rectTransform;

    internal override void EnsureNativeComponents() { base.EnsureNativeComponents(); cachedRenderer = Owner.AddComponent<CanvasRenderer>(); }
    internal override void ReleaseNativeComponents() {
        try { (cachedRenderer ?? Owner.GetComponent<CanvasRenderer>())?.Clear(); } catch { }
        cachedRenderer = null;
    }
    protected override void SetDirty() => SetAllDirty();
    protected override void OnRectTransformDimensionsChange() { SetVerticesDirty(); SetLayoutDirty(); }
    protected override void OnTransformParentChanged() { base.OnTransformParentChanged(); SetAllDirty(); }
    protected override void OnCanvasHierarchyChanged() { base.OnCanvasHierarchyChanged(); SetAllDirty(); }
    public override void OnEnable() { base.OnEnable(); SetAllDirty(); }
    public override void OnDisable() { CanvasUpdateRegistry.UnRegisterCanvasElementForRebuild(this); ReleaseNativeComponents(); base.OnDisable(); }
    public override void OnDestroy() { CanvasUpdateRegistry.UnRegisterCanvasElementForRebuild(this); ReleaseNativeComponents(); base.OnDestroy(); }
    public override void OnReloadRelease() { CanvasUpdateRegistry.UnRegisterCanvasElementForRebuild(this); ReleaseNativeComponents(); base.OnReloadRelease(); }

    public virtual void SetAllDirty() { SetLayoutDirty(); SetVerticesDirty(); SetMaterialDirty(); }
    public virtual void SetLayoutDirty() { if (IsActive()) LayoutRebuilder.MarkLayoutForRebuild(rectTransform); }
    public virtual void SetVerticesDirty() {
        if (!IsActive()) return; verticesDirty = true; CanvasUpdateRegistry.RegisterCanvasElementForGraphicRebuild(this);
    }
    public virtual void SetMaterialDirty() {
        if (!IsActive()) return; materialDirty = true; CanvasUpdateRegistry.RegisterCanvasElementForGraphicRebuild(this);
    }
    public virtual void Rebuild(CanvasUpdate executing) {
        if (executing != CanvasUpdate.PreRender || !IsActive() || (!verticesDirty && !materialDirty)) return;
        using VertexHelper helper = new();
        if (rectTransform.rect.width >= 0 && rectTransform.rect.height >= 0) OnPopulateMesh(helper);
        canvasRenderer.SetMesh(helper.Vertices(), helper.Indices());
        canvasRenderer.SetTexture(mainTexture); canvasRenderer.SetMaterial(material); canvasRenderer.SetRaycastTarget(raycastTarget);
        verticesDirty = materialDirty = false;
    }
    protected virtual void OnPopulateMesh(VertexHelper helper) {
        var rect = rectTransform.rect; Color32 tint = color;
        helper.Clear();
        helper.AddVert(new Vector3(rect.min.x, rect.min.y, 0), tint, new Vector2(0, 0));
        helper.AddVert(new Vector3(rect.min.x, rect.max.y, 0), tint, new Vector2(0, 1));
        helper.AddVert(new Vector3(rect.max.x, rect.max.y, 0), tint, new Vector2(1, 1));
        helper.AddVert(new Vector3(rect.max.x, rect.min.y, 0), tint, new Vector2(1, 0));
        helper.AddTriangle(0, 1, 2); helper.AddTriangle(2, 3, 0);
    }
    public virtual void SetNativeSize() { }
    public void LayoutComplete() { }
    public void GraphicUpdateComplete() { }
}

public class Image : Graphic
{
    [SerializeField] private AssetRef m_Sprite = new(AssetKind.Unknown, "");
    public AssetRef sprite {
        get => m_Sprite;
        set { if (value.Kind is not (AssetKind.Unknown or AssetKind.Texture)) throw new ArgumentException("Image sprite must reference a texture"); if (value == m_Sprite) return; m_Sprite = value; SetAllDirty(); }
    }
    public override AssetRef mainTexture => m_Sprite;
}

public class RawImage : Graphic
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
