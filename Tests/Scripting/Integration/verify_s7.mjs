import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const args=process.argv.slice(2),python=args[args.indexOf('--python')+1];
if(!args.includes('--python')||!path.isAbsolute(python))throw Error('--python requires an absolute interpreter path');
const configs=args.includes('--config')?args[args.indexOf('--config')+1].split(','):['Debug','Release','Publish'];
if(configs.some(c=>!['Debug','Release','Publish'].includes(c)))throw Error('Invalid configuration');
const headless=args.includes('--headless');
const evidenceArg=args.includes('--evidence')?args[args.indexOf('--evidence')+1]:null;
const evidence=evidenceArg||process.env.OV_SCRIPTING_EVIDENCE_ROOT||path.join(root,'Build/Tests/Scripting');
if(!path.isAbsolute(evidence))throw Error('--evidence requires an absolute directory path');
fs.mkdirSync(evidence,{recursive:true});
const physical=fs.mkdtempSync(path.join(evidence,'s7-')),output=path.join(root,'Build/Tests/Scripting',path.basename(physical));
if(path.resolve(physical)!==path.resolve(output))fs.symlinkSync(physical,output,'junction');
console.log('S7 evidence: '+output);
const env={...process.env,PYTHONDONTWRITEBYTECODE:'1',VSLANG:'1033',DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_NOLOGO:'1',DOTNET_MULTILEVEL_LOOKUP:'0',GIT_CONFIG_GLOBAL:'NUL'};
for(const k of Object.keys(env))if(/^(CscToolExe|CscToolPath|CscEnvironment|RoslynTargetsPath|RoslynCompilerType|SkipCompilerExecution|UseSharedCompilation|UseHostCompilerIfAvailable|MSBuildSDKsPath|CompilerToolPath)$/i.test(k))delete env[k];
const checks=[],results=[],blockers=[];
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const below=p=>fs.readdirSync(p,{withFileTypes:true}).flatMap(e=>e.isDirectory()?(['bin','obj'].includes(e.name)?[]:below(path.join(p,e.name))):[path.join(p,e.name)]);
const sources=['Sources/OvCore/include/OvCore/Scripting','Sources/OvCore/src/OvCore/Scripting','Sources/OvManaged','Tests/Scripting','Tools/OvCSharp','Scripts/Windows'].flatMap(p=>below(path.join(root,p))).filter(p=>!p.includes('ResolveCSharpBuild'));
sources.push(...['Utils/SystemCalls.cpp','Filesystem/IniFile.cpp'].map(p=>path.join(root,'Sources/OvTools/src/OvTools',p)));
const snapshot=()=>Object.fromEntries(sources.map(p=>[path.relative(root,p),hash(p)]));const before=snapshot();
function run(name,exe,argv,{expected=0,cwd=root,timeout=900000,childEnv=env}={}){
  console.log(name);const r=spawnSync(exe,argv,{cwd,env:childEnv,windowsHide:true,encoding:'utf8',timeout,maxBuffer:128*1024*1024});
  const text=(r.stdout||'')+(r.stderr||'')+(r.error||'');fs.writeFileSync(path.join(output,name+'.log'),text);
  checks.push({name,argv:[exe,...argv],cwd,exit_code:r.status,error:r.error?.message});
  if(expected!==null&&r.status!==expected)throw Error(name+' exit '+r.status+'; see log');return {text,status:r.status};
}
try {
  run('export',python,[path.join(root,'Tools/OvReflectGen/reflection.py'),'export','--output',path.join(output,'Export')]);
  const tc=JSON.parse(fs.readFileSync(path.join(output,'Export/toolchain.json'),'utf8'));
  const compiler=path.join(tc.msvc_root,'bin/Hostx64/x64');env.INCLUDE=tc.system_includes.join(';');
  env.LIB=[path.join(tc.msvc_root,'lib/x64'),...['ucrt','um'].map(p=>path.join(tc.sdk_root,'Lib',tc.sdk_version,p,'x64'))].join(';');
  const pk=Object.keys(env).find(k=>k.toLowerCase()==='path'),prior=env[pk];delete env[pk];env.PATH=compiler+';'+prior;
  const dotnetRoot=path.join(root,'Build/Toolchains/dotnet-10.0.401'),dotnet=path.join(dotnetRoot,'dotnet.exe');
  env.DOTNET_ROOT=dotnetRoot;env.DOTNET_CLI_HOME=path.join(output,'cli');env.NUGET_PACKAGES=path.join(output,'packages');
  if(run('sdk-version',dotnet,['--version']).text.trim()!=='10.0.401')throw Error('SDK pin mismatch');
  run('premake',path.join(root,'Dependencies/premake5/bin/premake5.exe'),['--reflection-python='+python,'vs2022']);
  const names=['OvReflection','OvAudio','OvCore','OvDebug','OvMaths','OvPhysics','OvRendering','OvTools','OvUI','OvWindowing','assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'];
  for(const config of configs){
    const work=path.join(output,config);fs.mkdirSync(work);const gen=path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc',config);
    const buildArgs=[path.join(root,'Overload.sln'),'/nologo','/m:2','/nr:false','/v:minimal','/t:Build','/p:Configuration='+config,'/p:Platform=x64','/p:PreferredToolArchitecture=x64','/p:PROCESSOR_ARCHITECTURE=AMD64','/p:VcpkgEnabled=false','/p:VcpkgApplocalDeps=false','/p:LinkIncremental=false','/p:VCToolsVersion='+tc.msvc_version,'/p:WindowsTargetPlatformVersion='+tc.sdk_version];
    const msbuild=path.join(tc.vs_root,'MSBuild/Current/Bin/amd64/MSBuild.exe');run(config+'-native-build',msbuild,buildArgs,{timeout:1800000});
    const objectSnapshot=()=>Object.fromEntries(below(path.join(root,'Bin-Int',config)).filter(p=>p.endsWith('.obj')).map(p=>[p,{hash:hash(p),mtime:fs.statSync(p).mtimeMs}]));
    const objects=objectSnapshot();run(config+'-native-no-change',msbuild,buildArgs);
    if(JSON.stringify(objects)!==JSON.stringify(objectSnapshot()))throw Error(config+' no-change native objects changed');
    const project=path.join(work,'project');fs.mkdirSync(project);fs.copyFileSync(path.join(root,'Tests/Scripting/Fixtures/S7/Types.cs'),path.join(project,'Types.cs'));
    fs.copyFileSync(path.join(root,'Tests/Scripting/Fixtures/S3/legacy.lua'),path.join(project,'s3.lua'));
    fs.writeFileSync(path.join(project,'GameScripts.csproj'),`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework><PlatformTarget>x64</PlatformTarget><Nullable>enable</Nullable><RestorePackagesWithLockFile>true</RestorePackagesWithLockFile><RestoreLockedMode>true</RestoreLockedMode></PropertyGroup><ItemGroup><ProjectReference Include="${root.replaceAll('\\','/')}/Sources/OvManaged/Overload.ScriptCore/Overload.ScriptCore.csproj" /></ItemGroup></Project>`);
    fs.writeFileSync(path.join(project,'packages.lock.json'),JSON.stringify({version:1,dependencies:{'net10.0':{'overload.scriptcore':{type:'Project'}}}}));
    fs.copyFileSync(path.join(root,'Sources/OvManaged/Overload.ManagedHost/NuGet.Config'),path.join(project,'NuGet.Config'));
    const hostProject=path.join(root,'Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj');
    const standardProps=['--artifacts-path',path.join(work,'standard-artifacts'),'-p:Configuration='+config,'-p:ReflectionConfiguration='+config];
    run(config+'-standard-restore',dotnet,['restore',hostProject,'--locked-mode','--configfile',path.join(path.dirname(hostProject),'NuGet.Config'),...standardProps]);
    const standard=run(config+'-standard-build',dotnet,['build',hostProject,'--no-restore','-t:Rebuild',...standardProps],{expected:null});
    if(standard.status!==0){if(!/CET/.test(standard.text))throw Error('Unexpected standard build failure');blockers.push({config,check:'standard-dotnet-build',reason:'CET',exit_code:standard.status});}
    const hostDir=path.join(work,'host'),gameDir=path.join(work,'game');
    for(const [kind,csproj,destination] of [['host',hostProject,hostDir],['game',path.join(project,'GameScripts.csproj'),gameDir]]){
      const argv=[path.join(root,'Scripts/Windows/BuildCSharp.mjs'),'--compat',csproj,'--configuration',config,'--artifacts-path',path.join(work,'artifacts',kind),'--output',path.join(work,'build',kind),'--publish',destination];
      run(config+'-'+kind+'-build-publish',process.execPath,[...argv,'--rebuild']);
      const dlls=()=>Object.fromEntries(below(destination).filter(p=>p.endsWith('.dll')).map(p=>[p,{hash:hash(p),mtime:fs.statSync(p).mtimeMs}]));const built=dlls();
      run(config+'-'+kind+'-no-change',process.execPath,argv);
      if(JSON.stringify(built)!==JSON.stringify(dlls()))throw Error(config+' '+kind+' unchanged publish altered DLL bytes or mtime');
    }
    for(const file of ['GameScripts.dll','GameScripts.deps.json','Overload.ScriptCore.dll'])if(!fs.existsSync(path.join(gameDir,file)))throw Error('Incomplete project publish: '+file);
    for(const file of ['Overload.ManagedHost.dll','Overload.ManagedHost.deps.json','Overload.ManagedHost.runtimeconfig.json','Overload.ScriptCore.dll'])if(!fs.existsSync(path.join(hostDir,file)))throw Error('Incomplete host publish: '+file);
    fs.copyFileSync(path.join(project,'s3.lua'),path.join(gameDir,'s3.lua'));
    const entry=JSON.parse(fs.readFileSync(path.join(output,'Export/x86_64-pc-windows-msvc',config,'OvCore/compile_commands.json'),'utf8'))[0];
    const nativeOptions=['/I'+gen,'/I'+path.join(root,'Dependencies/glfw/include'),...entry.arguments.filter(a=>/^\/[IDU]/.test(a)&&a!=='/DOV_REFLECTION_ANALYSIS=1')];
    const object=path.join(work,'headless.obj'),exe=path.join(work,'headless.exe');
    run(config+'-headless-compile',path.join(compiler,'cl.exe'),['/nologo','/std:c++20','/EHsc',config==='Debug'?'/MDd':'/MD',...(config==='Debug'?[]:['/O2']),'/c',...nativeOptions,path.join(root,'Tests/Scripting/Integration/native_s7_headless.cpp'),'/Fo'+object]);
    const libraries=names.map(n=>path.join(root,'Bin',config,n,n+'.lib'));
    run(config+'-headless-link',path.join(compiler,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+exe,object,...libraries,...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(n=>n+'.lib')]);
    const raw=path.join(work,'performance.jsonl');run(config+'-headless-runtime',exe,[dotnetRoot,hostDir,gameDir,raw],{timeout:300000});
    const data=fs.readFileSync(raw,'utf8').trim().split('\n').map(JSON.parse),summary=[];
    for(const workload of ['noop','transform'])for(const count of [1,100,1000]){
      const rows=data.filter(r=>r.kind==='sample'&&r.workload===workload&&r.count===count),times=rows.map(r=>r.nanoseconds/1e6).sort((a,b)=>a-b);
      if(rows.length!==500)throw Error('Incomplete benchmark');summary.push({workload,count,samples:rows.length,warmup:data.find(r=>r.kind==='warmup'&&r.workload===workload&&r.count===count),mean_ms:times.reduce((a,b)=>a+b,0)/times.length,p50_ms:times[250],p95_ms:times[475],allocated_bytes:rows.reduce((s,r)=>s+r.allocated_bytes,0),native_to_managed:rows.reduce((s,r)=>s+r.native_to_managed,0),managed_to_native:rows.reduce((s,r)=>s+r.managed_to_native,0),gc:[0,1,2].map(g=>rows.reduce((s,r)=>s+r['gen'+g],0))});
    }
    fs.writeFileSync(path.join(work,'performance-summary.json'),JSON.stringify({configuration:config,cpu:os.cpus()[0]?.model,logical_cpus:os.cpus().length,os:os.release(),scope:'Main Scene.Update only; no renderer, physics step, or counter probe inside timer; GC allocations on managed Main; 2 diagnostic ABI probes excluded per sample; no forced GC in measured window',summary},null,2));
    results.push({config,standard_build_passed:standard.status===0,compat_msbuild_build_publish:true,native_no_change:true,managed_no_change:true,headless:true,summary,host_sha256:hash(path.join(hostDir,'Overload.ManagedHost.dll')),native_exe_sha256:hash(exe)});
    if(!headless){const {verifyPackage}=await import('./verify_s7_package.mjs');await verifyPackage({root,output:work,run,env,config,hostDir,gameDir,dotnetRoot,python,compiler,nativeOptions,tc});}
  }
  if(JSON.stringify(before)!==JSON.stringify(snapshot()))throw Error('Source drift during S7 verification');
  fs.writeFileSync(path.join(output,'verification.json'),JSON.stringify({stage:headless?'S7-headless':'S7',success:true,scope:headless?'CPU/ABI/GC/build only':'packaging and local acceptance',timestamp_utc:new Date().toISOString(),configs,results,external_blockers:blockers,checks,source_receipt:before,independent_machine_vm:'not executed',remote_ci:'not executed'},null,2));
  console.log('S7 verification completed: '+output);
}catch(error){fs.writeFileSync(path.join(output,'failure.json'),JSON.stringify({error:String(error),checks,results,external_blockers:blockers},null,2));console.error(String(error));process.exitCode=1;}
