import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
import {fileURLToPath} from 'node:url';
import {verifyFlappyApps} from './verify-flappy-apps.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
// This shell may run as SYSTEM without a complete user profile; NuGet/MSBuild
// path resolution then throws on null special folders. Fill the usual Windows
// values before any restore/build child process.
const userProfile=process.env.USERPROFILE||'C:\\Users\\Administrator';
Object.assign(process.env,{
  USERPROFILE:userProfile,
  USERNAME:process.env.USERNAME==='SYSTEM'?'Administrator':(process.env.USERNAME||'Administrator'),
  APPDATA:process.env.APPDATA||path.join(userProfile,'AppData','Roaming'),
  LOCALAPPDATA:process.env.LOCALAPPDATA||path.join(userProfile,'AppData','Local'),
  HOMEDRIVE:process.env.HOMEDRIVE||'C:',
  HOMEPATH:process.env.HOMEPATH||'\\Users\\Administrator',
  PROGRAMDATA:process.env.PROGRAMDATA||'C:\\ProgramData',
  ProgramFiles:process.env.ProgramFiles||'C:\\Program Files',
  'ProgramFiles(x86)':process.env['ProgramFiles(x86)']||'C:\\Program Files (x86)',
  CommonProgramFiles:process.env.CommonProgramFiles||'C:\\Program Files\\Common Files',
  CommonProgramW6432:process.env.CommonProgramW6432||'C:\\Program Files\\Common Files',
  SystemRoot:process.env.SystemRoot||'C:\\Windows',
  SYSTEMDRIVE:process.env.SYSTEMDRIVE||'C:',
});

fs.mkdirSync(path.join(root,'Build/CSharpCompat/cli'),{recursive:true});
fs.mkdirSync(path.join(root,'Build/CSharpCompat/packages'),{recursive:true});
const parent=path.join(root,'Build/FlappyValidation');fs.mkdirSync(parent,{recursive:true});
const output=fs.mkdtempSync(path.join(parent,'flappy-'));
const temporary=path.join(output,'temp');fs.mkdirSync(temporary,{recursive:true});
process.env.TEMP=temporary;process.env.TMP=temporary;
process.env.OV_SCRIPTING_SHADOW_ROOT=path.join(output,'managed-shadow');
const lock=JSON.parse(fs.readFileSync(path.join(root,'Tools/OvCSharp/toolchain.lock.json'),'utf8'));
const runtime=path.join(root,'Build/Toolchains/dotnet-'+lock.sdk_version);
const refs=path.join(runtime,'packs/Microsoft.NETCore.App.Ref',lock.runtime_version,'ref',lock.target_framework);
const dotnet=path.join(runtime,'dotnet.exe');
const checks=[];
const revision=cp.spawnSync('git',['-c','safe.directory='+root.replaceAll('\\','/'),'rev-parse','HEAD'],{cwd:root,windowsHide:true,encoding:'utf8'});
const evidence={command:process.argv,commit:revision.status===0?revision.stdout.trim():null,viewport:{width:400,height:600},sources:{}};

let nativeEnv;
function run(name,exe,args,cwd=root){
  const fd=fs.openSync(path.join(output,name+'.log'),'w');
  const result=cp.spawnSync(exe,args,{cwd,env:nativeEnv,windowsHide:true,stdio:['ignore',fd,fd],timeout:300000});
  fs.closeSync(fd);
  checks.push({name,exit:result.status,error:String(result.error||'')});
  if(result.status!==0)throw Error(name+' failed: '+path.join(output,name+'.log'));
  console.log('PASS '+name);
}
function writePng(rgbaPath,pngPath,width,height){
  // Minimal RGBA8 PNG writer (filter 0 + zlib). Keeps the runner free of native deps.
  const raw=fs.readFileSync(rgbaPath);
  if(raw.length!==width*height*4)throw Error('RGBA size mismatch for '+rgbaPath);
  const stride=width*4;
  const rows=Buffer.alloc((stride+1)*height);
  for(let y=0;y<height;y++){
    rows[y*(stride+1)]=0;
    // Flip Y so the preview matches top-left image convention.
    raw.copy(rows,y*(stride+1)+1,(height-1-y)*stride,(height-y)*stride);
  }
  const idat=zlib.deflateSync(rows);
  const crcTable=[];for(let n=0;n<256;n++){let c=n;for(let k=0;k<8;k++)c=c&1?0xedb88320^(c>>>1):c>>>1;crcTable[n]=c>>>0;}
  const crc=(buf)=>{let c=0xffffffff;for(const b of buf)c=crcTable[(c^b)&0xff]^(c>>>8);return (c^0xffffffff)>>>0;};
  const chunk=(type,data)=>{
    const len=Buffer.alloc(4);len.writeUInt32BE(data.length);
    const body=Buffer.concat([Buffer.from(type),data]);
    const sum=Buffer.alloc(4);sum.writeUInt32BE(crc(body));
    return Buffer.concat([len,body,sum]);
  };
  const ihdr=Buffer.alloc(13);
  ihdr.writeUInt32BE(width,0);ihdr.writeUInt32BE(height,4);
  ihdr[8]=8;ihdr[9]=6;ihdr[10]=0;ihdr[11]=0;ihdr[12]=0;
  const png=Buffer.concat([
    Buffer.from([0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a]),
    chunk('IHDR',ihdr),chunk('IDAT',idat),chunk('IEND',Buffer.alloc(0))
  ]);
  fs.writeFileSync(pngPath,png);
}
try {
  for (const arg of process.argv.slice(2)) if(arg !== '--apps') throw Error('Unknown option: '+arg);
  for (const file of ['Tests/UI/flappy_app_validation.cpp','Tests/UI/verify-flappy-apps.mjs','Tests/UI/run-flappy-validation.mjs','Tests/UI/flappy_smoke_validation.cpp','Examples/FlappyBird/Assets/Scenes/FlappyBird.ovscene',
      'Examples/FlappyBird/FlappyBird.ovproject','Sources/OvManaged/Overload.ManagedHost/ProjectTypes.cs',
      'Sources/OvEditor/src/OvEditor/Core/Context.cpp','Sources/OvEditor/src/OvEditor/Utils/ProjectManagement.cpp'])
    evidence.sources[file]=crypto.createHash('sha256').update(fs.readFileSync(path.join(root,file))).digest('hex');
  const settings=fs.readFileSync(path.join(root,'Examples/FlappyBird/FlappyBird.ovproject'),'utf8');
  const startScene=settings.match(/^start_scene=(.+)$/m)?.[1].trim();
  if(!startScene || !fs.existsSync(path.join(root,'Examples/FlappyBird/Assets',startScene)))
    throw Error('FlappyBird start_scene does not resolve from Assets');
  const sampleSources=fs.readdirSync(path.join(root,'Examples/FlappyBird/Assets/Scripts')).filter(x=>x.endsWith('.cs'))
    .map(x=>path.join(root,'Examples/FlappyBird/Assets/Scripts',x));
  for(const file of sampleSources)evidence.sources[path.relative(root,file)]=crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

  const managed=path.join(output,'managed');
  run('managed-host',process.execPath,[path.join(root,'Scripts/Windows/BuildCSharp.mjs'),'--compat',
    path.join(root,'Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj'),'--rebuild',
    '--artifacts-path',path.join(output,'artifacts'),'--output',managed]);

  const project=path.join(output,'project');fs.mkdirSync(project,{recursive:true});
  const fixture=path.join(project,'GameScripts.dll'),rsp=path.join(output,'GameScripts.rsp');
  fs.writeFileSync(rsp,[
    '/nologo','/nostdlib+','/langversion:14.0','/nullable:enable','/warnaserror+','/platform:x64','/target:library',
    '/out:'+JSON.stringify(fixture),
    '/reference:'+JSON.stringify(path.join(managed,'Overload.ScriptCore.dll')),
    ...fs.readdirSync(refs).filter(x=>x.endsWith('.dll')).map(x=>'/reference:'+JSON.stringify(path.join(refs,x))),
    ...sampleSources.map(x=>JSON.stringify(x))].join('\n'));
  run('GameScripts',dotnet,[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+rsp]);

  const generated=path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug');
  const args=JSON.parse(fs.readFileSync(path.join(generated,'OvCore/compile_commands.json'),'utf8'))[0].arguments;
  const system=args.filter(x=>x.startsWith('/clang:C:/')).map(x=>x.slice(7));
  const msvc=path.dirname(system.find(x=>x.includes('/MSVC/')&&x.endsWith('/include')));
  const ucrt=system.find(x=>x.endsWith('/ucrt')),version=path.basename(path.dirname(ucrt)),sdk=path.resolve(ucrt,'../../..');
  nativeEnv={...process.env,LIB:[path.join(msvc,'lib/x64'),...['ucrt','um'].map(x=>path.join(sdk,'Lib',version,x,'x64'))].join(';')};
  const obj=path.join(output,'flappy.obj'),native=path.join(output,'flappy-runtime.exe');
  const includes=[...args.filter(x=>(x.startsWith('/I')||x.startsWith('/D'))&&!x.includes('OV_REFLECTION_ANALYSIS')),
    ...system.map(x=>'/I'+x),'/I'+root,'/I'+generated,...['glfw/include','bullet3/src'].map(x=>'/I'+path.join(root,'Dependencies',x))];
  run('runtime-compile',path.join(msvc,'bin/Hostx64/x64/cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...includes,
    path.join(root,'Tests/UI/flappy_smoke_validation.cpp'),'/Fo'+obj]);
  const libraries=['OvReflection','OvCore','OvRendering','OvUI','OvTools','OvMaths','OvAudio','OvDebug','OvPhysics','OvWindowing','assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'];
  evidence.libraries=libraries.map(x=>{const file=path.join(root,'Bin/Debug',x,x+'.lib'),stat=fs.statSync(file);return {file,size:stat.size,modified:stat.mtime.toISOString()};});
  run('runtime-link',path.join(msvc,'bin/Hostx64/x64/link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+native,obj,
    ...libraries.map(x=>path.join(root,'Bin/Debug',x,x+'.lib')),
    ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x=>x+'.lib')]);

  for(const mode of ['default','render-thread']){
    run('runtime-'+mode,native,[runtime,managed,fixture,path.join(output,mode+'.tga'),mode]);
    if(fs.readFileSync(path.join(output,'runtime-'+mode+'.log'),'utf8').match(/\[ERROR\]|UI rebuild:|callback=.*Exception/))
      throw Error('FlappyBird logged an engine or managed error');
  }
  const serial=fs.readFileSync(path.join(output,'default.tga.rgba'));
  const threaded=fs.readFileSync(path.join(output,'render-thread.tga.rgba'));
  if(serial.length!==400*600*4)throw Error('FlappyBird RGBA size mismatch');
  if(!serial.equals(threaded))throw Error('FlappyBird default and RenderThread pixels differ');
  evidence.pixels={width:400,height:600,sha256:crypto.createHash('sha256').update(serial).digest('hex'),exactRenderThreadMatch:true};
  writePng(path.join(output,'default.tga.rgba'),path.join(output,'preview.png'),400,600);
  writePng(path.join(output,'render-thread.tga.rgba'),path.join(output,'preview-render-thread.png'),400,600);
  for (const state of ['play','dead']) {
    const a=fs.readFileSync(path.join(output,'default.tga.'+state+'.tga.rgba'));
    const b=fs.readFileSync(path.join(output,'render-thread.tga.'+state+'.tga.rgba'));
    if(!a.equals(b)) throw Error(state+' pixels differ between render modes');
    writePng(path.join(output,'default.tga.'+state+'.tga.rgba'),path.join(output,state+'.png'),400,600);
  }
  evidence.preview=path.join(output,'preview.png');

  if(process.argv.includes('--apps')) verifyFlappyApps({root,output,run,msvc,includes,libraries,runtime,managed,fixture});
  fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({
    ...evidence,success:true,productApps:process.argv.includes('--apps'),
    scope:'FlappyBird scene, native input, gameplay and default/RenderThread pixels',
    checks},null,2));
  console.log('Evidence: '+output);
  console.log('Preview: '+path.join(output,'preview.png'));
} catch(error){
  fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({...evidence,success:false,checks,error:String(error)},null,2));
  console.error(String(error));
  process.exitCode=1;
}
