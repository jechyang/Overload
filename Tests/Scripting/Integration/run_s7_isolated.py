"""Run a newly packaged game in a capability-free Windows AppContainer.

Only the newly created package gets an RX grant; only its Evidence directory gets
write/low-integrity grants. The installed SDK, Windows, PATH and CET are not changed.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import msvcrt
import os
from pathlib import Path
import subprocess
import time
import uuid

K=c.WinDLL('kernel32',use_last_error=True)
U=c.WinDLL('userenv',use_last_error=True)
A=c.WinDLL('advapi32',use_last_error=True)
W=c.WinDLL('user32',use_last_error=True)
P=c.WinDLL('psapi',use_last_error=True)
PTR=c.c_void_p
SIZE=c.c_size_t
def bind(dll,name,result,args):
    f=getattr(dll,name);f.restype=result;f.argtypes=args;return f
def checked(value,name):
    if not value: raise c.WinError(c.get_last_error(),name)
    return value
class SI(c.Structure):
    _fields_=[('cb',w.DWORD),('reserved',w.LPWSTR),('desktop',w.LPWSTR),('title',w.LPWSTR),('x',w.DWORD),('y',w.DWORD),('cx',w.DWORD),('cy',w.DWORD),('charsX',w.DWORD),('charsY',w.DWORD),('fill',w.DWORD),('flags',w.DWORD),('show',w.WORD),('reservedSize',w.WORD),('reservedBytes',PTR),('stdin',w.HANDLE),('stdout',w.HANDLE),('stderr',w.HANDLE)]
class SIX(c.Structure): _fields_=[('si',SI),('attrs',PTR)]
class PI(c.Structure): _fields_=[('process',w.HANDLE),('thread',w.HANDLE),('pid',w.DWORD),('tid',w.DWORD)]
class CAPS(c.Structure): _fields_=[('sid',PTR),('capabilities',PTR),('count',w.DWORD),('reserved',w.DWORD)]
create_profile=bind(U,'CreateAppContainerProfile',c.c_long,[w.LPCWSTR,w.LPCWSTR,w.LPCWSTR,PTR,w.DWORD,c.POINTER(PTR)])
delete_profile=bind(U,'DeleteAppContainerProfile',c.c_long,[w.LPCWSTR])
sid_string=bind(A,'ConvertSidToStringSidW',w.BOOL,[PTR,c.POINTER(w.LPWSTR)])
free_sid=bind(A,'FreeSid',PTR,[PTR])
local_free=bind(K,'LocalFree',PTR,[PTR])
init_attrs=bind(K,'InitializeProcThreadAttributeList',w.BOOL,[PTR,w.DWORD,w.DWORD,c.POINTER(SIZE)])
update_attrs=bind(K,'UpdateProcThreadAttribute',w.BOOL,[PTR,w.DWORD,SIZE,PTR,SIZE,PTR,PTR])
delete_attrs=bind(K,'DeleteProcThreadAttributeList',None,[PTR])
create_process=bind(K,'CreateProcessW',w.BOOL,[w.LPCWSTR,w.LPWSTR,PTR,PTR,w.BOOL,w.DWORD,PTR,w.LPCWSTR,c.POINTER(SIX),c.POINTER(PI)])
resume=bind(K,'ResumeThread',w.DWORD,[w.HANDLE])
exit_code=bind(K,'GetExitCodeProcess',w.BOOL,[w.HANDLE,c.POINTER(w.DWORD)])
terminate=bind(K,'TerminateProcess',w.BOOL,[w.HANDLE,w.UINT])
wait=bind(K,'WaitForSingleObject',w.DWORD,[w.HANDLE,w.DWORD])
close=bind(K,'CloseHandle',w.BOOL,[w.HANDLE])
enum_modules=bind(P,'EnumProcessModules',w.BOOL,[w.HANDLE,PTR,w.DWORD,c.POINTER(w.DWORD)])
module_path=bind(P,'GetModuleFileNameExW',w.DWORD,[w.HANDLE,w.HMODULE,w.LPWSTR,w.DWORD])
CALLBACK=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
enum_windows=bind(W,'EnumWindows',w.BOOL,[CALLBACK,w.LPARAM])
window_pid=bind(W,'GetWindowThreadProcessId',w.DWORD,[w.HWND,c.POINTER(w.DWORD)])
show=bind(W,'ShowWindowAsync',w.BOOL,[w.HWND,c.c_int])
post=bind(W,'PostMessageW',w.BOOL,[w.HWND,w.UINT,w.WPARAM,w.LPARAM])

def modules(handle):
    values=(w.HMODULE*2048)();needed=w.DWORD();checked(enum_modules(handle,values,c.sizeof(values),c.byref(needed)),'EnumProcessModules')
    result=[]
    for item in values[:needed.value//c.sizeof(w.HMODULE)]:
        text=c.create_unicode_buffer(32768)
        if module_path(handle,item,text,len(text)): result.append(text.value)
    return result

def launch(sid,package,exe,argv,env,log_path,marker=None):
    size=SIZE();init_attrs(None,2,0,c.byref(size));buffer=c.create_string_buffer(size.value)
    checked(init_attrs(buffer,2,0,c.byref(size)),'InitializeProcThreadAttributeList')
    caps=CAPS(sid,None,0,0);policy=w.DWORD(1);pi=PI()
    with log_path.open('wb') as log:
        handle=msvcrt.get_osfhandle(log.fileno());os.set_handle_inheritable(handle,True);handles=(w.HANDLE*1)(handle)
        try:
            checked(update_attrs(buffer,0,0x20009,c.byref(caps),c.sizeof(caps),None,None),'AppContainer capabilities')
            checked(update_attrs(buffer,0,0x20002,handles,c.sizeof(handles),None,None),'Log handle list')
            startup=SIX();startup.si.cb=c.sizeof(SIX);startup.si.flags=0x101;startup.si.show=0;startup.si.stdout=handle;startup.si.stderr=handle;startup.attrs=c.cast(buffer,PTR)
            environment=c.create_unicode_buffer('\0'.join(k+'='+v for k,v in sorted(env.items(),key=lambda p:p[0].upper()))+'\0\0')
            command=c.create_unicode_buffer(subprocess.list2cmdline([str(exe),*map(str,argv)]))
            checked(create_process(str(exe),command,None,None,True,0x80000|0x400|0x4|0x08000000,environment,str(package),c.byref(startup),c.byref(pi)),'CreateProcess AppContainer')
            checked(resume(pi.thread)!=0xFFFFFFFF,'ResumeThread');code=w.DWORD(259);loaded=[];closing=False;deadline=time.monotonic()+90
            while True:
                checked(exit_code(pi.process,c.byref(code)),'GetExitCodeProcess')
                if code.value!=259: break
                ready=marker is not None and marker.exists() and 'running frames=8 transform-box=verified' in marker.read_text(encoding='utf8')
                if ready and not loaded: loaded=modules(pi.process)
                @CALLBACK
                def visit(hwnd,param):
                    pid=w.DWORD();window_pid(hwnd,c.byref(pid))
                    if pid.value==pi.pid:
                        show(hwnd,0)
                        if ready: post(hwnd,0x10,0,0)
                    return True
                enum_windows(visit,0)
                if ready: closing=True
                if time.monotonic()>deadline: raise TimeoutError('Isolated process did not complete; marker='+str(marker))
                time.sleep(0.025)
            return {'exit_code':code.value,'pid':pi.pid,'modules':loaded,'graceful_close_requested':closing}
        finally:
            if pi.process:
                code=w.DWORD();exit_code(pi.process,c.byref(code))
                if code.value==259: terminate(pi.process,1);wait(pi.process,5000)
                close(pi.thread);close(pi.process)
            delete_attrs(buffer);os.set_handle_inheritable(handle,False)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--package',required=True);parser.add_argument('--sdk',required=True);args=parser.parse_args()
    package=Path(args.package).resolve();manifest=json.loads((package/'s7-package.json').read_text(encoding='utf8'))
    if manifest.get('kind')!='Overload-private-runtime-package': raise RuntimeError('Not a newly generated S7 package')
    for item in package.rglob('*'):
        if item.lstat().st_file_attributes&0x400: raise RuntimeError('Package must contain no links/junctions')
    evidence=package/'Evidence';evidence.mkdir(exist_ok=True)
    for p in ['temp','profile','local','roaming','shadow']: (evidence/p).mkdir(exist_ok=True)
    ini=package/'Data/User/Game.ini';original=ini.read_text(encoding='utf8')
    name='Overload.S7.'+uuid.uuid4().hex;sid=PTR();created=False;receipt={'isolation':'AppContainer, no capabilities; SDK executable is inaccessible','sdk':args.sdk,'profile':name,'success':False}
    try:
        result=create_profile(name,name,'Temporary isolated S7 game validation',None,0,c.byref(sid))
        if result<0: raise OSError('CreateAppContainerProfile HRESULT '+hex(result&0xFFFFFFFF))
        created=True;text=w.LPWSTR();checked(sid_string(sid,c.byref(text)),'SID string');sid_text=text.value;local_free(text);receipt['sid']=sid_text
        with (evidence/'acl.log').open('wb') as log:
            for command in [[str(package),'/grant','*'+sid_text+':(OI)(CI)RX','/T','/Q'],[str(evidence),'/grant','*'+sid_text+':(OI)(CI)M','/T','/Q'],[str(evidence),'/setintegritylevel','(OI)(CI)L','/T','/Q']]:
                subprocess.run([str(Path(os.environ['SystemRoot'])/'System32/icacls.exe'),*command],check=True,stdout=log,stderr=log)
        env={k:v for k,v in os.environ.items() if not k.upper().startswith(('DOTNET','COREHOST','NUGET','MSBUILD','OV_CSC','OV_SCRIPTING')) and k.upper() not in ['PATH','APPDATA','LOCALAPPDATA','USERPROFILE','TEMP','TMP']}
        env.update(PATH=str(Path(os.environ['SystemRoot'])/'System32'),DOTNET_ROOT=str(evidence/'absent-dotnet'),DOTNET_ROOT_X64=str(evidence/'absent-dotnet'),DOTNET_MULTILEVEL_LOOKUP='0',DOTNET_ROLL_FORWARD='Disable',TEMP=str(evidence/'temp'),TMP=str(evidence/'temp'),USERPROFILE=str(evidence/'profile'),APPDATA=str(evidence/'roaming'),LOCALAPPDATA=str(evidence/'local'),OV_SCRIPTING_SHADOW_ROOT=str(evidence/'shadow'))
        receipt['environment']={k:env[k] for k in ['PATH','DOTNET_ROOT','DOTNET_ROOT_X64','DOTNET_MULTILEVEL_LOOKUP','DOTNET_ROLL_FORWARD']}
        sdk_roots=[Path(args.sdk),Path(os.environ.get('ProgramFiles','C:/Program Files'))/'dotnet',Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'dotnet',Path.home()/'.dotnet']
        sdk_files=sorted({str(f.resolve()) for sdk_root in sdk_roots for f in (sdk_root/'sdk').glob('*/MSBuild.dll')})
        receipt['sdk_survey']={'roots':[str(p) for p in sdk_roots],'installed_sdk_entrypoints':sdk_files}
        probe=launch(sid,package,package/'SdkProbe.exe',[Path(args.sdk)/'dotnet.exe',package/'Runtime/dotnet/shared/Microsoft.NETCore.App/10.0.12/coreclr.dll',*sdk_files],env,evidence/'sdk-probe.log');receipt['sdk_probe']=probe
        if probe['exit_code']!=0: raise RuntimeError('AppContainer SDK access denial/private-runtime probe failed')
        receipt['modes']={}
        ini=package/'Data/User/Game.ini';original=ini.read_text(encoding='utf8')
        for mode in ['default','render-thread']:
            ini.write_text(original.replace('render_thread=false','render_thread='+str(mode=='render-thread').lower()),encoding='utf8')
            marker=evidence/(mode+'-events.txt');child_env={**env,'OV_S7_MARKER':str(marker),'COREHOST_TRACE':'1','COREHOST_TRACEFILE':str(evidence/(mode+'-host-trace.log')),'DOTNET_HOST_TRACE':'1','DOTNET_HOST_TRACEFILE':str(evidence/(mode+'-host-trace.log'))}
            result=launch(sid,package,package/'OvGame.exe',[],child_env,evidence/(mode+'-stdout.log'),marker);receipt['modes'][mode]=result
            events=marker.read_text(encoding='utf8') if marker.exists() else '';result['events']=events.splitlines()
            if result['exit_code'] or not all(s in events for s in ['awake speed=7','start','running frames=8 transform-box=verified','destroy frames=']): raise RuntimeError('Packaged OvGame lifecycle/Transform/Box validation failed: '+mode)
            for dll,expected in [('hostfxr.dll',package/'Runtime/dotnet/host/fxr/10.0.12/hostfxr.dll'),('coreclr.dll',package/'Runtime/dotnet/shared/Microsoft.NETCore.App/10.0.12/coreclr.dll')]:
                matches=[p for p in result['modules'] if Path(p).name.lower()==dll]
                if len(matches)!=1 or Path(matches[0]).resolve()!=expected.resolve(): raise RuntimeError('Wrong private runtime module: '+dll)
            if any(str(Path(args.sdk).resolve()).lower() in p.lower() for p in result['modules']): raise RuntimeError('SDK module loaded')
        ini.write_bytes(original.encode('utf8'));receipt['success']=True
    except Exception as error:
        receipt['error']=str(error)
    finally:
        ini.write_bytes(original.encode('utf8'))
        if created: receipt['profile_delete_hresult']=delete_profile(name)
        if sid: free_sid(sid)
        (evidence/'isolation-verification.json').write_text(json.dumps(receipt,indent=2),encoding='utf8')
    print(json.dumps({'success':receipt['success'],'evidence':str(evidence),'error':receipt.get('error')}))
    return 0 if receipt['success'] else 1
if __name__=='__main__': raise SystemExit(main())
