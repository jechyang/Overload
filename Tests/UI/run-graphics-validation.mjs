import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const parent=path.join(root,'Build/UGUI-B');fs.mkdirSync(parent,{recursive:true});
const output=fs.mkdtempSync(path.join(parent,'graphics-'));
// Keep compiler temporaries and collectible CLR copies beside this run on the workspace drive.
const temporary=path.join(output,'temp'); fs.mkdirSync(temporary,{recursive:true});
process.env.TEMP=temporary; process.env.TMP=temporary;
process.env.OV_SCRIPTING_SHADOW_ROOT=path.join(output,'managed-shadow');
const lock=JSON.parse(fs.readFileSync(path.join(root,'Tools/OvCSharp/toolchain.lock.json'),'utf8'));
const runtime=path.join(root,'Build/Toolchains/dotnet-'+lock.sdk_version);
const refs=path.join(runtime,'packs/Microsoft.NETCore.App.Ref',lock.runtime_version,'ref',lock.target_framework);
const checks=[],sources={};let env={...process.env,VSLANG:'1033'};
const revision=cp.spawnSync('git',['-c','safe.directory='+root,'rev-parse','HEAD'],{cwd:root,windowsHide:true,encoding:'utf8'});
const metadata={startedAt:new Date().toISOString(),command:process.argv,configuration:'Debug',architecture:'x64',node:process.version,sdk:lock.sdk_version,runtime:lock.runtime_version,gitHead:revision.stdout.trim(),output};
function run(name,exe,args){const result=cp.spawnSync(exe,args,{cwd:root,env,windowsHide:true,encoding:'utf8',timeout:300000});fs.writeFileSync(path.join(output,name+'.log'),(result.stdout||'')+(result.stderr||''));checks.push({name,executable:exe,arguments:args,exit:result.status,error:String(result.error||'')});if(result.status!==0)throw Error(name+' failed: '+path.join(output,name+'.log'));console.log('PASS '+name);}
try{
 if(!process.argv.includes('--native-only')) {
 const source=path.join(root,'Sources/OvManaged/Overload.ScriptCore');const files=fs.readdirSync(source).filter(x=>x.endsWith('.cs'));
 for(const file of files)sources[file]=crypto.createHash('sha256').update(fs.readFileSync(path.join(source,file))).digest('hex');
 const dll=path.join(output,'GraphicTests.dll'),rsp=path.join(output,'tests.rsp');
 fs.writeFileSync(rsp,['/nologo','/nostdlib+','/langversion:14.0','/nullable:enable','/warnaserror+','/unsafe+','/deterministic+','/platform:x64','/target:exe','/out:'+JSON.stringify(dll),...fs.readdirSync(refs).filter(x=>x.endsWith('.dll')).map(x=>'/reference:'+JSON.stringify(path.join(refs,x))),...files.map(x=>JSON.stringify(path.join(source,x))),JSON.stringify(path.join(root,'Tests/UI/GraphicContracts.cs'))].join('\n'));
 run('managed-compile',path.join(runtime,'dotnet.exe'),[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+rsp]);
 fs.writeFileSync(path.join(output,'GraphicTests.runtimeconfig.json'),JSON.stringify({runtimeOptions:{tfm:lock.target_framework,framework:{name:'Microsoft.NETCore.App',version:lock.runtime_version}}}));
 run('managed-contracts',path.join(runtime,'dotnet.exe'),[dll]);
 }
 if(!process.argv.includes('--managed-only')){
 const generated=path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug');
 const args=JSON.parse(fs.readFileSync(path.join(generated,'OvCore/compile_commands.json'),'utf8'))[0].arguments;
 const system=args.filter(x=>x.startsWith('/clang:C:/')).map(x=>x.slice(7));const msvc=path.dirname(system.find(x=>x.includes('/MSVC/')&&x.endsWith('/include')));
 const ucrt=system.find(x=>x.endsWith('/ucrt')),version=path.basename(path.dirname(ucrt)),sdk=path.resolve(ucrt,'../../..');
 env.LIB=[path.join(msvc,'lib/x64'),...['ucrt','um'].map(x=>path.join(sdk,'Lib',version,x,'x64'))].join(';');
 const includes=[...args.filter(x=>(x.startsWith('/I')||x.startsWith('/D'))&&!x.includes('OV_REFLECTION_ANALYSIS')),...system.map(x=>'/I'+x),'/I'+root,'/I'+generated,...['glfw/include','bullet3/src'].map(x=>'/I'+path.join(root,'Dependencies',x))];
 const obj=path.join(output,'native.obj'),exe=path.join(output,'graphics.exe');
 run('native-compile',path.join(msvc,'bin/Hostx64/x64/cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...includes,path.join(root,'Tests/UI/graphics_pipeline_validation.cpp'),'/Fo'+obj]);
 const libraries=['OvReflection','OvCore','OvRendering','OvUI','OvTools','OvMaths','OvAudio','OvDebug','OvPhysics','OvWindowing','assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'];
 run('native-link',path.join(msvc,'bin/Hostx64/x64/link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+exe,obj,...libraries.map(x=>path.join(root,'Bin/Debug',x,x+'.lib')),...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x=>x+'.lib')]);
 run('native-gpu',exe,[output]);
 if(process.argv.includes('--runtime')){
 const managed=path.join(output,'managed'),project=path.join(output,'project');fs.mkdirSync(project,{recursive:true});
 run('host-build',process.execPath,[path.join(root,'Scripts/Windows/BuildCSharp.mjs'),'--compat',path.join(root,'Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj'),'--rebuild','--artifacts-path',path.join(output,'artifacts'),'--output',managed]);
 const fixture=path.join(project,'GraphicRuntime.dll'),fixtureRsp=path.join(output,'fixture.rsp');
 fs.writeFileSync(fixtureRsp,['/nologo','/nostdlib+','/langversion:14.0','/nullable:enable','/warnaserror+','/platform:x64','/target:library','/out:'+JSON.stringify(fixture),'/reference:'+JSON.stringify(path.join(managed,'Overload.ScriptCore.dll')),...fs.readdirSync(refs).filter(x=>x.endsWith('.dll')).map(x=>'/reference:'+JSON.stringify(path.join(refs,x))),JSON.stringify(path.join(root,'Tests/UI/fixtures/GraphicRuntime.cs'))].join('\n'));
 run('fixture',path.join(runtime,'dotnet.exe'),[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+fixtureRsp]);
 const runtimeObj=path.join(output,'runtime.obj'),runtimeExe=path.join(output,'runtime.exe');
 run('runtime-compile',path.join(msvc,'bin/Hostx64/x64/cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...includes,path.join(root,'Tests/UI/graphics_runtime_validation.cpp'),'/Fo'+runtimeObj]);
 run('runtime-link',path.join(msvc,'bin/Hostx64/x64/link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+runtimeExe,runtimeObj,...libraries.map(x=>path.join(root,'Bin/Debug',x,x+'.lib')),...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x=>x+'.lib')]);
 for(const mode of ['default','render-thread']){run('runtime-'+mode,runtimeExe,[runtime,managed,fixture,mode]);if(fs.readFileSync(path.join(output,'runtime-'+mode+'.log'),'utf8').includes('[ERROR]'))throw Error('Managed lifecycle error in '+mode);}
 if(!fs.readFileSync(path.join(output,'b-default.rgba')).equals(fs.readFileSync(path.join(output,'b-render-thread.rgba'))))throw Error('CLR frame pixels differ by render mode');checks.push({name:'CLR full-frame pixel identity',exit:0});
 }

 }
 fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({success:true,...metadata,sources,checks},null,2));console.log('Evidence: '+output);
}catch(error){fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({success:false,...metadata,error:String(error),sources,checks},null,2));console.error(String(error));process.exitCode=1;}
