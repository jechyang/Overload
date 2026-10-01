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
const parent=path.join(root,'Build/FlappyBird'); fs.mkdirSync(parent,{recursive:true});
const output=fs.mkdtempSync(path.join(parent,'smoke-'));
process.env.TEMP=path.join(output,'temp'); process.env.TMP=process.env.TEMP;
fs.mkdirSync(process.env.TEMP,{recursive:true});
const lock=JSON.parse(fs.readFileSync(path.join(root,'Tools/OvCSharp/toolchain.lock.json'),'utf8'));
const runtime=path.join(root,'Build/Toolchains/dotnet-'+lock.sdk_version);
const managed=path.join(root,'Examples/FlappyBird/.overload/csharp');
const generation=fs.readdirSync(managed).filter(n=>n!=='build.log'&&n!=='build.xml'&&n!=='temp'&&n!=='managed-shadow');
void generation;
const checks=[];
function run(name,exe,args,env){
  const r=cp.spawnSync(exe,args,{cwd:root,env:env||process.env,windowsHide:true,encoding:'utf8',timeout:300000});
  fs.writeFileSync(path.join(output,name+'.log'),(r.stdout||'')+(r.stderr||''));
  checks.push({name,exit:r.status});
  if(r.status!==0) throw Error(name+' failed: '+path.join(output,name+'.log'));
  console.log('PASS '+name);
}
try {
  // Build host + GameScripts via the project script.
  run('gamescripts',process.execPath,[path.join(root,'Examples/FlappyBird/build.mjs'),
    'C:/Users/Administrator/AppData/Local/Programs/Python/Python312/python.exe']);
  const local=path.join(root,'Examples/FlappyBird/FlappyBird.local.ovproject');
  const settings=fs.readFileSync(local,'utf8');
  const hostDir=settings.match(/csharp_managed_root=(.*)/)?.[1]?.trim();
  const gameDll=settings.match(/csharp_project_assembly=(.*)/)?.[1]?.trim();
  if(!hostDir||!gameDll) throw Error('FlappyBird.local.ovproject missing managed paths');
  const hostBin=path.join(root,'Bin/Debug');
  const generated=path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug');
  const cc=JSON.parse(fs.readFileSync(path.join(generated,'OvCore/compile_commands.json'),'utf8'))[0].arguments;
  const system=cc.filter(x=>x.startsWith('/clang:C:/')).map(x=>x.slice(7));
  const msvc=path.dirname(system.find(x=>x.includes('/MSVC/')&&x.endsWith('/include')));
  const ucrt=system.find(x=>x.endsWith('/ucrt')),version=path.basename(path.dirname(ucrt)),sdk=path.resolve(ucrt,'../../..');
  const nativeEnv={...process.env,LIB:[path.join(msvc,'lib/x64'),...['ucrt','um'].map(x=>path.join(sdk,'Lib',version,x,'x64'))].join(';')};
  const includes=[...cc.filter(x=>(x.startsWith('/I')||x.startsWith('/D'))&&!x.includes('OV_REFLECTION_ANALYSIS')),
    ...system.map(x=>'/I'+x),'/I'+root,'/I'+generated,...['glfw/include','bullet3/src'].map(x=>'/I'+path.join(root,'Dependencies',x))];
  const obj=path.join(output,'flappy.obj'), exe=path.join(output,'flappy-smoke.exe');
  run('compile',path.join(msvc,'bin/Hostx64/x64/cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...includes,
    path.join(root,'Tests/UI/flappy_smoke_validation.cpp'),'/Fo'+obj],nativeEnv);
  const libraries=['OvReflection','OvCore','OvRendering','OvUI','OvTools','OvMaths','OvAudio','OvDebug','OvPhysics','OvWindowing','assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'];
  run('link',path.join(msvc,'bin/Hostx64/x64/link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+exe,obj,
    ...libraries.map(x=>path.join(root,'Bin/Debug',x,x+'.lib')),
    ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x=>x+'.lib')],nativeEnv);
  run('smoke',exe,[runtime,hostDir,gameDll],nativeEnv);
  fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({success:true,scope:'FlappyBird uGUI smoke',checks},null,2));
  console.log('Evidence: '+output);
} catch(error) {
  fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({success:false,checks,error:String(error)},null,2));
  console.error(String(error)); process.exitCode=1;
}
