using System;
using System.Runtime.InteropServices;

namespace Overload.ManagedHost;

internal enum Status : uint
{
    Success, AbiMismatch, InvalidArgument, WrongThread, SessionNotFound, SessionClosing,
    SessionFaulted, TypeNotFound, DuplicateIdentity, InstanceNotFound, InstanceFaulted,
    InvalidLifecycle, FieldNotFound, FieldTypeMismatch, UnsupportedField, ManagedException,
    BufferOwnership, NativeAbiMismatch, Busy
}
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct Utf8 { public byte* Data; public ulong Length; }
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public record struct SessionHandle(ulong Id, ulong Generation);
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct InstanceHandle { public ulong Id, Generation; }
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct SessionInfo
{
    public uint Size, Flags;
    public ulong Generation;
    public Utf8 ProjectRoot, AssemblyPath, LogicalName;
}
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct HostApi
{
    public uint Version, Size;
    public fixed byte Signature[32];
    public delegate* unmanaged[Cdecl]<Utf8*, uint> ReleaseUtf8;
    public delegate* unmanaged[Cdecl]<void*, uint> ReleaseValue;
    public delegate* unmanaged[Cdecl]<SessionHandle, Utf8*, uint> TakeError;
    public delegate* unmanaged[Cdecl]<SessionInfo*, SessionHandle*, uint> CreateSession;
    public delegate* unmanaged[Cdecl]<SessionHandle, uint> CloseSession;
    public delegate* unmanaged[Cdecl]<SessionHandle, Utf8, uint, uint, byte*, nuint, uint> Bind;
    public delegate* unmanaged[Cdecl]<SessionHandle, uint*, uint> TypeCount;
    public delegate* unmanaged[Cdecl]<SessionHandle, uint, void*, uint> TypeInfo;
    public delegate* unmanaged[Cdecl]<SessionHandle, ulong, uint, Utf8*, uint> TypeAlias;
    public delegate* unmanaged[Cdecl]<void*, uint> ReleaseType;
    public delegate* unmanaged[Cdecl]<SessionHandle, ulong, uint, void*, uint> FieldInfo;
    public delegate* unmanaged[Cdecl]<SessionHandle, ulong, ulong, uint, Utf8*, uint> FieldAlias;
    public delegate* unmanaged[Cdecl]<void*, uint> ReleaseField;
    public delegate* unmanaged[Cdecl]<SessionHandle, void*, InstanceHandle*, uint> CreateInstance;
    public delegate* unmanaged[Cdecl]<SessionHandle, InstanceHandle, uint> DestroyInstance;
    public delegate* unmanaged[Cdecl]<SessionHandle, void*, uint> Dispatch;
    public delegate* unmanaged[Cdecl]<SessionHandle, InstanceHandle, ulong, void*, uint> ReadField;
    public delegate* unmanaged[Cdecl]<SessionHandle, InstanceHandle, ulong, void*, uint> WriteField;
}
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct BootstrapServices
{
    public uint Version, Size;
    public ulong Context;
    public delegate* unmanaged[Cdecl]<uint> IsMain;
    public delegate* unmanaged[Cdecl]<uint, Utf8, void> Log;
}
