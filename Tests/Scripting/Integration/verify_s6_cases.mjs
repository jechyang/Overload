import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
export async function verifyReload({root,output,run,env,compiler,nativeOptions,libraries,gen,python,project,cycles=20}) {
  const work=path.join(output,'apps-s6');fs.mkdirSync(path.join(work,'Data'),{recursive:true});
  for(const name of ['Engine','Editor'])fs.symlinkSync(path.join(root,'Resources',name),path.join(work,'Data',name),'junction');
  const original=fs.readFileSync(path.join(root,'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp'),'utf8');
  const start=original.indexOf('std::string OvTools::Utils::SystemCalls::GetPathToAppdata()');const body=original.indexOf('{',start);let end=body+1,depth=1;
  for(;depth&&end<original.length;++end){if(original[end]==='{')++depth;else if(original[end]==='}')--depth;}
  if(start<0||depth)throw Error('AppData isolation changed');
  const profile=path.join(work,'Profile').replaceAll('\\','/');fs.mkdirSync(path.join(profile,'OverloadTech/OvEditor'),{recursive:true});
  const calls=path.join(work,'SystemCalls.cpp');fs.writeFileSync(calls,original.slice(0,body)+'{ return "'+profile+'"; }'+original.slice(end));
  const includes=['/I'+gen,'/I'+path.join(root,'Dependencies/glfw/include'),'/I'+path.join(root,'Dependencies/bullet3/src'),'/I'+path.join(root,'Sources/OvEditor/include'),...nativeOptions];
  const objects=[];
  for(const [name,source] of [['SystemCalls',calls],['S6',path.join(root,'Tests/Scripting/Integration/native_s6_app_validation.cpp')]]) {
    const object=path.join(work,name+'.obj');objects.push(object);
    run('s6-'+name+'-compile',path.join(compiler,'cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...includes,'/UUNICODE','/U_UNICODE',source,'/Fo'+object]);
  }
  const directory=path.join(root,'Bin-Int/Debug/OvEditor');const production=fs.readdirSync(directory).filter(f=>f.endsWith('.obj')&&f!=='Main.obj').map(f=>path.join(directory,f));
  const exe=path.join(work,'OvEditor-s6.exe');run('s6-editor-link',path.join(compiler,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+exe,...objects,...production,...libraries,
    ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(s=>s+'.lib')]);
  const modes=['default','render-thread','retained-event'];
  for(const mode of modes) {
    const user=path.join(work,'重载 空格 '+mode);fs.mkdirSync(path.join(user,'Assets'),{recursive:true});
    fs.copyFileSync(path.join(project,'s3.lua'),path.join(user,'Assets/s3.lua'));
    fs.writeFileSync(path.join(user,'S6.ovproject'),`gravity=0\nx_resolution=320\ny_resolution=240\nfullscreen=false\nexecutable_name=S6\nstart_scene=\nvsync=false\nmultisampling=false\nsamples=1\nbuild_type=0\nwindow_icon=\nrender_commands=true\nrender_thread=${mode==='render-thread'}\ncsharp_enabled=false\ncsharp_engine_root=${root}\ncsharp_node=${process.execPath}\ncsharp_python=${python}\ncsharp_build_cache=${path.join(work,'cache')}\n`);
    run('s6-editor-'+mode,exe,[user,...(mode==='retained-event'?['leak']:['--cycles',String(cycles)])],0,1800000,work);
  }
  fs.writeFileSync(path.join(work,'reload-verification.json'),JSON.stringify({success:true,modes,build_reload_play_stop_cycles_per_mode:cycles,weak_reference_diagnostics:true},null,2));
}
