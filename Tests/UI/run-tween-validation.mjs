import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const userProfile=process.env.USERPROFILE||'C:\\Users\\Administrator';
Object.assign(process.env,{
  USERPROFILE:userProfile, USERNAME:process.env.USERNAME==='SYSTEM'?'Administrator':(process.env.USERNAME||'Administrator'),
  APPDATA:process.env.APPDATA||path.join(userProfile,'AppData','Roaming'),
  LOCALAPPDATA:process.env.LOCALAPPDATA||path.join(userProfile,'AppData','Local'),
  HOMEDRIVE:process.env.HOMEDRIVE||'C:', HOMEPATH:process.env.HOMEPATH||'\\Users\\Administrator',
  HOME:process.env.HOME||userProfile, PROGRAMDATA:process.env.PROGRAMDATA||'C:\\ProgramData',
  ProgramFiles:process.env.ProgramFiles||'C:\\Program Files',
  'ProgramFiles(x86)':process.env['ProgramFiles(x86)']||'C:\\Program Files (x86)',
  CommonProgramFiles:process.env.CommonProgramFiles||'C:\\Program Files\\Common Files',
  CommonProgramW6432:process.env.CommonProgramW6432||'C:\\Program Files\\Common Files',
  SystemRoot:process.env.SystemRoot||'C:\\Windows', SYSTEMDRIVE:process.env.SYSTEMDRIVE||'C:',
});
fs.mkdirSync('C:\\ProgramData\\NuGet\\Config',{recursive:true});
const parent=path.join(root,'Build/UGUI-Tween');fs.mkdirSync(parent,{recursive:true});
const output=fs.mkdtempSync(path.join(parent,'tween-'));
const temporary=path.join(output,'temp');fs.mkdirSync(temporary,{recursive:true});
process.env.TEMP=temporary;process.env.TMP=temporary;
process.env.OV_SCRIPTING_SHADOW_ROOT=path.join(output,'managed-shadow');
const lock=JSON.parse(fs.readFileSync(path.join(root,'Tools/OvCSharp/toolchain.lock.json'),'utf8'));
const runtime=path.join(root,'Build/Toolchains/dotnet-'+lock.sdk_version);
const refs=path.join(runtime,'packs/Microsoft.NETCore.App.Ref',lock.runtime_version,'ref',lock.target_framework);
const dotnet=path.join(runtime,'dotnet.exe');
const checks=[];
function run(name,exe,args,env){
  const result=cp.spawnSync(exe,args,{cwd:root,env:env||process.env,windowsHide:true,encoding:'utf8',timeout:300000});
  fs.writeFileSync(path.join(output,name+'.log'),(result.stdout||'')+(result.stderr||''));
  checks.push({name,exit:result.status});
  if(result.status!==0)throw Error(name+' failed: '+path.join(output,name+'.log'));
  console.log('PASS '+name);
}
try {
  const managed=path.join(output,'managed');
  run('managed-host',process.execPath,[path.join(root,'Scripts/Windows/BuildCSharp.mjs'),'--compat',
    path.join(root,'Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj'),'--rebuild',
    '--artifacts-path',path.join(output,'artifacts'),'--output',managed]);
  const project=path.join(output,'project');fs.mkdirSync(project,{recursive:true});
  const fixture=path.join(project,'TweenFixture.dll'),rsp=path.join(output,'fixture.rsp');
  fs.writeFileSync(rsp,[
    '/nologo','/nostdlib+','/langversion:14.0','/nullable:enable','/warnaserror+','/platform:x64','/target:library',
    '/out:'+JSON.stringify(fixture),
    '/reference:'+JSON.stringify(path.join(managed,'Overload.ScriptCore.dll')),
    ...fs.readdirSync(refs).filter(x=>x.endsWith('.dll')).map(x=>'/reference:'+JSON.stringify(path.join(refs,x))),
    JSON.stringify(path.join(root,'Tests/UI/fixtures/TweenProbes.cs'))].join('\n'));
  run('fixture',dotnet,[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+rsp]);
  const generated=path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug');
  const args=JSON.parse(fs.readFileSync(path.join(generated,'OvCore/compile_commands.json'),'utf8'))[0].arguments;
  const system=args.filter(x=>x.startsWith('/clang:C:/')).map(x=>x.slice(7));
  const msvc=path.dirname(system.find(x=>x.includes('/MSVC/')&&x.endsWith('/include')));
  const ucrt=system.find(x=>x.endsWith('/ucrt')),version=path.basename(path.dirname(ucrt)),sdk=path.resolve(ucrt,'../../..');
  const nativeEnv={...process.env,LIB:[path.join(msvc,'lib/x64'),...['ucrt','um'].map(x=>path.join(sdk,'Lib',version,x,'x64'))].join(';')};
  const obj=path.join(output,'tween.obj'),native=path.join(output,'tween-runtime.exe');
  const includes=[...args.filter(x=>(x.startsWith('/I')||x.startsWith('/D'))&&!x.includes('OV_REFLECTION_ANALYSIS')),
    ...system.map(x=>'/I'+x),'/I'+root,'/I'+generated,...['glfw/include','bullet3/src'].map(x=>'/I'+path.join(root,'Dependencies',x))];
  run('runtime-compile',path.join(msvc,'bin/Hostx64/x64/cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...includes,
    path.join(root,'Tests/UI/tween_validation.cpp'),'/Fo'+obj],nativeEnv);
  const libraries=['OvReflection','OvCore','OvRendering','OvUI','OvTools','OvMaths','OvAudio','OvDebug','OvPhysics','OvWindowing','assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'];
  run('runtime-link',path.join(msvc,'bin/Hostx64/x64/link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+native,obj,
    ...libraries.map(x=>path.join(root,'Bin/Debug',x,x+'.lib')),
    ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x=>x+'.lib')],nativeEnv);
  run('runtime',native,[runtime,managed,fixture]);
  if(fs.readFileSync(path.join(output,'runtime.log'),'utf8').includes('[ERROR]'))
    throw Error('Tween logged an engine or managed error');
  fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({success:true,scope:'ITweenValue + ColorTween',checks},null,2));
  console.log('Evidence: '+output);
} catch(error){
  fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({success:false,checks,error:String(error)},null,2));
  console.error(String(error));process.exitCode=1;
}
