#!/usr/bin/env python3
"""R6 generated C ABI / real CoreCLR execution with optional full solution regression."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
sys.dont_write_bytecode = True
from verify_r3 import Verification, snapshot, r, require, ROOT

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',nargs='+',default=['Debug','Release','Publish'])
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--engine',action='store_true',help='Execute generated C# against real GL/Bullet components')
    parser.add_argument('--dotnet',default='C:/Program Files/dotnet/dotnet.exe')
    parser.add_argument('--runtime',help='CoreCLR runtime directory; defaults to the newest installed runtime >= 6')
    args=parser.parse_args()
    output=Path(tempfile.mkdtemp(prefix='r6-',dir=ROOT/'Build/Tests/Reflection'))
    print('R6 evidence: '+str(output),flush=True)
    v=Verification(output)
    v.run('export',[sys.executable,r.TOOL/'reflection.py','export','--output',output/'Export'])
    tc=r.read_json(output/'Export/toolchain.json')
    compiler=Path(tc['msvc_root'])/'bin/Hostx64/x64'
    v.env['INCLUDE']=';'.join(tc['system_includes'])
    v.env['LIB']=';'.join(str(p) for p in [Path(tc['msvc_root'])/'lib/x64',Path(tc['sdk_root'])/'Lib'/tc['sdk_version']/'ucrt/x64',Path(tc['sdk_root'])/'Lib'/tc['sdk_version']/'um/x64'])
    prior=next((v.env.pop(k) for k in list(v.env) if k.lower()=='path'),'')
    v.env['PATH']=str(compiler)+os.pathsep+prior
    v.env['DOTNET_TieredCompilation']='0'
    csc=Path(tc['vs_root'])/'MSBuild/Current/Bin/Roslyn/csc.exe'
    runtime_root=Path(args.dotnet).parent/'shared/Microsoft.NETCore.App'
    runtime=Path(args.runtime) if args.runtime else max((p for p in runtime_root.iterdir() if p.is_dir() and p.name.split('.')[0].isdigit() and int(p.name.split('.')[0])>=6),key=lambda p:tuple(int(x) for x in p.name.split('.')))
    require((runtime/'System.Private.CoreLib.dll').is_file(),'Set --runtime to a CoreCLR runtime directory')
    def managed_assembly(path):
        import struct
        data=path.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0];optional=pe+24
        magic=struct.unpack_from('<H',data,optional)[0]
        directory=optional+(112 if magic==0x20b else 96)
        return struct.unpack_from('<I',data,directory+14*8)[0]!=0
    references=[p for p in sorted(runtime.glob('*.dll')) if managed_assembly(p)]
    v.run('csharp-version',[csc,'/version'])
    v.run('coreclr-version',[args.dotnet,'--info'])
    v.run('premake',[ROOT/'Dependencies/premake5/bin/premake5.exe','--reflection-python='+sys.executable,'vs2022'])
    generator=ROOT/'Build/Tools/OvReflectGen/Release/OvReflectGen.exe'
    negatives={
        'copy':('struct OV_TYPE() Bad { int n; OV_METHOD(Script) const int& Get() const { return n; } };','reference return requires'),
        'mutable-ref':('struct OV_TYPE() Bad { OV_METHOD(Script) void Set(int& n) { n=0; } };','mutable reference'),
        'thread':('struct OV_TYPE() Bad { OV_FIELD(Script, Thread = Any) int n; };','requires Main thread'),
        'ownership':('struct OV_TYPE() Other {}; struct OV_TYPE() Bad { OV_METHOD(Script) Other* Get(); };','require Ownership'),
        'nullable':('struct OV_TYPE() Other {}; struct OV_TYPE() Bad { OV_METHOD(Script, Ownership = Borrowed) Other* Get(); };','explicit Nullable'),
        'overload':('struct OV_TYPE() Bad { OV_METHOD(Script, Overload = "int (float)") int Get(int); int Get(float); };','exactly match canonical signature'),
        'rvalue-method':('struct OV_TYPE() Bad { OV_METHOD(Script) int Get() &&; };','callable on an lvalue'),
        'non-script-policy':('struct OV_TYPE() Bad { OV_FIELD(Return = Copy) int n; };','requires Script'),
    }
    for name,(source,diagnostic) in negatives.items():
        directory=output/'Negative'/name;directory.mkdir(parents=True)
        scan=directory/'bad.cpp';scan.write_text('#include <OvReflection/Annotations.h>\n'+source+'\n')
        entry=r.read_json(output/'Export'/r.TARGET/'Debug/OvMaths/compile_commands.json')[0]
        entry['file']=scan.as_posix();entry['arguments'][-1]=scan.as_posix();r.write_json(directory/'compile_commands.json',[entry])
        log=v.run('negative-'+name,[generator,'-p',directory,'--source-root='+ROOT.as_posix(),'--module=Fixture','--configuration=Debug','--report='+(directory/'analysis-report.json').as_posix(),scan],success=False)
        require(diagnostic in log and ('bad.cpp:' in log or 'bad.cpp(2,' in log),'Missing expected source diagnostic: '+name)
    def receipt():
        return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for folder in ['Sources','Tools/OvReflectGen','Tests/Reflection'] for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix in ['.h','.cpp','.cs','.py','.json','.lua','.inl'] and 'Projects' not in p.parts}
    source_receipt=receipt();r.write_json(output/'source-receipt.json',source_receipt)
    for config in args.config:
        build=output/config;build.mkdir();fixture=build/'Fixture';fixture.mkdir()
        if not args.skip_build:
            command=[Path(tc['vs_root'])/'MSBuild/Current/Bin/amd64/MSBuild.exe',ROOT/'Overload.sln','/nologo','/m:2','/nr:false','/v:minimal','/t:Build','/p:Configuration='+config,'/p:Platform=x64','/p:PreferredToolArchitecture=x64','/p:PROCESSOR_ARCHITECTURE=AMD64','/p:VcpkgEnabled=false','/p:VcpkgApplocalDeps=false','/p:VCToolsVersion='+tc['msvc_version'],'/p:WindowsTargetPlatformVersion='+tc['sdk_version']]
            v.run(config+'/build',command)
            before=snapshot(ROOT/'Bin-Int'/config,'*.obj'); generated=snapshot(ROOT/'Build/Generated/Reflection'/r.TARGET/config)
            v.run(config+'/no-change',command)
            require(before==snapshot(ROOT/'Bin-Int'/config,'*.obj'),'No-change build compiled objects')
            require(generated==snapshot(ROOT/'Build/Generated/Reflection'/r.TARGET/config),'No-change build modified outputs')
        entry=r.read_json(output/'Export'/r.TARGET/config/'OvCore/compile_commands.json')[0]
        scan=fixture/'Reflection.scan.cpp';scan.write_text('#include "Tests/Reflection/Fixtures/R6/Values.h"\n')
        entry['file']=scan.as_posix();entry['arguments'][-1]=scan.as_posix();entry['arguments'].insert(1,'/I'+ROOT.as_posix())
        r.write_json(fixture/'compile_commands.json',[entry])
        v.run(config+'/generate',[generator,'-p',fixture,'--source-root='+ROOT.as_posix(),'--module=Fixture','--configuration='+config,'--report='+(fixture/'analysis-report.json').as_posix(),scan])
        schema=r.read_json(fixture/'schema.json')
        r.emit_cpp(schema,fixture,types=[t for t in schema['types'] if t['cpp_name'].startswith('R6::')])
        first={name:(fixture/name).read_bytes() for name in r.CPP_OUTPUTS}
        r.emit_cpp(schema,fixture,types=[t for t in schema['types'] if t['cpp_name'].startswith('R6::')])
        require(first=={name:(fixture/name).read_bytes() for name in r.CPP_OUTPUTS},'Non-deterministic binding output')
        abi=r.read_json(fixture/'script-abi.json');signature=abi['signature_sha256']
        options=[a for a in entry['arguments'] if a.startswith(('/I','/D','/U')) and a!='/DOV_REFLECTION_ANALYSIS=1']
        common=['/nologo','/std:c++20','/EHsc','/MDd' if config=='Debug' else '/MD','/Od' if config=='Debug' else '/O2','/I'+fixture.as_posix(),*options]
        for name,tool in [('MSVC',[compiler/'cl.exe']),('Clang',[tc['clang'],'--driver-mode=cl'])]:
            objects=[]
            for stem,source in [('generated',fixture/'Reflection.generated.cpp'),('bridge',ROOT/'Sources/OvReflection/src/OvReflection/ScriptBridge.cpp'),('reflection',ROOT/'Sources/OvReflection/src/OvReflection/Reflection.cpp'),('host',ROOT/'Tests/Reflection/Integration/script_validation.cpp')]:
                obj=build/(name+'-'+stem+'.obj')
                v.run(config+'/'+name+'-'+stem,[*tool,*common,'/W4','/WX','/c',source,'/Fo'+obj.as_posix()]);objects.append(obj)
            libs=[ROOT/'Bin'/config/'OvMaths/OvMaths.lib']
            dll=build/(name+'-fixture.dll')
            v.run(config+'/'+name+'-dll',[compiler/'link.exe','/nologo','/DLL','/OUT:'+dll.as_posix(),*objects,*libs])
            native=build/(name+'-native.obj');exe=build/(name+'-native.exe')
            v.run(config+'/'+name+'-native-compile',[*tool,*common,'/DR6_NATIVE_EXECUTE','/DR6_SIGNATURE_BYTES='+','.join('0x'+signature[i:i+2] for i in range(0,64,2)),'/c',ROOT/'Tests/Reflection/Integration/script_validation.cpp','/Fo'+native.as_posix()])
            v.run(config+'/'+name+'-native-link',[compiler/'link.exe','/nologo','/OUT:'+exe.as_posix(),*objects[:-1],native,*libs])
            require('PASS:' in v.run(config+'/'+name+'-native-run',[exe]),'Native execution incomplete')
            # Check the generated public ABI is also legal C, not just extern C C++.
            c=build/'abi.c';c.write_text('#include "Script.generated.h"\nint r6_abi_size(void) { return sizeof(OvScriptFixtureTable) > 0; }\n')
            v.run(config+'/'+name+'-c-header',[*tool,'/nologo','/TC','/W4','/WX','/I'+fixture.as_posix(),'/I'+(ROOT/'Sources/OvReflection/include').as_posix(),'/c',c,'/Fo'+(build/(name+'-c.obj')).as_posix()])
            managed=build/(name+'-managed.dll')
            rsp=build/(name+'-csharp.rsp')
            sources=[ROOT/'Sources/OvReflection/Managed/ScriptBridge.cs',fixture/'Script.generated.cs',ROOT/'Tests/Reflection/Integration/ScriptValidation.cs']
            rsp.write_text('\n'.join(['/nologo','/nostdlib+','/unsafe+','/langversion:latest','/nullable:enable','/warnaserror+','/target:exe','/optimize+','/out:"'+str(managed)+'"']+['/reference:"'+str(p)+'"' for p in references]+['"'+str(p)+'"' for p in sources])+'\n',encoding='utf-8')
            v.run(config+'/'+name+'-csharp',[csc,'/noconfig','@'+str(rsp)])
            r.write_json(managed.with_suffix('.runtimeconfig.json'),{'runtimeOptions':{'tfm':'net'+runtime.name.split('.')[0]+'.0','framework':{'name':'Microsoft.NETCore.App','version':runtime.name}}})
            plugin=build/(name+'-reload.dll')
            plugin_rsp=build/(name+'-reload.rsp')
            text=rsp.read_text(encoding='utf-8').replace('/target:exe','/target:library').replace(str(managed),str(plugin)).replace(str(ROOT/'Tests/Reflection/Integration/ScriptValidation.cs'),str(ROOT/'Tests/Reflection/Integration/ScriptReload.cs'))
            plugin_rsp.write_text(text,encoding='utf-8')
            v.run(config+'/'+name+'-reload-compile',[csc,'/noconfig','@'+str(plugin_rsp)])
            require('PASS:' in v.run(config+'/'+name+'-coreclr',[args.dotnet,managed,dll,plugin]),'CoreCLR execution incomplete')
            if name=='MSVC':
                altered=build/'Mismatched.generated.cs'
                altered.write_text((fixture/'Script.generated.cs').read_text(encoding='utf-8').replace(signature,('0' if signature[0]!='0' else '1')+signature[1:]),encoding='utf-8')
                mismatch=build/'mismatch.dll';mismatch_rsp=build/'mismatch.rsp'
                mismatch_rsp.write_text(rsp.read_text(encoding='utf-8').replace(str(managed),str(mismatch)).replace(str(fixture/'Script.generated.cs'),str(altered)),encoding='utf-8')
                v.run(config+'/mismatch-compile',[csc,'/noconfig','@'+str(mismatch_rsp)])
                r.write_json(mismatch.with_suffix('.runtimeconfig.json'),r.read_json(managed.with_suffix('.runtimeconfig.json')))
                log=v.run(config+'/mismatch-load',[args.dotnet,mismatch,dll,plugin],success=False)
                require('status 8' in log,'Managed mismatched ABI was not rejected at load')
        if args.engine:
            from verify_r5 import PROJECTS
            generated=ROOT/'Build/Generated/Reflection'/r.TARGET/config
            signature=r.read_json(generated/'OvCore/script-abi.json')['signature_sha256']
            host=build/'engine-host.obj';dll=build/'engine-host.dll'
            engine_options=[*common,'/I'+generated.as_posix(),'/I'+(ROOT/'Dependencies/glfw/include').as_posix(),'/I'+(ROOT/'Dependencies/bullet3/src').as_posix(),'/DR6_CORE_SIGNATURE_BYTES='+','.join('0x'+signature[i:i+2] for i in range(0,64,2))]
            v.run(config+'/engine-compile',[compiler/'cl.exe',*engine_options,'/c',ROOT/'Tests/Reflection/Integration/script_engine_host.cpp','/Fo'+host.as_posix()])
            libraries=[ROOT/'Bin'/config/p/(p+'.lib') for p in PROJECTS]
            v.run(config+'/engine-link',[compiler/'link.exe','/nologo','/DLL','/OPT:REF','/OPT:ICF','/OUT:'+dll.as_posix(),host,*libraries,'opengl32.lib','dbghelp.lib','ws2_32.lib','winmm.lib','imm32.lib','version.lib','user32.lib','gdi32.lib','shell32.lib','ole32.lib','oleaut32.lib','advapi32.lib','comdlg32.lib','uuid.lib'])
            managed_engine=build/'managed-engine.dll';engine_rsp=build/'engine.rsp'
            text=rsp.read_text(encoding='utf-8').replace(str(managed),str(managed_engine)).replace(str(fixture/'Script.generated.cs'),str(generated/'OvCore/Script.generated.cs')).replace(str(ROOT/'Tests/Reflection/Integration/ScriptValidation.cs'),str(ROOT/'Tests/Reflection/Integration/ScriptEngineValidation.cs'))
            engine_rsp.write_text(text,encoding='utf-8')
            v.run(config+'/engine-csharp',[csc,'/noconfig','@'+str(engine_rsp)])
            r.write_json(managed_engine.with_suffix('.runtimeconfig.json'),r.read_json(managed.with_suffix('.runtimeconfig.json')))
            for mode in ['default','render-thread']:
                require('PASS:' in v.run(config+'/engine-'+mode,[args.dotnet,managed_engine,dll,mode]),'Managed engine behavior incomplete')
    require(receipt()==source_receipt,'Sources changed during validation; rerun with fixed sources')
    r.write_json(output/'verification.json',{'stage':'R6','success':True,'full_build':not args.skip_build,'engine':args.engine,'managed_runtime':str(runtime),'checks':v.checks})
    print('PASS: R6 evidence: '+str(output/'verification.json'),flush=True)
if __name__=='__main__':
    try: main()
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print('R6 verification failed: '+str(error),file=sys.stderr);sys.exit(1)
