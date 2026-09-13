using System;
using System.Runtime.InteropServices;

internal static class Program
{
    private static int Main()
    {
        if (!Environment.Is64BitProcess || RuntimeInformation.ProcessArchitecture != Architecture.X64)
        {
            Console.Error.WriteLine($"S0 smoke requires x64; process={RuntimeInformation.ProcessArchitecture}");
            return 10;
        }

        if (Environment.Version != new Version(10, 0, 12))
        {
            Console.Error.WriteLine($"S0 smoke requires runtime 10.0.12; actual={Environment.Version}");
            return 11;
        }

        long signed = long.MinValue;
        ulong unsigned = ulong.MaxValue;
        string utf8 = "Overload C# 工具链 ✅";
        if (signed + 1 != -9223372036854775807L || unsigned - 1 != 18446744073709551614UL || utf8.Length != 17)
        {
            Console.Error.WriteLine("S0 smoke value checks failed");
            return 12;
        }

        Console.WriteLine(
            $"PASS: S0 .NET toolchain smoke; framework={RuntimeInformation.FrameworkDescription}; " +
            $"architecture={RuntimeInformation.ProcessArchitecture}; utf16-length={utf8.Length}"
        );
        return 0;
    }
}
