import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
export async function verifyWorkflow({root,output,run,env,compiler,nativeOptions,libraries,gen,python,project}) {
  const work=path.join(output,'apps-s5');fs.mkdirSync(path.join(work,'Data'),{recursive:true});
  for(const name of ['Engine','Editor'])fs.symlinkSync(path.join(root,'Resources',name),path.join(work,'Data',name),'junction');
  const original=fs.readFileSync(path.join(root,'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp'),'utf8');
  const start=original.indexOf('std::string OvTools::Utils::SystemCalls::GetPathToAppdata()');const body=original.indexOf('{',start);let end=body+1,depth=1;
  for(;depth&&end<original.length;++end){if(original[end]==='{')++depth;else if(original[end]==='}')--depth;}
  if(start<0||depth)throw Error('AppData isolation changed');
  const profile=path.join(work,'Profile').replaceAll('\\','/');fs.mkdirSync(path.join(profile,'OverloadTech/OvEditor'),{recursive:true});
  const calls=path.join(work,'SystemCalls.cpp');fs.writeFileSync(calls,original.slice(0,body)+'{ return "'+profile+'"; }'+original.slice(end));
  const includes=['/I'+gen,'/I'+path.join(root,'Dependencies/glfw/include'),'/I'+path.join(root,'Dependencies/bullet3/src'),'/I'+path.join(root,'Sources/OvEditor/include'),...nativeOptions];
  const objects=[];
  for(const [name,source] of [['SystemCalls',calls],['S5',path.join(root,'Tests/Scripting/Integration/native_s5_app_validation.cpp')]]) {
    const object=path.join(work,name+'.obj');objects.push(object);
    run('s5-'+name+'-compile',path.join(compiler,'cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...includes,'/UUNICODE','/U_UNICODE',source,'/Fo'+object]);
  }
  const directory=path.join(root,'Bin-Int/Debug/OvEditor');const production=fs.readdirSync(directory).filter(f=>f.endsWith('.obj')&&f!=='Main.obj').map(f=>path.join(directory,f));
  const exe=path.join(work,'OvEditor-s5.exe');run('s5-editor-link',path.join(compiler,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+exe,...objects,...production,...libraries,
    ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(s=>s+'.lib')]);
  const users=[];
  const cacheFor=user=>user.endsWith('default')?path.join(user,'输出 cache'):path.join(work,'cache');
  for(const mode of ['default','render-thread']) {
    const user=path.join(work,'新项目 空格 '+mode);fs.mkdirSync(path.join(user,'Assets'),{recursive:true});users.push(user);
    fs.copyFileSync(path.join(project,'s3.lua'),path.join(user,'Assets/s3.lua'));
    fs.writeFileSync(path.join(user,'S5.ovproject'),`gravity=0\nx_resolution=320\ny_resolution=240\nfullscreen=false\nexecutable_name=S5\nstart_scene=\nvsync=false\nmultisampling=false\nsamples=1\nbuild_type=0\nwindow_icon=\nrender_commands=true\nrender_thread=${mode==='render-thread'}\ncsharp_enabled=false\ncsharp_engine_root=${root}\ncsharp_node=${process.execPath}\ncsharp_python=${python}\ncsharp_build_cache=${cacheFor(user)}\n`);
    run('s5-editor-'+mode,exe,[user],0,600000,work);
    run('s5-reopen-'+mode,exe,[user,'reopen'],0,120000,work);
  }
  // Independent processes requesting the same project/configuration cannot race outputs.
  const workers=[0,1].map(i=>new Promise((resolve,reject)=>{
    const log=fs.openSync(path.join(work,'concurrent-'+i+'.log'),'w');
    const child=spawn(process.execPath,[path.join(root,'Scripts/Windows/BuildGameScripts.mjs'),'--project',users[0],'--cache',cacheFor(users[0]),'--python',python,'--configuration','Release','--result',path.join(work,'concurrent-'+i+'.xml')],{cwd:root,env,windowsHide:true,stdio:['ignore',log,log]});
    const timer=setTimeout(()=>{child.kill();reject(Error('Concurrent build timeout'));},600000);
    child.on('error',reject);child.on('exit',code=>{clearTimeout(timer);fs.closeSync(log);if(code!==0)reject(Error('Concurrent worker failed: '+code));else resolve();});
  }));await Promise.all(workers);
  const receipts=[0,1].map(i=>fs.readFileSync(path.join(work,'concurrent-'+i+'.xml'),'utf8'));
  if(receipts[0]!==receipts[1]||!receipts[0].includes('unchanged="true"'))throw Error('Concurrent unchanged build generation mismatch');
  fs.writeFileSync(path.join(work,'workflow-verification.json'),JSON.stringify({success:true,modes:['default','render-thread'],concurrent_receipts:receipts},null,2));
}
