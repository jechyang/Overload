using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Overload.ManagedHost;

public static unsafe partial class Bootstrap
{
    private static ulong nativeCalls, lifecycleCalls;
    [StructLayout(LayoutKind.Sequential)]
    public struct RuntimeMetrics
    {
        public ulong AllocatedBytes, NativeCalls, LifecycleCalls;
        public uint Gen0, Gen1, Gen2, Reserved;
    }
    // Allocation-free probe. Counters include script calls only, not instrumentation.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint GetRuntimeMetrics(uint size, RuntimeMetrics* result)
    {
        try {
            Require(Environment.CurrentManagedThreadId == mainThread && services.IsMain != null && services.IsMain() == 1, Status.WrongThread, "Metrics require Main");
            Require(!busy && size == sizeof(RuntimeMetrics) && result != null, Status.InvalidArgument, "Invalid metric probe");
            *result = new() { AllocatedBytes = (ulong)GC.GetAllocatedBytesForCurrentThread(), NativeCalls = nativeCalls, LifecycleCalls = lifecycleCalls,
                Gen0 = (uint)GC.CollectionCount(0), Gen1 = (uint)GC.CollectionCount(1), Gen2 = (uint)GC.CollectionCount(2) };
            return 0;
        } catch (Exception ex) { return Error(default, ex); }
    }
}
