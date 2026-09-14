import fs from 'node:fs';
import path from 'node:path';

export function verifyApps({ root, output, run, env, compiler, nativeOptions, libraries, gen, dotnetRoot, production, project }) {
  const work = path.join(output, 'apps'); fs.mkdirSync(work, { recursive:true });
  fs.mkdirSync(path.join(work, 'Data'), { recursive:true });
  for (const name of ['Engine','Editor']) if (!fs.existsSync(path.join(work,'Data',name))) fs.symlinkSync(path.join(root,'Resources',name), path.join(work,'Data',name), 'junction');
  const user = path.join(work,'Data/User'); fs.mkdirSync(path.join(user,'Assets'), { recursive:true });
  fs.copyFileSync(path.join(project,'s3.lua'), path.join(user,'Assets/s3.lua'));
  fs.writeFileSync(path.join(user,'Assets/empty.ovscene'), '<root><scene><actors/></scene></root>');

  // Same isolation used by Tests/Skinning: only OS profile lookup is replaced.
  // Production Editor/Game/EditorActions/Scene/Host dispatch objects are linked unchanged.
  const original = fs.readFileSync(path.join(root,'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp'),'utf8');
  const start = original.indexOf('std::string OvTools::Utils::SystemCalls::GetPathToAppdata()');
  const body = original.indexOf('{',start); let end=body+1, depth=1;
  for (;depth && end<original.length;++end) { if(original[end]==='{')++depth; else if(original[end]==='}')--depth; }
  if(start<0 || depth) throw Error('AppData isolation boundary changed');
  const profile = path.join(work,'Profile').replaceAll('\\','/');
  fs.mkdirSync(path.join(profile,'OverloadTech/OvEditor'),{recursive:true});
  const calls = path.join(work,'SystemCalls.cpp');
  fs.writeFileSync(calls, original.slice(0,body)+'{ return "'+profile+'"; }'+original.slice(end));
  const includes = ['/I'+gen, '/I'+path.join(root,'Dependencies/glfw/include'), '/I'+path.join(root,'Dependencies/bullet3/src'),
    '/I'+path.join(root,'Sources/OvEditor/include'), '/I'+path.join(root,'Sources/OvGame/include'), ...nativeOptions];
  const callsObj = path.join(work,'SystemCalls.obj');
  run('s3-app-profile-compile',path.join(compiler,'cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...includes,'/UUNICODE','/U_UNICODE',calls,'/Fo'+callsObj]);
  for (const app of ['OvEditor','OvGame']) {
    const object=path.join(work,app+'.obj'), executable=path.join(work,app+'-s3.exe');
    run('s3-'+app+'-compile',path.join(compiler,'cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...includes,
      ...(app==='OvEditor'?['/DS3_EDITOR']:[]),path.join(root,'Tests/Scripting/Integration/native_s3_app_validation.cpp'),'/Fo'+object]);
    const directory=path.join(root,'Bin-Int/Debug',app);
    const objects=fs.readdirSync(directory).filter(f=>f.endsWith('.obj') && f !== 'Main.obj').map(f=>path.join(directory,f));
    run('s3-'+app+'-link',path.join(compiler,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+executable,object,callsObj,...objects,...libraries,
      ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(s=>s+'.lib')]);
    for(const mode of ['default','render-thread']) {
      const settings = `gravity=0\nx_resolution=320\ny_resolution=240\nfullscreen=false\nexecutable_name=S3\nstart_scene=empty.ovscene\nvsync=false\nmultisampling=false\nsamples=1\nbuild_type=0\nwindow_icon=\nrender_commands=true\nrender_thread=${mode==='render-thread'}\ncsharp_enabled=true\ncsharp_runtime_root=${dotnetRoot}\ncsharp_managed_root=${production}\ncsharp_project_assembly=${path.join(project,'S3.dll')}\ncsharp_assembly_name=S3\n`;
      fs.writeFileSync(path.join(user,'S3.ovproject'),settings); fs.writeFileSync(path.join(user,'Game.ini'),settings);
      env.OV_S3_TRACE=path.join(output,app+'-'+mode+'-events.txt');
      run('s3-'+app+'-'+mode,executable,[user],0,120000,work);
    }
  }
}
