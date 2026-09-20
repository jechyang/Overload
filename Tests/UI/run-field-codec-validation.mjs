import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const parent=path.join(root,'Build/UIValidation');fs.mkdirSync(parent,{recursive:true});
const output=fs.mkdtempSync(path.join(parent,'field-codec-'));
// Keep compiler temporaries and collectible CLR copies beside this run on the workspace drive.
const temporary=path.join(output,'temp'); fs.mkdirSync(temporary,{recursive:true});
process.env.TEMP=temporary; process.env.TMP=temporary;
process.env.OV_SCRIPTING_SHADOW_ROOT=path.join(output,'managed-shadow');
const lock=JSON.parse(fs.readFileSync(path.join(root,'Tools/OvCSharp/toolchain.lock.json'),'utf8'));
const runtime=path.join(root,'Build/Toolchains/dotnet-'+lock.sdk_version);
const refs=path.join(runtime,'packs/Microsoft.NETCore.App.Ref',lock.runtime_version,'ref',lock.target_framework);
const dotnet=path.join(runtime,'dotnet.exe'),checks=[],sources={};
const env={...process.env};
const flags=['/nologo','/nostdlib+','/langversion:14.0','/nullable:enable','/warnaserror+','/unsafe+','/deterministic+','/platform:x64',
 ...fs.readdirSync(refs).filter(x=>x.endsWith('.dll')).map(x=>'/reference:'+JSON.stringify(path.join(refs,x)))];
function run(name,args) {
 const result=cp.spawnSync(dotnet,args,{cwd:root,env,windowsHide:true,encoding:'utf8',timeout:300000});
 fs.writeFileSync(path.join(output,name+'.log'),(result.stdout||'')+(result.stderr||''));
 checks.push({name,exit:result.status,error:String(result.error||'')});
 if(result.status!==0)throw Error(name+' failed; see '+output);
 console.log('PASS '+name); if(name==='contracts')console.log(result.stdout.trim());
}
function compile(name,directory,extra,additionalFlags=[]) {
 const files=fs.readdirSync(path.join(root,directory)).filter(x=>x.endsWith('.cs')).map(x=>path.join(root,directory,x)).concat(extra);
 for(const file of files)sources[path.relative(root,file)]=crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const rsp=path.join(output,name+'.rsp');
 fs.writeFileSync(rsp,[...flags,...additionalFlags,'/out:'+JSON.stringify(path.join(output,name+'.dll')),...files.map(x=>JSON.stringify(x))].join('\n'));
 run(name,[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+rsp]);
}
try {
 compile('Overload.ScriptCore','Sources/OvManaged/Overload.ScriptCore',[],['/target:library']);
 compile('Overload.ManagedHost','Sources/OvManaged/Overload.ManagedHost',[
  'Sources/OvReflection/Managed/ScriptBridge.cs','Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug/OvCore/Script.generated.cs',
  'Tests/UI/FieldCodecContracts.cs'].map(x=>path.join(root,x)),['/target:exe','/reference:'+JSON.stringify(path.join(output,'Overload.ScriptCore.dll'))]);
 fs.writeFileSync(path.join(output,'Overload.ManagedHost.runtimeconfig.json'),JSON.stringify({runtimeOptions:{tfm:lock.target_framework,framework:{name:'Microsoft.NETCore.App',version:lock.runtime_version}}}));
 run('contracts',[path.join(output,'Overload.ManagedHost.dll')]);
 fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({success:true,checks,sources,scope:'production field codec; does not exercise native Inspector or project reload'},null,2));
 console.log('Evidence: '+output);
} catch(error) {
 fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({success:false,checks,sources,error:String(error)},null,2));
 console.error(String(error));process.exitCode=1;
}
