#nullable enable
using System;
using System.Text;
using System.Runtime.InteropServices;
namespace Overload.Script
{
    public sealed class BridgeException : Exception
    {
        public int Status { get; }
        public ulong Completed { get; }
        public BridgeException(int status, ulong completed = 0) : base($"Native Script call failed (status {status}, completed {completed})") { Status = status; Completed = completed; }
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    public unsafe struct NativeString { public byte* Data; public ulong Size; }
    [StructLayout(LayoutKind.Explicit, Size = 12)]
    public struct Vector3 { [FieldOffset(0)] public float x; [FieldOffset(4)] public float y; [FieldOffset(8)] public float z; }
    [StructLayout(LayoutKind.Explicit, Size = 16)]
    public struct Quaternion { [FieldOffset(0)] public float x; [FieldOffset(4)] public float y; [FieldOffset(8)] public float z; [FieldOffset(12)] public float w; }
    public static unsafe class Bridge
    {
        public static readonly UTF8Encoding Utf8 = new UTF8Encoding(false, true);
        public static void Check(int status) { if (status != 0) throw new BridgeException(status); }
        public static string Decode(NativeString value) => Utf8.GetString(new ReadOnlySpan<byte>(value.Data, checked((int)value.Size)));
    }
}
