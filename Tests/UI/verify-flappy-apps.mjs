import fs from 'node:fs';
import path from 'node:path';

// Link the product Context/Editor/Game objects unchanged. Only the editor's
// OS profile lookup is isolated, so tests never rewrite the user's preferences.
export function verifyFlappyApps({root,output,run,msvc,includes,libraries,runtime,managed,fixture}) {
  const work=path.join(output,'apps');fs.mkdirSync(work,{recursive:true});
  const data=path.join(work,'Data');fs.mkdirSync(data,{recursive:true});
  for(const name of ['Engine','Editor'])if(!fs.existsSync(path.join(data,name)))fs.symlinkSync(path.join(root,'Resources',name),path.join(data,name),'junction');
  const user=path.join(data,'User');fs.mkdirSync(user,{recursive:true});
  fs.cpSync(path.join(root,'Examples/FlappyBird/Assets'),path.join(user,'Assets'),{recursive:true});
  const profile=path.join(work,'Profile').replaceAll('\\','/');
  fs.mkdirSync(path.join(profile,'OverloadTech/OvEditor'),{recursive:true});
  const original=fs.readFileSync(path.join(root,'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp'),'utf8');
  const start=original.indexOf('std::string OvTools::Utils::SystemCalls::GetPathToAppdata()');
  const body=original.indexOf('{',start);let end=body+1,depth=1;
  for(;depth&&end<original.length;++end){if(original[end]==='{')++depth;else if(original[end]==='}')--depth;}
  if(start<0||depth)throw Error('AppData isolation boundary changed');
  const calls=path.join(work,'SystemCalls.cpp'),callsObj=path.join(work,'SystemCalls.obj');
  fs.writeFileSync(calls,original.slice(0,body)+'{ return "'+profile+'"; }'+original.slice(end));
  const bin=path.join(msvc,'bin/Hostx64/x64');
  const options=[...includes,'/I'+path.join(root,'Sources/OvEditor/include'),'/I'+path.join(root,'Sources/OvGame/include')];
  run('app-profile-compile',path.join(bin,'cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...options,'/UUNICODE','/U_UNICODE',calls,'/Fo'+callsObj]);
  for(const app of ['OvEditor','OvGame']) {
    const obj=path.join(work,app+'.obj'),exe=path.join(work,app+'-flappy.exe');
    run(app+'-compile',path.join(bin,'cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...options,
      ...(app==='OvEditor'?['/DFLAPPY_EDITOR']:[]),path.join(root,'Tests/UI/flappy_app_validation.cpp'),'/Fo'+obj]);
    const directory=path.join(root,'Bin-Int/Debug',app);
    const objects=fs.readdirSync(directory).filter(x=>x.endsWith('.obj')&&x!=='Main.obj').map(x=>path.join(directory,x));
    run(app+'-link',path.join(bin,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+exe,obj,callsObj,...objects,
      ...libraries.map(x=>path.join(root,'Bin/Debug',x,x+'.lib')),
      ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x=>x+'.lib')]);
    for(const mode of ['default','render-thread']) {
      let settings=fs.readFileSync(path.join(root,'Examples/FlappyBird/FlappyBird.ovproject'),'utf8')
        .replace('vsync=true','vsync=false').replace('render_thread=false','render_thread='+(mode==='render-thread'));
      settings+='\ncsharp_auto_build=false\ncsharp_runtime_root='+runtime+'\ncsharp_managed_root='+managed+
        '\ncsharp_project_assembly='+fixture+'\n';
      fs.writeFileSync(path.join(user,'FlappyBird.ovproject'),settings.replace('csharp_enabled=true','csharp_enabled=false'));
      fs.writeFileSync(path.join(user,'FlappyBird.local.ovproject'),settings);fs.writeFileSync(path.join(user,'Game.ini'),settings);
      const name=app+'-'+mode;
      run(name,exe,[user],work);
      if(/\[ERROR\]|UI rebuild:/.test(fs.readFileSync(path.join(output,name+'.log'),'utf8')))
        throw Error(name+' logged an engine error');
    }
  }
}
