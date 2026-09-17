import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';
const project=path.dirname(fileURLToPath(import.meta.url)), root=path.resolve(project,'../..');
const python=process.argv[2] || process.env.OV_REFLECTION_PYTHON;
if(!python || !path.isAbsolute(python)) throw Error('Pass an absolute Python 3 executable path, or set OV_REFLECTION_PYTHON.');
const pythonCheck=cp.spawnSync(python,['-c','import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)'],
    {windowsHide:true,encoding:'utf8',timeout:10000});
if(pythonCheck.error || pythonCheck.status!==0) throw Error('A working Python 3 executable is required: '+python);
const cache=path.join(project,'.overload/csharp'); fs.mkdirSync(cache,{recursive:true});
const result=path.join(cache,'build.xml'), log=path.join(cache,'build.log'), fd=fs.openSync(log,'w');
const build=cp.spawnSync(process.execPath,[path.join(root,'Scripts/Windows/BuildGameScripts.mjs'),
    '--project',project,'--cache',cache,'--python',python,'--configuration','Debug','--result',result],
    {cwd:root,windowsHide:true,stdio:['ignore',fd,fd],timeout:300000});
fs.closeSync(fd);
if(build.status!==0) throw Error('Managed UI build failed: '+log+' '+(build.error || ''));
const receipt=fs.readFileSync(result,'utf8');
const escaped=receipt.match(/generation="([^"]+)"/)?.[1];
if(!escaped)throw Error('Build receipt has no generation');
const generation=escaped.replaceAll('&quot;','"').replaceAll('&amp;','&').replaceAll('&lt;','<').replaceAll('&gt;','>');
const lock=JSON.parse(fs.readFileSync(path.join(root,'Tools/OvCSharp/toolchain.lock.json'),'utf8'));
const settings=fs.readFileSync(path.join(project,'ManagedUI.ovproject'),'utf8')+
    '\ncsharp_engine_root='+root.replaceAll('\\','/')+
    '\ncsharp_node='+process.execPath.replaceAll('\\','/')+
    '\ncsharp_python='+python.replaceAll('\\','/')+
    '\ncsharp_runtime_root='+path.join(root,'Build/Toolchains/dotnet-'+lock.sdk_version).replaceAll('\\','/')+
    '\ncsharp_managed_root='+path.join(generation,'host').replaceAll('\\','/')+
    '\ncsharp_project_assembly='+path.join(generation,'GameScripts.dll').replaceAll('\\','/')+'\n';
const output=path.join(project,'ManagedUI.local.ovproject'); fs.writeFileSync(output,settings);
console.log('Open '+output+' in the rebuilt editor, then open Scenes/ManagedUI.ovscene and Play.');
