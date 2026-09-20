using System;
using System.Text;
using Overload.Maths;

namespace Overload.EventSystems;

/// <summary>Input operations 80..84, allocated by A; native InputManager owns the immutable frame.</summary>
public sealed class NativeUIInputSource(Actor owner) : IUIInputSource
{
    public UIInputSnapshot Capture() {
        int count = checked((int)owner.Access.Query(owner.Handle,80));
        if (count < 12 || count > 200000) throw new InvalidOperationException("Invalid native input packet size");
        var words = new ulong[count];
        for (int i = 0; i < count; ++i) words[i] = owner.Access.Query(owner.Handle,81,(ulong)i);
        return Decode(words);
    }
    public string clipboard {
        get {
            int count = checked((int)owner.Access.Query(owner.Handle,82));
            if (count < 0 || count > 1048576) throw new InvalidOperationException("Invalid clipboard length");
            var bytes = new byte[count];
            for (int offset = 0; offset < count; offset += 8) {
                ulong word = owner.Access.Query(owner.Handle,83,(ulong)(offset/8));
                for (int j = 0; j < 8 && offset+j < count; ++j) bytes[offset+j] = (byte)(word >> (j*8));
            }
            return new UTF8Encoding(false,true).GetString(bytes);
        }
        set => owner.Access.Query(owner.Handle,84,0,value);
    }
    public static UIInputSnapshot Decode(ReadOnlySpan<ulong> packet) {
        // Copy once so the decoder never stores a caller-owned mutable span.
        var words = packet.ToArray(); int cursor = 0;
        ulong Next() => cursor < words.Length ? words[cursor++] : throw new ArgumentException("Truncated UI input packet");
        int Count(int limit) { ulong n = Next(); return n <= (ulong)limit ? (int)n : throw new ArgumentException("UI input count limit"); }
        Vector2 Point() { ulong v = Next(); return new(BitConverter.UInt32BitsToSingle((uint)v),BitConverter.UInt32BitsToSingle((uint)(v >> 32))); }
        string Text(int length) {
            var text = new StringBuilder();
            for (int i = 0; i < length; ++i) { ulong scalar = Next(); if (scalar > int.MaxValue || !Rune.IsValid((int)scalar)) throw new ArgumentException("Invalid native Unicode scalar"); text.Append(new Rune((int)scalar)); }
            return text.ToString();
        }
        if (Next() != 0x3149554f) throw new ArgumentException("Unsupported input encoding");
        ulong sequence = Next(); double time = BitConverter.UInt64BitsToDouble(Next()), delta = BitConverter.UInt64BitsToDouble(Next());
        bool focus = Count(1) != 0; var mouse = Point(); var extent = Point();
        int eventCount = Count(16384), keyCount = Count(1024), buttonCount = Count(32), textCount = Count(4096), textCursor = Count(4096);
        var events = new UIInputEvent[eventCount];
        for (int i = 0; i < eventCount; ++i) {
            ulong packed = Next(); var point = Point(); int phase = Count(4), compositionCursor = Count(4096), length = Count(4096);
            events[i] = new((UIInputKind)(uint)packed,unchecked((int)(packed >> 32)),point,(TouchPhase)phase,Text(length),compositionCursor);
        }
        var keys = new int[keyCount]; for (int i = 0; i < keyCount; ++i) keys[i] = unchecked((int)Next());
        var buttons = new int[buttonCount]; for (int i = 0; i < buttonCount; ++i) buttons[i] = unchecked((int)Next());
        string composition = Text(textCount);
        if (cursor != words.Length) throw new ArgumentException("Trailing UI input payload");
        return new(sequence,time,(float)delta,focus,mouse,extent,events,keys,buttons,composition,textCursor);
    }
}
