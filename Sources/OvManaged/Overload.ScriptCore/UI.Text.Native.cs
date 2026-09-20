using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Text;
using Overload.Maths;

namespace Overload.UI;

internal sealed class NativeTextFontSource(Actor owner) : ITextFontSource
{
    private const uint Magic = 0x3146544f;
    private readonly Dictionary<TextFontBatch, ulong> receipts = new();
    private sealed class Lease(Action release) : IDisposable
    {
        private Action? release = release;
        public void Dispose() { var callback = release; release = null; callback?.Invoke(); }
    }
    private static void Put(byte[] bytes, int offset, uint value) => BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(offset), value);
    private static void Put64(byte[] bytes, int offset, ulong value) => BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(offset), value);
    private static uint U32(byte[] bytes, int offset) => BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(offset));
    private static ulong U64(byte[] bytes, int offset) => BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(offset));
    private static float Float(byte[] bytes, int offset) => BitConverter.UInt32BitsToSingle(U32(bytes, offset));
    private static Rect Rect(byte[] bytes, int offset) { Vector2 min = new(Float(bytes, offset), Float(bytes, offset + 4)); return new(min, new Vector2(Float(bytes, offset + 8), Float(bytes, offset + 12)) - min); }
    private static byte[] Packet(int requestBytes, int capacity, int count = 0, ulong receipt = 0)
    {
        var bytes = new byte[Math.Max(requestBytes, capacity)];
        Put(bytes, 0, Magic); Put(bytes, 4, 1); Put(bytes, 8, checked((uint)requestBytes)); Put(bytes, 12, checked((uint)bytes.Length));
        Put(bytes, 16, checked((uint)count)); Put64(bytes, 24, receipt); return bytes;
    }
    public TextFontBatch EnsureGlyphs(string font, FontStyle style, IReadOnlyList<uint> scalars)
    {
        if (scalars.Count > 4096) throw new ArgumentException("A text font/style batch is limited to 4096 distinct scalars");
        byte[] path = new UTF8Encoding(false, true).GetBytes(font);
        if (path.Length is 0 or > 4096) throw new ArgumentException("Invalid font path length");
        var bytes = Packet(64 + path.Length + scalars.Count * 4, 48 + scalars.Count * 80, scalars.Count);
        Put(bytes, 20, (uint)style); Put(bytes, 32, (uint)path.Length); Put(bytes, 36, 64); Put(bytes, 40, 8);
        path.CopyTo(bytes, 64);
        for (int i = 0; i < scalars.Count; ++i) Put(bytes, 64 + path.Length + i * 4, scalars[i]);
        ulong receipt = owner.Access.ExchangeFont(owner.Handle, 100, bytes);
        TextFontBatch? result = null;
        try
        {
            uint count = U32(bytes, 12);
            if (U32(bytes, 0) != Magic || U32(bytes, 4) != 1 || U64(bytes, 16) != receipt || count > scalars.Count || U32(bytes, 8) != 48 + count * 80)
                throw new InvalidOperationException("Native font response ABI mismatch");
            var glyphs = new Dictionary<uint, TextGlyph>();
            for (int i = 0; i < count; ++i)
            {
                int p = 48 + i * 80;
                if (U32(bytes, p + 8) > 1 || U32(bytes, p + 76) != 0) throw new InvalidOperationException("Invalid native glyph flags");
                glyphs.Add(U32(bytes, p), new(U32(bytes, p + 4), U32(bytes, p + 8) != 0, Float(bytes, p + 24),
                    new(Float(bytes, p + 28), Float(bytes, p + 32)), Rect(bytes, p + 36), Rect(bytes, p + 52),
                    U32(bytes, p + 12), U64(bytes, p + 16), Float(bytes, p + 68), Float(bytes, p + 72)));
            }
            result = new(font, style, U64(bytes, 24), new(Float(bytes, 32), Float(bytes, 36), Float(bytes, 40)), glyphs,
                new Lease(() => { if (result != null) receipts.Remove(result); Release(receipt); }));
            receipts.Add(result, receipt); return result;
        }
        catch { Release(receipt); throw; }
    }
    private void Release(ulong receipt) => owner.Access.ExchangeFont(owner.Handle, 102, Packet(64, 64, receipt: receipt));
    public IReadOnlyList<float> GetKerning(TextFontBatch batch, IReadOnlyList<TextKerningPair> pairs)
    {
        if (pairs.Count > 4096 || !receipts.TryGetValue(batch, out ulong receipt)) throw new ArgumentException("Invalid text kerning request");
        var bytes = Packet(64 + pairs.Count * 8, 16 + pairs.Count * 4, pairs.Count, receipt);
        for (int i = 0; i < pairs.Count; ++i) { Put(bytes, 64 + i * 8, pairs[i].left); Put(bytes, 68 + i * 8, pairs[i].right); }
        ulong length = owner.Access.ExchangeFont(owner.Handle, 101, bytes);
        if (length != (ulong)(16 + pairs.Count * 4) || U32(bytes, 0) != Magic || U32(bytes, 4) != 1 || U32(bytes, 12) != pairs.Count)
            throw new InvalidOperationException("Native font kerning ABI mismatch");
        var result = new float[pairs.Count];
        for (int i = 0; i < result.Length; ++i) result[i] = Float(bytes, 16 + i * 4);
        return result;
    }
    internal void Bind(CanvasRenderer renderer, TextPageKey page, int slot)
    {
        ulong receipt = 0;
        // Later batches of the same generation include newer append-only pages.
        foreach (var entry in receipts) if (entry.Key.generation == page.generation) receipt = entry.Value;
        if (receipt == 0) throw new InvalidOperationException("Text page lease is no longer available");
        var bytes = Packet(64, 64, receipt: receipt); Put(bytes, 44, page.page); Put(bytes, 48, checked((uint)slot));
        owner.Access.ExchangeFont(renderer.Handle, 103, bytes);
    }
}
