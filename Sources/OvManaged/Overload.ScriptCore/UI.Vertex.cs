using System;
using System.Collections.Generic;
using Overload.Maths;

namespace Overload.UI;

public struct UIVertex
{
    public Vector3 position;
    public Color32 color;
    public Vector2 uv0, uv1, uv2, uv3;
    public Vector3 normal;
    public Vector4 tangent;
    public static UIVertex simpleVert => new() { color = new(255,255,255,255), normal = new(0,0,-1), tangent = new(1,0,0,-1) };
}

/// <summary>Owned CPU mesh. SetMesh and FillMesh copy their inputs; no borrowed arrays cross the native boundary.</summary>
public sealed class UIMesh
{
    internal UIVertex[] vertices = Array.Empty<UIVertex>();
    internal int[] indices = Array.Empty<int>();
    public int vertexCount => vertices.Length;
    public int indexCount => indices.Length;
    public UIVertex[] GetVertices() => (UIVertex[])vertices.Clone();
    public int[] GetIndices() => (int[])indices.Clone();
    public void Clear() { vertices = Array.Empty<UIVertex>(); indices = Array.Empty<int>(); }
}

public sealed class VertexHelper : IDisposable
{
    public const int MaximumVertices = 65535, MaximumIndices = MaximumVertices * 6;
    private readonly List<UIVertex> vertices = new();
    private readonly List<int> indices = new();
    public VertexHelper() { }
    public VertexHelper(UIMesh mesh) { ArgumentNullException.ThrowIfNull(mesh); AddUIVertexStream(mesh.vertices, mesh.indices); }
    public int currentVertCount => vertices.Count;
    public int currentIndexCount => indices.Count;
    public void Clear() { vertices.Clear(); indices.Clear(); }
    public void Dispose() => Clear();
    internal static void Validate(UIVertex v) {
        static void F(float x) { if (!float.IsFinite(x)) throw new ArgumentException("UI vertex channels must be finite"); }
        F(v.position.X); F(v.position.Y); F(v.position.Z); F(v.normal.X); F(v.normal.Y); F(v.normal.Z);
        F(v.tangent.X); F(v.tangent.Y); F(v.tangent.Z); F(v.tangent.W);
        F(v.uv0.x); F(v.uv0.y); F(v.uv1.x); F(v.uv1.y); F(v.uv2.x); F(v.uv2.y); F(v.uv3.x); F(v.uv3.y);
    }
    private void Capacity(int v, int i) {
        if (v < 0 || i < 0 || v > MaximumVertices - vertices.Count || i > MaximumIndices - indices.Count)
            throw new ArgumentException("UI mesh exceeds vertex/index limits");
    }
    public void AddVert(Vector3 position, Color32 color, Vector2 uv0) {
        var v = UIVertex.simpleVert; v.position = position; v.color = color; v.uv0 = uv0; AddVert(v);
    }
    public void AddVert(Vector3 position, Color32 color, Vector2 uv0, Vector2 uv1, Vector3 normal, Vector4 tangent) =>
        AddVert(new UIVertex { position=position, color=color, uv0=uv0, uv1=uv1, normal=normal, tangent=tangent });
    public void AddVert(Vector3 position, Color32 color, Vector2 uv0, Vector2 uv1, Vector2 uv2, Vector2 uv3, Vector3 normal, Vector4 tangent) =>
        AddVert(new UIVertex { position=position, color=color, uv0=uv0, uv1=uv1, uv2=uv2, uv3=uv3, normal=normal, tangent=tangent });
    public void AddVert(UIVertex vertex) { Validate(vertex); Capacity(1,0); vertices.Add(vertex); }
    public void AddTriangle(int a, int b, int c) {
        if ((uint)a >= vertices.Count || (uint)b >= vertices.Count || (uint)c >= vertices.Count) throw new ArgumentOutOfRangeException(nameof(a));
        Capacity(0,3); indices.Add(a); indices.Add(b); indices.Add(c);
    }
    public void AddUIVertexQuad(UIVertex[] quad) {
        if (quad == null || quad.Length != 4) throw new ArgumentException("A quad requires four vertices", nameof(quad));
        AddUIVertexStream(quad, new[] { 0,1,2,2,3,0 });
    }
    public void AddUIVertexStream(IReadOnlyList<UIVertex> stream, IReadOnlyList<int> triangles) {
        ArgumentNullException.ThrowIfNull(stream); ArgumentNullException.ThrowIfNull(triangles);
        Capacity(stream.Count, triangles.Count);
        if (triangles.Count % 3 != 0) throw new ArgumentException("Incomplete triangle", nameof(triangles));
        for (int i=0;i<stream.Count;++i) Validate(stream[i]);
        for (int i=0;i<triangles.Count;++i) if ((uint)triangles[i] >= stream.Count) throw new ArgumentOutOfRangeException(nameof(triangles));
        int start = vertices.Count;
        for (int i=0;i<stream.Count;++i) vertices.Add(stream[i]);
        for (int i=0;i<triangles.Count;++i) indices.Add(start+triangles[i]);
    }
    public void AddUIVertexTriangleStream(IReadOnlyList<UIVertex> stream) {
        ArgumentNullException.ThrowIfNull(stream);
        if (stream.Count % 3 != 0) throw new ArgumentException("Incomplete triangle", nameof(stream));
        var triangles = new int[stream.Count]; for(int i=0;i<triangles.Length;++i) triangles[i]=i;
        AddUIVertexStream(stream,triangles);
    }
    public void GetUIVertexStream(List<UIVertex> stream) {
        ArgumentNullException.ThrowIfNull(stream); stream.Clear();
        foreach(int i in indices) stream.Add(vertices[i]);
    }
    public void PopulateUIVertex(ref UIVertex vertex, int index) => vertex = vertices[index];
    public void SetUIVertex(UIVertex vertex, int index) { Validate(vertex); vertices[index] = vertex; }
    public void FillMesh(UIMesh mesh) { ArgumentNullException.ThrowIfNull(mesh); mesh.vertices = Vertices(); mesh.indices = Indices(); }
    internal UIVertex[] Vertices() => vertices.ToArray();
    internal int[] Indices() => indices.ToArray();
}
