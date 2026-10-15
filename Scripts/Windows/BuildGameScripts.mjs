// Editor worker: one dotnet process per script edit, immutable complete generations,
// one writer per project/configuration.
//
// Engine-side pieces are not rebuilt on every edit: the reflection bindings come from the
// native build, and ManagedHost is cached per configuration under Build/Managed.
// Exit codes: 0 success, 1 failure, 3 sources changed during the build (superseded).
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { resolvePython, dotnetToolchain } from './CSharpBuildTools.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const self = fileURLToPath(import.meta.url);
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const hash = p => sha(fs.readFileSync(p));
const xml = s => String(s).replaceAll('&','&amp;').replaceAll('"','&quot;').replaceAll('<','&lt;').replaceAll('>','&gt;');
const excluded = new Set(['bin','obj','.overload','.git','build','generations','artifacts']);
const sourceLike = /\.(cs|csproj|props|targets|json|config)$/i;
const SUPERSEDED = 3;
function files(dir, outputRoot) {
  return fs.existsSync(dir) ? fs.readdirSync(dir,{withFileTypes:true}).flatMap(e => {
    if(e.isSymbolicLink()) return [];
    const p=path.join(dir,e.name);
    if(outputRoot && p.toLowerCase()===outputRoot.toLowerCase())return [];
    return e.isDirectory() ? (excluded.has(e.name.toLowerCase()) ? [] : files(p,outputRoot)) : [p];
  }).sort() : [];
}
// Metadata fingerprint (path, size, mtime): cheap enough to run before every build.
const stamp = list => sha(JSON.stringify(list.map(p => { try { const s=fs.statSync(p); return [p,s.size,s.mtimeMs]; } catch { return [p,null]; } })));
function atomic(file, data) { const temp=file+'.'+crypto.randomUUID()+'.tmp'; fs.writeFileSync(temp,data); fs.renameSync(temp,file); }
function copyTree(from, to) {
  fs.mkdirSync(to,{recursive:true});
  for(const e of fs.readdirSync(from,{withFileTypes:true})) {
    const source=path.join(from,e.name), target=path.join(to,e.name);
    if(e.isDirectory())copyTree(source,target); else fs.copyFileSync(source,target);
  }
}
async function lock(file) {
  const until=Date.now()+600000;
  for(;;) {
    try { const fd=fs.openSync(file,'wx'); fs.writeFileSync(fd,String(process.pid)); return () => {fs.closeSync(fd);fs.unlinkSync(file);}; }
    catch(e) {
      if(e.code!=='EEXIST'||Date.now()>until) throw Error('Build lock unavailable: '+file);
      // Do not race another waiter by deleting a stale lock. Keep crash evidence;
      // after confirming all editors/builds are closed, the user can retire it.
      let pid;try{pid=Number(fs.readFileSync(file,'utf8'));}catch{}
      if(pid>0)try{process.kill(pid,0);}catch(dead){if(dead.code==='ESRCH')throw Error('Previous build exited without releasing '+file+'; close other builds and move this stale lock aside.');}
    }
    await new Promise(r=>setTimeout(r,200));
  }
}
// MSBuild implicitly imports these from every ancestor directory.
function implicitImports(dir) {
  const found=[];
  for(let current=dir;;) {
    for(const name of ['Directory.Build.props','Directory.Build.targets','Directory.Packages.props'])
      if(fs.existsSync(path.join(current,name)))found.push(path.join(current,name));
    const parent=path.dirname(current); if(parent===current)return found; current=parent;
  }
}
// Projects whose only external input is ScriptCore can skip MSBuild when nothing changed.
function selfContained(projectFile) {
  const text=fs.readFileSync(projectFile,'utf8');
  if(/<(PackageReference|Reference|Import|Analyzer|PackageVersion)\b/i.test(text))return false;
  return [...text.matchAll(/<ProjectReference\b[^>]*Include\s*=\s*"([^"]+)"/gi)].every(m=>/Overload\.ScriptCore\.csproj$/i.test(m[1]));
}
function intact(generation, entries) {
  return entries.every(f=>{ const p=path.join(generation,f.path); try { return fs.statSync(p).size===f.size&&hash(p)===f.sha256; } catch { return false; } });
}
const hostRequired=['Overload.ManagedHost.dll','Overload.ManagedHost.deps.json','Overload.ManagedHost.runtimeconfig.json','Overload.ScriptCore.dll'];

// ManagedHost depends only on engine sources and the native reflection output, so it is shared
// by every project and rebuilt only when those change.
async function ensureHost(toolchain, configuration, python) {
  const generated=path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc',configuration);
  if(!fs.existsSync(path.join(generated,'generation-state.json'))) {
    // The native build normally produces this; bootstrap it once for a configuration never built.
    const interpreter=resolvePython(root,python);
    const r=spawnSync(interpreter,[path.join(root,'Tools/OvReflectGen/reflection.py'),'ensure','--config',configuration],{cwd:root,windowsHide:true,encoding:'utf8',timeout:600000,maxBuffer:64*1024*1024});
    process.stdout.write((r.stdout||'')+(r.stderr||''));
    if(r.error||r.status!==0)throw Error('Reflection generation failed for '+configuration+'; build the native '+configuration+' configuration first.');
  }
  const managed=path.join(root,'Sources/OvManaged');
  const inputs=[...files(managed).filter(p=>sourceLike.test(p)),path.join(root,'Sources/OvReflection/Managed/ScriptBridge.cs'),
    path.join(generated,'OvCore/Script.generated.cs'),path.join(generated,'generation-state.json'),
    path.join(root,'global.json'),path.join(root,'Tools/OvCSharp/toolchain.lock.json'),path.join(root,'Scripts/Windows/CSharpBuildTools.mjs'),self];
  const key=stamp(inputs).slice(0,20), hostRoot=path.join(root,'Build/Managed',configuration), host=path.join(hostRoot,key);
  if(fs.existsSync(path.join(host,'host.json')))return host;
  fs.mkdirSync(hostRoot,{recursive:true});
  const unlock=await lock(path.join(hostRoot,'writer.lock'));
  try {
    if(fs.existsSync(path.join(host,'host.json')))return host;
    console.log('Building ManagedHost ('+configuration+')');
    const project=path.join(managed,'Overload.ManagedHost/Overload.ManagedHost.csproj');
    const staging=path.join(hostRoot,'staging-'+crypto.randomUUID());
    const properties=[...toolchain.properties,'--artifacts-path',path.join(hostRoot,'artifacts'),'-p:OutputPath='+staging.split(path.sep).join('/')+'/'];
    if(!toolchain.run(['restore',project,'--locked-mode','--configfile',path.join(path.dirname(project),'NuGet.Config'),...properties]).ok)throw Error('ManagedHost restore failed');
    if(!toolchain.run(['build',project,'--no-restore','-v:minimal','-clp:NoSummary','-nr:false',...properties]).ok)throw Error('ManagedHost build failed');
    for(const p of hostRequired)if(!fs.existsSync(path.join(staging,p)))throw Error('Incomplete ManagedHost build: '+p);
    fs.writeFileSync(path.join(staging,'host.json'),JSON.stringify({configuration,key,sdk:toolchain.sdkVersion},null,2));
    fs.renameSync(staging,host);
    return host;
  } finally {unlock();}
}

async function main() {
  const options={}; const args=process.argv.slice(2);
  while(args.length) {const key=args.shift();if(!['--project','--cache','--python','--configuration','--result'].includes(key)||!args.length)throw Error('Invalid argument '+key);options[key.slice(2)]=args.shift();}
  const configuration=options.configuration;
  if(!['Debug','Release'].includes(configuration))throw Error('Expected Debug or Release');
  for(const key of ['project','cache','result'])if(!path.isAbsolute(options[key]||''))throw Error(key+' must be absolute');
  if(options.python&&!path.isAbsolute(options.python))throw Error('csharp_python/--python must be an absolute Python 3.10+ executable path');
  const project=fs.realpathSync(options.project), projectFile=path.join(project,'GameScripts.csproj');
  // Cache keys are based on canonical project identity, independent of display name.
  const key=sha(project.toLowerCase()).slice(0,24);
  const cache=path.join(options.cache,key,configuration); fs.mkdirSync(cache,{recursive:true});
  const projectCache=fs.realpathSync(path.dirname(cache));
  const toolchain=dotnetToolchain(root,configuration);
  const unlock=await lock(path.join(cache,'writer.lock'));
  try {
    const host=await ensureHost(toolchain,configuration,options.python);
    const scriptCore=[path.join(root,'Sources/OvManaged/Overload.ScriptCore'),path.join(root,'Sources/OvManaged/Overload.SerializationGenerator')]
      .flatMap(d=>files(d)).filter(p=>sourceLike.test(p));
    const engineInputs=[...scriptCore,path.join(root,'Sources/OvManaged/Serialization.targets'),path.join(root,'global.json'),
      path.join(root,'Tools/OvCSharp/toolchain.lock.json'),path.join(root,'Scripts/Windows/CSharpBuildTools.mjs'),self];
    const fingerprint=()=>stamp([...files(project,projectCache).filter(p=>sourceLike.test(p)),...implicitImports(project),...engineInputs])+':'+host;
    const before=fingerprint(), latest=path.join(cache,'latest.json');
    const previous=fs.existsSync(latest)?JSON.parse(fs.readFileSync(latest,'utf8')):null;
    const finish=(generation,unchanged)=>atomic(options.result,`<build configuration="${configuration}" generation="${xml(generation)}" unchanged="${unchanged}"/>`);

    // Fast path: same inputs as the last complete generation, nothing outside the project to check.
    if(previous && previous.inputs===before && selfContained(projectFile) && intact(previous.generation,previous.files)) {
      finish(previous.generation,true); console.log('Unchanged: '+previous.generation); return;
    }

    const properties=[...toolchain.properties,'--artifacts-path',path.join(cache,'artifacts','game')];
    // Restore only when its inputs changed; a missing assets file shows up as NETSDK1004 and restores then.
    const restoreState=path.join(cache,'restore.json');
    const restoreInputs=stamp([projectFile,path.join(project,'packages.lock.json'),path.join(project,'NuGet.Config'),path.join(project,'global.json'),
      ...implicitImports(project),...scriptCore.filter(p=>/\.(csproj|props|targets)$|packages\.lock\.json$/i.test(p))]);
    const restore=()=>{
      const config=path.join(project,'NuGet.Config');
      if(!toolchain.run(['restore',projectFile,'--locked-mode',...(fs.existsSync(config)?['--configfile',config]:[]),...properties]).ok)throw Error('Restore failed for '+projectFile);
      atomic(restoreState,JSON.stringify({inputs:restoreInputs}));
    };
    const restored=fs.existsSync(restoreState)&&JSON.parse(fs.readFileSync(restoreState,'utf8')).inputs===restoreInputs;
    if(!restored)restore();

    const id=crypto.randomUUID(); staging=path.join(cache,'staging-'+id); const generation=path.join(cache,'generations',id);
    const build=()=>toolchain.run(['build',projectFile,'--no-restore','-v:minimal','-clp:NoSummary','-nr:false',...properties,'-p:OutputPath='+staging.split(path.sep).join('/')+'/']);
    let built=build();
    if(!built.ok&&restored&&/NETSDK1004/.test(built.text)){restore();built=build();}
    if(!built.ok)throw Error('C# build failed');
    copyTree(host,path.join(staging,'host')); fs.rmSync(path.join(staging,'host','host.json'),{force:true});
    for(const p of ['GameScripts.dll','GameScripts.deps.json','Overload.ScriptCore.dll',...hostRequired.map(f=>'host/'+f)])
      if(!fs.existsSync(path.join(staging,p)))throw Error('Incomplete build: '+p);
    if(before!==fingerprint())throw Object.assign(Error('Sources changed during build; candidate discarded, build again'),{code:SUPERSEDED});

    // Include every dependency, satellite, PDB and runtime file in the generation manifest.
    const entries=files(staging).map(p=>({path:path.relative(staging,p).replaceAll('\\','/'),sha256:hash(p),size:fs.statSync(p).size}));
    if(previous && JSON.stringify(previous.files)===JSON.stringify(entries) && intact(previous.generation,previous.files)) {
      fs.rmSync(staging,{recursive:true,force:true});
      atomic(latest,JSON.stringify({...previous,inputs:before},null,2));
      finish(previous.generation,true);console.log('Unchanged: '+previous.generation);return;
    }
    const manifest={schema:1,project,configuration,sdk:toolchain.sdkVersion,runtime:toolchain.runtimeVersion,inputs:before,generation,files:entries};
    fs.writeFileSync(path.join(staging,'manifest.json'),JSON.stringify(manifest,null,2));
    fs.writeFileSync(path.join(staging,'manifest.xml'),`<generation configuration="${configuration}">${entries.map(f=>`<file path="${xml(f.path)}" size="${f.size}" sha256="${f.sha256}"/>`).join('')}</generation>`);
    fs.mkdirSync(path.dirname(generation),{recursive:true});fs.renameSync(staging,generation);
    atomic(latest,JSON.stringify(manifest,null,2));finish(generation,false);console.log('Complete generation: '+generation);
  } finally {unlock();}
}
let staging;
try { await main(); }
catch(e) {
  if(staging)fs.rmSync(staging,{recursive:true,force:true});
  console.error(String(e));process.exitCode=e.code===SUPERSEDED?SUPERSEDED:1;
}
