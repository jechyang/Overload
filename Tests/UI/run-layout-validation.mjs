import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const parent=path.join(root,'Build/UGUI-D');fs.mkdirSync(parent,{recursive:true});
const output=fs.mkdtempSync(path.join(parent,'layout-'));
// Keep compiler temporaries and collectible CLR copies beside this run on the workspace drive.
const temporary=path.join(output,'temp'); fs.mkdirSync(temporary,{recursive:true});
process.env.TEMP=temporary; process.env.TMP=temporary;
process.env.OV_SCRIPTING_SHADOW_ROOT=path.join(output,'managed-shadow');
const lock=JSON.parse(fs.readFileSync(path.join(root,'Tools/OvCSharp/toolchain.lock.json'),'utf8'));
const runtime=path.join(root,'Build/Toolchains/dotnet-'+lock.sdk_version);
const refs=path.join(runtime,'packs/Microsoft.NETCore.App.Ref',lock.runtime_version,'ref',lock.target_framework);
const dotnet=path.join(runtime,'dotnet.exe');
const checks=[];
const revision=cp.spawnSync('git',['-c','safe.directory='+root.replaceAll('\\','/'),'rev-parse','HEAD'],{cwd:root,windowsHide:true,encoding:'utf8'});
const evidence={command:process.argv,commit:revision.status===0?revision.stdout.trim():null,sources:{},libraries:[]};
for(const file of ['Sources/OvCore/include/OvCore/UI/DrivenProperties.h','Sources/OvCore/src/OvCore/Scripting/CSharp/DrivenBridge.cpp','Tests/UI/LayoutContracts.cs','Tests/UI/LayoutRuntime.cs','Tests/UI/layout_runtime_validation.cpp','Tests/UI/driven_properties_validation.cpp'])
 evidence.sources[file]=crypto.createHash('sha256').update(fs.readFileSync(path.join(root,file))).digest('hex');
let nativeEnv;
function run(name,exe,args){
 const result=cp.spawnSync(exe,args,{cwd:root,env:nativeEnv,windowsHide:true,encoding:'utf8',timeout:300000});
 fs.writeFileSync(path.join(output,name+'.log'),(result.stdout||'')+(result.stderr||''));
 checks.push({name,exit:result.status,error:String(result.error||'')});
 if(result.status!==0)throw Error(name+' failed: '+path.join(output,name+'.log'));
 console.log('PASS '+name);
 if(name==='layout-contracts'||name==='native-driven')console.log(result.stdout.trim());
}
try {
 const source=path.join(root,'Sources/OvManaged/Overload.ScriptCore');
 const sources=fs.readdirSync(source).filter(x=>x.endsWith('.cs'));
 for(const file of sources)evidence.sources[file]=crypto.createHash('sha256').update(fs.readFileSync(path.join(source,file))).digest('hex');
 const response=path.join(output,'tests.rsp'),dll=path.join(output,'LayoutTests.dll');
 fs.writeFileSync(response,[
 '/nologo','/nostdlib+','/langversion:14.0','/nullable:enable','/warnaserror+','/unsafe+','/deterministic+','/platform:x64','/target:exe',
 '/out:'+JSON.stringify(dll),
 ...fs.readdirSync(refs).filter(x=>x.endsWith('.dll')).map(x=>'/reference:'+JSON.stringify(path.join(refs,x))),
 ...sources.map(x=>JSON.stringify(path.join(source,x))),
 JSON.stringify(path.join(root,'Tests/UI/LayoutContracts.cs'))].join('\n'));
 run('compile',dotnet,[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+response]);
 fs.writeFileSync(path.join(output,'LayoutTests.runtimeconfig.json'),JSON.stringify({runtimeOptions:{tfm:lock.target_framework,framework:{name:'Microsoft.NETCore.App',version:lock.runtime_version}}}));
 run('layout-contracts',dotnet,[dll]);
 const generated=path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug');
 const args=JSON.parse(fs.readFileSync(path.join(generated,'OvCore/compile_commands.json'),'utf8'))[0].arguments;
 const system=args.filter(x=>x.startsWith('/clang:C:/')).map(x=>x.slice(7));
 const msvc=path.dirname(system.find(x=>x.includes('/MSVC/')&&x.endsWith('/include')));
 const ucrt=system.find(x=>x.endsWith('/ucrt')),version=path.basename(path.dirname(ucrt)),sdk=path.resolve(ucrt,'../../..');
 nativeEnv={...process.env,LIB:[path.join(msvc,'lib/x64'),...['ucrt','um'].map(x=>path.join(sdk,'Lib',version,x,'x64'))].join(';')};
 const exe=path.join(output,'driven.exe');
 run('native-driven-compile',path.join(msvc,'bin/Hostx64/x64/cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd',
 ...args.filter(x=>(x.startsWith('/I')||x.startsWith('/D'))&&!x.includes('OV_REFLECTION_ANALYSIS')),...system.map(x=>'/I'+x),
 path.join(root,'Tests/UI/driven_properties_validation.cpp'),path.join(root,'Bin/Debug/OvMaths/OvMaths.lib'),'/Fo'+path.join(output,'driven.obj'),'/Fe'+exe]);
 run('native-driven',exe,[]);
 if(process.argv.includes('--runtime')) {
   const managed=path.join(output,'managed'),project=path.join(output,'project');fs.mkdirSync(project,{recursive:true});
   run('managed-host',process.execPath,[path.join(root,'Scripts/Windows/BuildCSharp.mjs'),'--compat',path.join(root,'Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj'),'--rebuild','--artifacts-path',path.join(output,'artifacts'),'--output',managed]);
   const fixture=path.join(project,'LayoutRuntime.dll'),rsp=path.join(output,'fixture.rsp');
   fs.writeFileSync(rsp,['/nologo','/nostdlib+','/langversion:14.0','/nullable:enable','/warnaserror+','/platform:x64','/target:library','/out:'+JSON.stringify(fixture),
     '/reference:'+JSON.stringify(path.join(managed,'Overload.ScriptCore.dll')),
     ...fs.readdirSync(refs).filter(x=>x.endsWith('.dll')).map(x=>'/reference:'+JSON.stringify(path.join(refs,x))),JSON.stringify(path.join(root,'Tests/UI/LayoutRuntime.cs'))].join('\n'));
   run('fixture',dotnet,[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+rsp]);
   const obj=path.join(output,'runtime.obj'),native=path.join(output,'layout-runtime.exe');
   const includes=[...args.filter(x=>(x.startsWith('/I')||x.startsWith('/D'))&&!x.includes('OV_REFLECTION_ANALYSIS')),...system.map(x=>'/I'+x),'/I'+root,'/I'+generated,...['glfw/include','bullet3/src'].map(x=>'/I'+path.join(root,'Dependencies',x))];
   run('runtime-compile',path.join(msvc,'bin/Hostx64/x64/cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...includes,path.join(root,'Tests/UI/layout_runtime_validation.cpp'),'/Fo'+obj]);
   const libraries=['OvReflection','OvCore','OvRendering','OvUI','OvTools','OvMaths','OvAudio','OvDebug','OvPhysics','OvWindowing','assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'];
   evidence.libraries=libraries.map(x=>{const file=path.join(root,'Bin/Debug',x,x+'.lib'),stat=fs.statSync(file);return {file,size:stat.size,modified:stat.mtime.toISOString()};});
   run('runtime-link',path.join(msvc,'bin/Hostx64/x64/link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+native,obj,...libraries.map(x=>path.join(root,'Bin/Debug',x,x+'.lib')),...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x=>x+'.lib')]);
   for(const mode of ['default','render-thread']) {
     run('runtime-'+mode,native,[runtime,managed,fixture,mode]);
     if(fs.readFileSync(path.join(output,'runtime-'+mode+'.log'),'utf8').includes('[ERROR]'))throw Error('Runtime logged an engine or managed error');
   }
   const serial=fs.readFileSync(path.join(project,'default.rgba')),threaded=fs.readFileSync(path.join(project,'render-thread.rgba'));
   if(serial.length!==26*300*200*4||!serial.equals(threaded))throw Error('All 26 real layout frames must match exactly across RenderThread');
   evidence.pixels={frames:26,width:300,height:200,sha256:crypto.createHash('sha256').update(serial).digest('hex'),exactRenderThreadMatch:true};
 }

 fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({...evidence,success:true,scope:process.argv.includes('--runtime')?'production C# contracts plus native runtime, pixels, reload and session restoration':'production C# components with deterministic native-access double; no GPU claim',checks},null,2));
 console.log('Evidence: '+output);
} catch(error){fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({...evidence,success:false,checks,error:String(error)},null,2));console.error(String(error));process.exitCode=1;}
