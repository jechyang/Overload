import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const parent=path.join(root,'Build/UIValidation'); fs.mkdirSync(parent,{recursive:true});
const output=fs.mkdtempSync(path.join(parent,'F-Debug-'));
const evidence={scope:'F independent input/event consumers; not full U7 GPU acceptance',checks:[]};
const env={...process.env,VSLANG:'1033',DOTNET_CLI_HOME:path.join(output,'cli')};
function run(name,exe,args) {
  const fd=fs.openSync(path.join(output,name+'.log'),'w');
  const result=cp.spawnSync(exe,args,{cwd:root,env,windowsHide:true,stdio:['ignore',fd,fd],timeout:180000}); fs.closeSync(fd);
  evidence.checks.push({name,exe,args,exit:result.status,error:String(result.error||'')});
  if(result.status!==0) throw Error(name+' failed; '+path.join(output,name+'.log'));
  console.log('PASS '+name);
}
try {
  const commit=cp.spawnSync('git',['-c',`safe.directory=${root.replaceAll('\\','/')}`,'rev-parse','HEAD'],{cwd:root,encoding:'utf8',windowsHide:true}); evidence.commit=commit.stdout.trim();
  const generated=path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug');
  const args=JSON.parse(fs.readFileSync(path.join(generated,'OvCore/compile_commands.json'),'utf8'))[0].arguments;
  const system=args.filter(x=>x.startsWith('/clang:C:/')).map(x=>x.slice(7));
  const msvc=path.dirname(system.find(x=>x.includes('/MSVC/')&&x.endsWith('/include')));
  const sdkInclude=system.find(x=>x.endsWith('/ucrt')), sdkVersion=path.basename(path.dirname(sdkInclude)),sdk=path.resolve(sdkInclude,'../../..');
  const bin=path.join(msvc,'bin/Hostx64/x64'); env.LIB=[path.join(msvc,'lib/x64'),...['ucrt','um'].map(x=>path.join(sdk,'Lib',sdkVersion,x,'x64'))].join(';');
  const include=[...system.map(x=>'/I'+x),...['Sources/OvCore/include','Sources/OvWindowing/include'].map(x=>'/I'+path.join(root,x))];
  const native=path.join(output,'input.exe'), packet=path.join(output,'snapshot.bin');
  run('native-compile',path.join(bin,'cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/utf-8',...include,path.join(root,'Tests/UI/input_snapshot_validation.cpp'),'/Fo'+path.join(output,'input.obj'),'/Fe'+native]);
  run('native-input',native,[packet]);
  const lock=JSON.parse(fs.readFileSync(path.join(root,'Tools/OvCSharp/toolchain.lock.json'),'utf8'));
  const runtime=path.join(root,'Build/Toolchains/dotnet-'+lock.sdk_version),refs=path.join(runtime,'packs/Microsoft.NETCore.App.Ref',lock.runtime_version,'ref',lock.target_framework);
  const managed=path.join(output,'FEvents.dll'),rsp=path.join(output,'events.rsp');
  const sourceRoot=path.join(root,'Sources/OvManaged/Overload.ScriptCore');
  fs.writeFileSync(rsp,['/nologo','/nostdlib+','/langversion:14.0','/nullable:enable','/warnaserror+','/platform:x64','/target:exe','/out:'+JSON.stringify(managed),
    ...fs.readdirSync(refs).filter(x=>x.endsWith('.dll')).map(x=>'/reference:'+JSON.stringify(path.join(refs,x))),
    ...fs.readdirSync(sourceRoot).filter(x=>x.endsWith('.cs')).map(x=>JSON.stringify(path.join(sourceRoot,x))),
    JSON.stringify(path.join(root,'Tests/UI/fixtures/InputEvents.cs'))].join('\n'));
  fs.writeFileSync(path.join(output,'FEvents.runtimeconfig.json'),JSON.stringify({runtimeOptions:{tfm:lock.target_framework,framework:{name:'Microsoft.NETCore.App',version:lock.runtime_version}}}));
  run('managed-compile',path.join(runtime,'dotnet.exe'),[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+rsp]);
  run('managed-events',path.join(runtime,'dotnet.exe'),[managed,packet]);
  if(process.argv.includes('--compile-production') || process.argv.includes('--runtime') || process.argv.includes('--compile-runtime')) {
    const allIncludes=[...args.filter(x=>(x.startsWith('/I')||x.startsWith('/D'))&&!x.includes('OV_REFLECTION_ANALYSIS')),...system.map(x=>'/I'+x),'/I'+root,'/I'+generated,
      ...['Sources/OvEditor/include','Dependencies/glfw/include','Dependencies/bullet3','Dependencies/bullet3/bullet','Dependencies/ImGui','Dependencies/ImGui/imgui'].map(x=>'/I'+path.join(root,x))];
    for(const source of ['Sources/OvWindowing/src/OvWindowing/Window.cpp','Sources/OvWindowing/src/OvWindowing/Inputs/InputManager.cpp','Sources/OvWindowing/src/OvWindowing/Inputs/UIPlatformInput.cpp','Sources/OvPhysics/src/OvPhysics/Core/PhysicsEngine.cpp','Sources/OvCore/src/OvCore/Scripting/CSharp/InputBridge.cpp','Sources/OvEditor/src/OvEditor/Panels/GameView.cpp']) {
      const name=path.basename(source,'.cpp');
      run('production-'+name,path.join(bin,'cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/utf-8','/c',...allIncludes,path.join(root,source),'/Fo'+path.join(output,name+'.obj')]);
    }
  }
  if(process.argv.includes('--runtime') || process.argv.includes('--compile-runtime')) {
    const host=path.join(output,'host'), artifacts=path.join(output,'artifacts');
    run('managed-host',process.execPath,[path.join(root,'Scripts/Windows/BuildCSharp.mjs'),'--compat',path.join(root,'Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj'),'--artifacts-path',artifacts,'--output',host]);
    const fixture=path.join(output,'FInput.dll'),fixtureRsp=path.join(output,'fixture.rsp');
    fs.writeFileSync(fixtureRsp,['/nologo','/nostdlib+','/langversion:14.0','/nullable:enable','/warnaserror+','/platform:x64','/target:library','/out:'+JSON.stringify(fixture),'/reference:'+JSON.stringify(path.join(host,'Overload.ScriptCore.dll')),
      ...fs.readdirSync(refs).filter(x=>x.endsWith('.dll')).map(x=>'/reference:'+JSON.stringify(path.join(refs,x))),JSON.stringify(path.join(root,'Tests/UI/fixtures/NativeInputContracts.cs'))].join('\n'));
    run('runtime-fixture',path.join(runtime,'dotnet.exe'),[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+fixtureRsp]);
    const allIncludes=[...args.filter(x=>(x.startsWith('/I')||x.startsWith('/D'))&&!x.includes('OV_REFLECTION_ANALYSIS')),...system.map(x=>'/I'+x),'/I'+root,'/I'+generated,
      ...['Dependencies/glfw/include','Dependencies/bullet3','Dependencies/bullet3/bullet'].map(x=>'/I'+path.join(root,x))];
    const object=path.join(output,'runtime.obj'),exe=path.join(output,'runtime.exe');
    run('runtime-compile',path.join(bin,'cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/utf-8','/c',...allIncludes,path.join(root,'Tests/UI/event_system_validation.cpp'),'/Fo'+object]);
    const libraries=['OvReflection','OvCore','OvRendering','OvUI','OvTools','OvMaths','OvAudio','OvDebug','OvPhysics','OvWindowing','assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'];
    if(process.argv.includes('--runtime')) {
    run('runtime-link',path.join(bin,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+exe,object,...libraries.map(x=>path.join(root,'Bin/Debug',x,x+'.lib')),
      ...['opengl32','dbghelp','ws2_32','winmm','imm32','comctl32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x=>x+'.lib')]);
    for(const mode of ['default','render-thread']) run('runtime-'+mode,exe,[runtime,host,fixture,...(mode==='render-thread'?[mode]:[])]);
    }
  }
  evidence.success=true;
} catch(error) { evidence.success=false;evidence.error=String(error);console.error(error);process.exitCode=1; }
finally {fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(evidence,null,2));console.log('Evidence: '+output);}
