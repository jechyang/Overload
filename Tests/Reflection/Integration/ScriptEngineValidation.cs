using System;
using System.Runtime.InteropServices;
using Overload.Generated.OvCore;
using Overload.Script;
public static unsafe class ScriptEngineValidation {
    static void Require(bool condition,string message){if(!condition)throw new Exception(message);}
    static void Expired(Action call){try{call();}catch(BridgeException e){Require(e.Status==2,"expired handle status");return;}throw new Exception("old component survived");}
    public static int Main(string[] args){
        nint lib=NativeLibrary.Load(args[0]);
        var start=(delegate* unmanaged[Cdecl]<uint,ulong>)NativeLibrary.GetExport(lib,"R6_EngineStart");
        var handle=(delegate* unmanaged[Cdecl]<uint,ulong>)NativeLibrary.GetExport(lib,"R6_EngineHandle");
        var ray=(delegate* unmanaged[Cdecl]<int>)NativeLibrary.GetExport(lib,"R6_EngineRay");
        var matrix=(delegate* unmanaged[Cdecl]<int>)NativeLibrary.GetExport(lib,"R6_EngineMatrix");
        var render=(delegate* unmanaged[Cdecl]<int>)NativeLibrary.GetExport(lib,"R6_EngineRenderReject");
        var remove=(delegate* unmanaged[Cdecl]<int>)NativeLibrary.GetExport(lib,"R6_EngineRemoveBox");
        var replace=(delegate* unmanaged[Cdecl]<int>)NativeLibrary.GetExport(lib,"R6_EngineReplaceScene");
        var stop=(delegate* unmanaged[Cdecl]<void>)NativeLibrary.GetExport(lib,"R6_EngineStop");
        ulong context=start(args[1]=="render-thread"?1u:0u);Require(context!=0,"start engine");
        var api=new Api(NativeLibrary.GetExport(lib,"OvScript_GetOvCoreApi"),context);
        ulong box=handle(0),transform=handle(1);
        Require(ray()==0,"small initial box");
        api.OvCore_ECS_Components_CPhysicalBox_set_size(box,new Vector3{x=2,y=3,z=4});
        Require(ray()==1 && api.OvCore_ECS_Components_CPhysicalBox_get_size(box).y==3,"managed size updates real Bullet body");
        api.OvCore_ECS_Components_CPhysicalBox_set_size(box,new Vector3{x=.5f,y=.5f,z=.5f});Require(ray()==0,"restored Bullet shape");
        Require(render()==1,"Render owner rejection");
        api.OvCore_ECS_Components_CTransform_set_position(transform,new Vector3{x=1,y=2,z=3});
        api.OvCore_ECS_Components_CTransform_set_scale(transform,new Vector3{x=2,y=3,z=4});
        api.OvCore_ECS_Components_CTransform_set_rotation(transform,new Quaternion{x=0,y=0,z=0,w=1});
        api.OvCore_ECS_Components_CTransform_call_TranslateLocal(transform,new Vector3{x=4,y=5,z=6});
        var position=api.OvCore_ECS_Components_CTransform_get_position(transform);Require(position.x==5 && position.y==7 && position.z==9,"managed method updates transform");
        Require(matrix()==1,"hierarchy world matrix");
        Require(remove()==1,"remove box");Expired(()=>api.OvCore_ECS_Components_CPhysicalBox_get_size(box));
        Require(replace()==1,"scene replacement");Expired(()=>api.OvCore_ECS_Components_CTransform_get_position(transform));
        ulong fresh=handle(1);Require(fresh!=transform && api.OvCore_ECS_Components_CTransform_get_position(fresh).x==0,"new scene handle");
        stop();try{api.OvCore_ECS_Components_CTransform_get_position(fresh);throw new Exception("closed context accepted");}catch(BridgeException e){Require(e.Status==1,"closed context status");}
        Console.WriteLine("PASS: CoreCLR production Box Bullet effects, Transform hierarchy, removal, scene replacement and "+args[1]);return 0;
    }
}
