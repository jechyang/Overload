import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';
const project=path.dirname(fileURLToPath(import.meta.url)), root=path.resolve(project,'../..');
// Desktop CLR/NuGet restore needs a complete Windows profile environment.
const userProfile=process.env.USERPROFILE||'C:\\Users\\Administrator';
Object.assign(process.env,{
  USERPROFILE:userProfile,
  USERNAME:process.env.USERNAME==='SYSTEM'?'Administrator':(process.env.USERNAME||'Administrator'),
  APPDATA:process.env.APPDATA||path.join(userProfile,'AppData','Roaming'),
  LOCALAPPDATA:process.env.LOCALAPPDATA||path.join(userProfile,'AppData','Local'),
  HOMEDRIVE:process.env.HOMEDRIVE||'C:',
  HOMEPATH:process.env.HOMEPATH||'\\Users\\Administrator',
  HOME:process.env.HOME||userProfile,
  PROGRAMDATA:process.env.PROGRAMDATA||'C:\\ProgramData',
  ProgramFiles:process.env.ProgramFiles||'C:\\Program Files',
  'ProgramFiles(x86)':process.env['ProgramFiles(x86)']||'C:\\Program Files (x86)',
  CommonProgramFiles:process.env.CommonProgramFiles||'C:\\Program Files\\Common Files',
  CommonProgramW6432:process.env.CommonProgramW6432||'C:\\Program Files\\Common Files',
  SystemRoot:process.env.SystemRoot||'C:\\Windows',
  SYSTEMDRIVE:process.env.SYSTEMDRIVE||'C:',
});
fs.mkdirSync('C:\\ProgramData\\NuGet\\Config',{recursive:true});
const tempDir=path.join(project,'.overload/temp');
fs.mkdirSync(tempDir,{recursive:true});
fs.mkdirSync(path.join(project,'.overload/managed-shadow'),{recursive:true});
process.env.TEMP=tempDir; process.env.TMP=tempDir;
process.env.OV_SCRIPTING_SHADOW_ROOT=path.join(project,'.overload/managed-shadow');
const python=process.argv[2] || process.env.OV_REFLECTION_PYTHON;
if(!python || !path.isAbsolute(python)) throw Error('Pass an absolute Python 3 executable path, or set OV_REFLECTION_PYTHON.');
const pythonCheck=cp.spawnSync(python,['-c','import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)'],
    {windowsHide:true,encoding:'utf8',timeout:10000});
if(pythonCheck.error || pythonCheck.status!==0) throw Error('A working Python 3 executable is required: '+python);
const cache=path.join(project,'.overload/csharp'); fs.mkdirSync(cache,{recursive:true});
const result=path.join(cache,'build.xml'), log=path.join(cache,'build.log'), fd=fs.openSync(log,'w');
const build=cp.spawnSync(process.execPath,[path.join(root,'Scripts/Windows/BuildGameScripts.mjs'),
    '--project',project,'--cache',cache,'--python',python,'--configuration','Debug','--result',result],
    {cwd:root,windowsHide:true,stdio:['ignore',fd,fd],timeout:300000,env:process.env});
fs.closeSync(fd);
if(build.status!==0) throw Error('Managed UI build failed: '+log+' '+(build.error || ''));
const receipt=fs.readFileSync(result,'utf8');
const escaped=receipt.match(/generation="([^"]+)"/)?.[1];
if(!escaped)throw Error('Build receipt has no generation');
const generation=escaped.replaceAll('&quot;','"').replaceAll('&amp;','&').replaceAll('&lt;','<').replaceAll('&gt;','>');
const lock=JSON.parse(fs.readFileSync(path.join(root,'Tools/OvCSharp/toolchain.lock.json'),'utf8'));
const settings=fs.readFileSync(path.join(project,'FlappyBird.ovproject'),'utf8')+
    '\ncsharp_engine_root='+root.replaceAll('\\','/')+
    '\ncsharp_node='+process.execPath.replaceAll('\\','/')+
    '\ncsharp_python='+python.replaceAll('\\','/')+
    '\ncsharp_runtime_root='+path.join(root,'Build/Toolchains/dotnet-'+lock.sdk_version).replaceAll('\\','/')+
    '\ncsharp_managed_root='+path.join(generation,'host').replaceAll('\\','/')+
    '\ncsharp_project_assembly='+path.join(generation,'GameScripts.dll').replaceAll('\\','/')+'\n';
const output=path.join(project,'FlappyBird.local.ovproject'); fs.writeFileSync(output,settings);
console.log('Open '+output+' in the editor, then open Assets/Scenes/FlappyBird.ovscene and Play.');
