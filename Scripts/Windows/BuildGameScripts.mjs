// Editor worker: no shell, immutable complete generations, one writer per project/configuration.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const hash = p => crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const xml = s => String(s).replaceAll('&','&amp;').replaceAll('"','&quot;').replaceAll('<','&lt;').replaceAll('>','&gt;');
const excluded = new Set(['bin','obj','.overload','.git','build','generations','artifacts']);
function files(dir, outputRoot) {
  return fs.existsSync(dir) ? fs.readdirSync(dir,{withFileTypes:true}).flatMap(e => {
    if(e.isSymbolicLink()) return [];
    const p=path.join(dir,e.name);
    if(outputRoot && p.toLowerCase()===outputRoot.toLowerCase())return [];
    return e.isDirectory() ? (excluded.has(e.name.toLowerCase()) ? [] : files(p,outputRoot)) : [p];
  }).sort() : [];
}
function atomic(file, data) { const temp=file+'.'+crypto.randomUUID()+'.tmp'; fs.writeFileSync(temp,data); fs.renameSync(temp,file); }
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
let result;
try {
  const options={}; const args=process.argv.slice(2);
  while(args.length) {const key=args.shift();if(!['--project','--cache','--python','--configuration','--result'].includes(key)||!args.length)throw Error('Invalid argument '+key);options[key.slice(2)]=args.shift();}
  const configuration=options.configuration;
  if(!['Debug','Release'].includes(configuration))throw Error('Expected Debug or Release');
  for(const key of ['project','cache','python','result'])if(!path.isAbsolute(options[key]||''))throw Error(key+' must be absolute');
  result=options.result;
  const project=fs.realpathSync(options.project), projectFile=path.join(project,'GameScripts.csproj');
  // Cache keys are based on canonical project identity, independent of display name.
  const key=crypto.createHash('sha256').update(project.toLowerCase()).digest('hex').slice(0,24);
  const cache=path.join(options.cache,key,configuration); fs.mkdirSync(cache,{recursive:true});
  const projectCache=fs.realpathSync(path.dirname(cache));
  const unlock=await lock(path.join(cache,'writer.lock'));
  try {
    const run=(exe,argv)=>{
      if(!path.isAbsolute(exe))throw Error('Executable must be absolute');
      const r=spawnSync(exe,argv,{cwd:root,windowsHide:true,encoding:'utf8',timeout:600000,maxBuffer:64*1024*1024});
      process.stdout.write((r.stdout||'')+(r.stderr||''));
      if(r.error||r.status!==0)throw Error('Build step failed ('+r.status+'): '+exe+' '+argv.join(' '));
    };
    // The R6 generator owns its own cross-process lock; shared generated source is immutable on no-change builds.
    run(options.python,[path.join(root,'Tools/OvReflectGen/reflection.py'),'ensure','--config',configuration]);
    const inputFiles=()=>[...files(project,projectCache).filter(p=>/\.(cs|csproj|props|targets|json|config)$/i.test(p)),
      ...files(path.join(root,'Sources/OvManaged')).filter(p=>/\.(cs|csproj|json|config)$/i.test(p)),
      path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc',configuration,'OvCore/Script.generated.cs'),
      path.join(root,'global.json'),path.join(root,'Tools/OvCSharp/toolchain.lock.json'),
      path.join(root,'Scripts/Windows/BuildCSharp.mjs'),fileURLToPath(import.meta.url)];
    const fingerprint=()=>crypto.createHash('sha256').update(JSON.stringify(inputFiles().map(p=>[p,hash(p)]))).digest('hex');
    const before=fingerprint(), latest=path.join(cache,'latest.json');
    const finish=(generation,unchanged)=>atomic(result,`<build configuration="${configuration}" generation="${xml(generation)}" unchanged="${unchanged}"/>`);
    // Always let MSBuild check its complete dependency graph (including project
    // references outside Assets). Content comparison below avoids needless reload.
    const previous=fs.existsSync(latest)?JSON.parse(fs.readFileSync(latest,'utf8')):null;
    await build();
    async function build() {
      const id=crypto.randomUUID(), staging=path.join(cache,'staging-'+id), generation=path.join(cache,'generations',id);
      fs.mkdirSync(staging,{recursive:true});
      const compat=(csproj,out,group)=>run(process.execPath,[path.join(root,'Scripts/Windows/BuildCSharp.mjs'),'--compat',csproj,
        '--configuration',configuration,'--artifacts-path',path.join(cache,'artifacts',group),'--output',out]);
      compat(path.join(root,'Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj'),path.join(staging,'host'),'host');
      compat(projectFile,staging,'game');
      for(const p of ['GameScripts.dll','GameScripts.deps.json','Overload.ScriptCore.dll','host/Overload.ScriptCore.dll','host/Overload.ManagedHost.dll','host/Overload.ManagedHost.deps.json','host/Overload.ManagedHost.runtimeconfig.json'])
        if(!fs.existsSync(path.join(staging,p)))throw Error('Incomplete build: '+p);
      if(before!==fingerprint())throw Error('Sources changed during build; candidate discarded, build again');
      // Include every dependency, satellite, PDB and runtime file in the generation manifest.
      const entries=files(staging).map(p=>({path:path.relative(staging,p).replaceAll('\\','/'),sha256:hash(p),size:fs.statSync(p).size}));
      if(previous && JSON.stringify(previous.files)===JSON.stringify(entries) &&
        previous.files.every(f=>fs.existsSync(path.join(previous.generation,f.path))&&hash(path.join(previous.generation,f.path))===f.sha256)) {
        finish(previous.generation,true);console.log('Unchanged: '+previous.generation);return;
      }
      const manifest={schema:1,project,configuration,sdk:'10.0.401',runtime:'10.0.12',fingerprint:before,generation,files:entries};
      fs.writeFileSync(path.join(staging,'manifest.json'),JSON.stringify(manifest,null,2));
      fs.writeFileSync(path.join(staging,'manifest.xml'),`<generation configuration="${configuration}">${entries.map(f=>`<file path="${xml(f.path)}" size="${f.size}" sha256="${f.sha256}"/>`).join('')}</generation>`);
      fs.mkdirSync(path.dirname(generation),{recursive:true});fs.renameSync(staging,generation);
      atomic(latest,JSON.stringify(manifest,null,2));finish(generation,false);console.log('Complete generation: '+generation);
    }
  } finally {unlock();}
} catch(e) {console.error(String(e));process.exitCode=1;}
