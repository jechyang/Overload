import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const args=process.argv.slice(2),get=n=>{const i=args.indexOf('--'+n);if(i<0||!args[i+1])throw Error('Missing --'+n);return path.resolve(args[i+1]);};
const config=args[args.indexOf('--configuration')+1];
if(!['Debug','Release','Publish'].includes(config))throw Error('Invalid configuration');
const assets=get('assets'),settings=get('settings');
const destination=get('destination'),runtime=get('runtime'),host=get('host'),game=get('game'),crt=get('crt'),probe=get('probe'),redist=get('redist-notice');
if(fs.existsSync(destination))throw Error('Destination must be new; existing packages are never overwritten');
fs.mkdirSync(destination,{recursive:true});
const copy=(src,rel)=>{const target=path.join(destination,rel);fs.mkdirSync(path.dirname(target),{recursive:true});fs.cpSync(src,target,{recursive:true,dereference:true});};
const digest=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const walk=p=>fs.readdirSync(p,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(path.join(p,e.name)):[path.join(p,e.name)]);
copy(path.join(root,'Bin',config,'OvGame/OvGame.exe'),'OvGame.exe');
for(const file of fs.readdirSync(crt))if(file.endsWith('.dll'))copy(path.join(crt,file),file);
copy(probe,'SdkProbe.exe');copy(path.join(root,'Resources/Engine'),'Data/Engine');
copy(host,'Managed');copy(game,'Data/User/Managed');
copy(assets,'Data/User/Assets');copy(settings,'Data/User/Game.ini');
for(const rel of ['shared/Microsoft.NETCore.App/10.0.12','host/fxr/10.0.12','packs/Microsoft.NETCore.App.Host.win-x64/10.0.12/runtimes/win-x64/native/nethost.dll'])copy(path.join(runtime,rel),'Runtime/dotnet/'+rel);
for(const name of ['LICENSE.txt','ThirdPartyNotices.txt'])copy(path.join(runtime,name),'Licenses/dotnet/'+name);
copy(path.join(root,'LICENSE'),'Licenses/Overload-LICENSE');copy(path.join(root,'Tools/OvCSharp/Licenses'),'Licenses/upstream');copy(redist,'Licenses/MSVC-Redist.txt');
const vcNotices=path.join(path.dirname(redist),'ThirdPartyNotices.txt');
if(!fs.existsSync(vcNotices))throw Error('MSVC third-party notices are missing');
copy(vcNotices,'Licenses/MSVC-ThirdPartyNotices.txt');
// Preserve every dependency's embedded copyright/license comment, including amalgamated codecs.
// Complete standalone notice files are copied verbatim; source comments are kept with their source paths.
const legal=[];for(const file of walk(path.join(root,'Dependencies'))){
 if(!/\.(h|hpp|c|cpp|cc|cxx|txt|md|inl)$/i.test(file)&&!/(LICENSE|COPYING|COPYRIGHT|NOTICE)$/i.test(file))continue;
 const relative=path.relative(root,file).replaceAll('\\','/');
 if(/(^|\/)(license[^/]*|copying[^/]*|copyright[^/]*|notice[^/]*)$/i.test(relative)){copy(file,'Licenses/source/'+relative);legal.push({source:relative,sha256:digest(file)});continue;}
 const text=fs.readFileSync(file,'utf8');const comments=[...text.matchAll(/\/\*[\s\S]*?\*\/|(?:^[ \t]*\/\/[^\n]*\n)+/gm)].map(m=>m[0]).filter(t=>/copyright|permission is hereby granted|redistribution and use|public domain|SPDX-License-Identifier/i.test(t));
 if(comments.length){const target='Licenses/embedded/'+crypto.createHash('sha256').update(relative).digest('hex')+'.txt';fs.mkdirSync(path.dirname(path.join(destination,target)),{recursive:true});fs.writeFileSync(path.join(destination,target),comments.join('\n\n'));legal.push({source:relative,source_sha256:digest(file),notice:target});}
}
const deps={engine:'Overload MIT',managed:['Overload.ManagedHost (includes NativeApi)','Overload.ScriptCore','GameScripts'],static_native:['assimp','baregl','bullet3','freetype','glfw','ImGui','lua','sol','soloud','tinyxml2','tracy'],assets:['Engine assets under project license','Roboto Regular: SIL OFL 1.1'],runtime:'.NET 10.0.12 win-x64, hostfxr/coreclr/nethost and full runtime directory',crt:{kind:config==='Debug'?'DebugCRT: development validation only, NOT redistributable':'Microsoft.VC143.CRT app-local redistributable',source:crt,license:'Microsoft Visual Studio license; redistribution subject to its terms',redistributable_list:'https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution'},os_provided:['Windows API DLLs and API sets','system OpenGL loader and installed GPU driver','Windows audio services'],embedded_notices:legal};
fs.writeFileSync(path.join(destination,'Licenses/dependencies.json'),JSON.stringify(deps,null,2));
for(const [folder,files] of [['Managed',['Overload.ManagedHost.dll','Overload.ManagedHost.deps.json','Overload.ManagedHost.runtimeconfig.json','Overload.ScriptCore.dll']],['Data/User/Managed',['GameScripts.dll','GameScripts.deps.json','Overload.ScriptCore.dll']]])for(const file of files)if(!fs.existsSync(path.join(destination,folder,file)))throw Error('Missing artifact '+file);
const files=walk(destination).map(p=>({path:path.relative(destination,p).replaceAll('\\','/'),bytes:fs.statSync(p).size,sha256:digest(p)}));
fs.writeFileSync(path.join(destination,'s7-package.json'),JSON.stringify({kind:'Overload-private-runtime-package',configuration:config,distribution_eligible:config!=='Debug',runtime:'10.0.12',framework:'net10.0',sdk_included:false,compiler_included:false,files},null,2));
console.log(JSON.stringify({destination,configuration:config,files:files.length,bytes:files.reduce((s,f)=>s+f.bytes,0)}));
