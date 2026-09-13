#nullable enable
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using Overload.Generated.Fixture;
using Overload.Script;
public static unsafe class ScriptValidation
{
    static void Require(bool condition, string label) { if (!condition) throw new Exception(label); }
    static void Fails(Action action, int status) { try { action(); } catch (BridgeException e) { Require(e.Status == status, "status"); return; } throw new Exception("expected failure " + status); }
    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    static WeakReference Reload(string path, nint entry, ulong context, ulong handle)
    {
        var load = new System.Runtime.Loader.AssemblyLoadContext("R6Reload", isCollectible: true);
        var assembly = load.LoadFromAssemblyPath(path);
        object? result = assembly.GetType("ScriptReload")!.GetMethod("Run")!.Invoke(null, new object[]{(IntPtr)entry,context,handle});
        Require((int)result! == 42, "reloaded wrapper execution");
        var weak = new WeakReference(load); load.Unload(); return weak;
    }
    public static int Main(string[] args)
    {
        nint library = NativeLibrary.Load(args[0]);
        var start = (delegate* unmanaged[Cdecl]<ulong>)NativeLibrary.GetExport(library, "R6_Start");
        var getHandle = (delegate* unmanaged[Cdecl]<uint, ulong>)NativeLibrary.GetExport(library, "R6_Handle");
        var replace = (delegate* unmanaged[Cdecl]<ulong>)NativeLibrary.GetExport(library, "R6_Replace");
        var stop = (delegate* unmanaged[Cdecl]<void>)NativeLibrary.GetExport(library, "R6_Stop");
        var destroyOther = (delegate* unmanaged[Cdecl]<void>)NativeLibrary.GetExport(library, "R6_DestroyOther");
        ulong context = start(), handle = getHandle(0), other = getHandle(1), readOnly = getHandle(2);
        nint entry = NativeLibrary.GetExport(library, "OvScript_GetFixtureApi");
        var api = new Api(entry, context);
        api.R6_Values_set_flag(handle,true); Require(api.R6_Values_get_flag(handle),"bool");
        api.R6_Values_set_i8(handle,sbyte.MinValue); Require(api.R6_Values_get_i8(handle)==sbyte.MinValue,"i8");
        api.R6_Values_set_u8(handle,byte.MaxValue); Require(api.R6_Values_get_u8(handle)==byte.MaxValue,"u8");
        api.R6_Values_set_i16(handle,short.MinValue); Require(api.R6_Values_get_i16(handle)==short.MinValue,"i16");
        api.R6_Values_set_u16(handle,ushort.MaxValue); Require(api.R6_Values_get_u16(handle)==ushort.MaxValue,"u16");
        api.R6_Values_set_i32(handle,int.MinValue); Require(api.R6_Values_get_i32(handle)==int.MinValue,"i32");
        api.R6_Values_set_u32(handle,uint.MaxValue); Require(api.R6_Values_get_u32(handle)==uint.MaxValue,"u32");
        api.R6_Values_set_i64(handle,long.MinValue); Require(api.R6_Values_get_i64(handle)==long.MinValue,"i64");
        api.R6_Values_set_u64(handle,ulong.MaxValue); Require(api.R6_Values_get_u64(handle)==ulong.MaxValue,"u64");
        api.R6_Values_set_single(handle,float.MaxValue); Require(api.R6_Values_get_single(handle)==float.MaxValue,"float");
        api.R6_Values_set_real(handle,double.MaxValue); Require(api.R6_Values_get_real(handle)==double.MaxValue,"double");
        api.R6_Values_set_mode(handle,E_R6_Mode.Maximum); Require(api.R6_Values_get_mode(handle)==E_R6_Mode.Maximum,"enum");
        Require(Marshal.SizeOf<Vector3>()==12 && Marshal.OffsetOf<Quaternion>("w").ToInt32()==12,"layout");
        var vector=new Vector3{x=1.25f,y=-2.5f,z=9}; var rotation=new Quaternion{x=0.1f,y=0.2f,z=0.3f,w=0.4f};
        api.R6_Values_set_vector(handle,vector); var copy=api.R6_Values_call_Position(handle);
        Require(copy.x==vector.x && copy.y==vector.y && copy.z==vector.z,"reference copy");
        copy.x=99; Require(api.R6_Values_get_vector(handle).x==1.25f,"copy isolation");
        api.R6_Values_set_rotation(handle,rotation); Require(api.R6_Values_get_rotation(handle).w==0.4f,"quaternion");
        foreach (string text in new[]{"", "中文🙂\0tail", new string('z',100000)}) {
            api.R6_Values_set_text(handle,text); Require(api.R6_Values_get_text(handle)==text,"string property");
            Require(api.R6_Values_call_Echo(handle,text)==text,"string method");
        }
        for(int i=0;i<5000;i++) Require(api.R6_Values_call_Echo(handle,"owned🙂") == "owned🙂","owned outputs");
        try { api.R6_Values_set_text(handle,null!); throw new Exception("null string accepted"); } catch(ArgumentNullException) {}
        try { api.R6_Values_set_text(handle,"\ud800"); throw new Exception("invalid UTF16 accepted"); } catch(System.Text.EncoderFallbackException) {}
        Require(api.R6_Values_call_Integer(handle,7)==10 && api.R6_Values_call_Real(handle,7)==7.5,"overloads");
        Require(api.R6_Values_call_Add(handle,8,9)==17 && api.R6_Values_get_secret(handle)==17,"method/private access");
        Require(api.R6_Values_call_EchoObject(handle,other)==other && api.R6_Values_call_EchoObject(handle,0)==0,"object handles/null");
        api.R6_Values_set_target(handle,other); Require(api.R6_Values_get_target(handle)==other,"handle property");
        api.R6_Values_set_target(handle,0); Require(api.R6_Values_get_target(handle)==0,"nullable handle property");
        Fails(()=>api.R6_Values_call_RequiredObject(handle,0),2);
        Fails(()=>api.R6_Values_call_EchoObject(handle,handle),3);
        Fails(()=>api.R6_Values_call_Unpublished(handle),2);
        Fails(()=>api.R6_Values_set_i32(readOnly,0),5);
        Fails(()=>api.R6_Values_get_i32(other),3);
        Fails(()=>api.R6_Values_call_Raise(handle),7); Fails(()=>api.R6_Values_call_RaiseUnknown(handle),7);
        Exception? workerError=null;
        var worker=new Thread(()=>{try { Fails(()=>api.R6_Values_get_i32(handle),4); } catch(Exception e){workerError=e;} }); worker.Start();worker.Join(); if(workerError!=null)throw workerError;
        Require(typeof(Api).GetMethod("R6_Values_get_nativeOnly")==null && typeof(Api).GetMethod("R6_Values_call_Unmarked")==null && typeof(Api).GetMethod("R6_Values_set_immutable")==null,"opt-in boundary");
        // Warm both paths; same scalar values and same total number of reads.
        api.R6_Values_set_i32(handle,7); const int count=262144, width=256;
        var handles=new ulong[width]; Array.Fill(handles,handle); var output=new int[width];
        for(int i=0;i<20000;i++)api.R6_Values_get_i32(handle);
        for(int i=0;i<1000;i++)api.R6_Values_get_i32_batch(handles,output);
        long sum=0; var watch=Stopwatch.StartNew();
        for(int i=0;i<count;i++)sum+=api.R6_Values_get_i32(handle);
        watch.Stop(); double single=watch.Elapsed.TotalMilliseconds*1000000/count;
        Require(sum==7L*count,"scalar benchmark result");sum=0;watch.Restart();
        for(int i=0;i<count/width;i++){api.R6_Values_get_i32_batch(handles,output);for(int j=0;j<width;j++)sum+=output[j];}
        watch.Stop();double batch=watch.Elapsed.TotalMilliseconds*1000000/count;Require(sum==7L*count,"batch benchmark result");
        long methodSum=0; watch.Restart();
        for(int i=0;i<count;i++)methodSum+=api.R6_Values_call_Add(handle,1,2);
        watch.Stop(); double method=watch.Elapsed.TotalMilliseconds*1000000/count;Require(methodSum==3L*count,"method benchmark result");
        Console.WriteLine($"BENCHMARK: calls={count}; method_ns={method:F2}");
        Console.WriteLine($"BENCHMARK: reads={count}; scalar_ns={single:F2}; batch_ns_per_item={batch:F2}; batch_width={width}");
        for (int cycle=0; cycle<10; cycle++) {
            var weak=Reload(args[1],entry,context,handle);
            for(int attempt=0;attempt<20 && weak.IsAlive;attempt++){GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();}
            Require(!weak.IsAlive,"collectible assembly still retained");
        }
        destroyOther(); Fails(()=>api.R6_Values_call_EchoObject(handle,other),2);
        ulong old=handle; handle=replace(); Require(old!=handle,"non-reused handle");Fails(()=>api.R6_Values_get_i32(old),2);
        try { api.R6_Values_get_i32_batch(new[]{handle,old},new int[2]); throw new Exception("batch accepted stale handle"); } catch(BridgeException e){Require(e.Status==2 && e.Completed==1,"batch partial prefix");}
        stop(); Fails(()=>api.R6_Values_get_i32(handle),1);
        // The native library remains loaded for all wrappers' lifetimes by host contract.
        Console.WriteLine("PASS: CoreCLR generated C# ABI, values, lifetime, errors, thread and batch checks"); return 0;
    }
}
