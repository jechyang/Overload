using System;
using System.Buffers.Binary;

namespace Overload.UI;

/// <summary>Versioned op45 payload. Little endian. Legacy 36-byte vertices remain accepted by native.</summary>
public static class UIMeshEncoding
{
    public const uint Magic = 0x3249554f; // OUI2
    public const int VertexBytes = 88;
    public static byte[] Encode(UIVertex[] vertices, int[] indices) {
        ArgumentNullException.ThrowIfNull(vertices); ArgumentNullException.ThrowIfNull(indices);
        if (vertices.Length > VertexHelper.MaximumVertices || indices.Length > VertexHelper.MaximumIndices || indices.Length % 3 != 0)
            throw new ArgumentException("Invalid UI mesh size");
        var payload = new byte[checked(16+vertices.Length*VertexBytes+indices.Length*4)];
        BinaryPrimitives.WriteUInt32LittleEndian(payload,Magic);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4),2);
        BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(8),vertices.Length);
        BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(12),indices.Length);
        int offset=16;
        void F(float x) { BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(offset),x); offset+=4; }
        foreach(var v in vertices) {
            VertexHelper.Validate(v);
            F(v.position.X);F(v.position.Y);F(v.position.Z);F(v.uv0.x);F(v.uv0.y);
            F(v.color.r/255f);F(v.color.g/255f);F(v.color.b/255f);F(v.color.a/255f);
            F(v.normal.X);F(v.normal.Y);F(v.normal.Z);F(v.tangent.X);F(v.tangent.Y);F(v.tangent.Z);F(v.tangent.W);
            F(v.uv1.x);F(v.uv1.y);F(v.uv2.x);F(v.uv2.y);F(v.uv3.x);F(v.uv3.y);
        }
        foreach(int index in indices) {
            if ((uint)index >= vertices.Length) throw new ArgumentOutOfRangeException(nameof(indices));
            BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(offset),index); offset+=4;
        }
        return payload;
    }
}
