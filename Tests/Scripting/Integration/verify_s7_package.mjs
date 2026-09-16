import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
export async function verifyPackage({root,output,run,env,config,hostDir,gameDir,dotnetRoot,python,compiler,tc}) {
 const prefix=config+'-package',probe=path.join(output,'SdkProbe.exe');
 run(prefix+'-probe-build',path.join(compiler,'cl.exe'),['/nologo','/MT','/EHsc',path.join(root,'Tests/Scripting/Integration/native_s7_sdk_probe.cpp'),'/Fo'+path.join(output,'SdkProbe.obj'),'/Fe'+probe,'/link','/APPCONTAINER','advapi32.lib']);
 const redist=path.resolve(tc.msvc_root,'../../../Redist/MSVC',tc.msvc_version);
 const crt=path.join(redist,config==='Debug'?'debug_nonredist/x64/Microsoft.VC143.DebugCRT':'x64/Microsoft.VC143.CRT');
 const licenseRoot=path.join(tc.vs_root,'Licenses');
 const redistNotice=fs.readdirSync(licenseRoot).map(n=>path.join(licenseRoot,n,'Redist.txt')).find(p=>fs.existsSync(p));
 if(!redistNotice)throw Error('Installed MSVC redistributable license inventory is missing');
 const destination=path.join(output,'package');
 const assets=path.join(output,'package-assets'),settings=path.join(output,'Game.ini');fs.mkdirSync(assets);
 for(const name of ['sample.ovscene','s3.lua'])fs.copyFileSync(path.join(gameDir,name),path.join(assets,name));
 const ini=['gravity=0','x_resolution=320','y_resolution=240','fullscreen=false','executable_name=S7','start_scene=sample.ovscene','vsync=false','multisampling=false','samples=1','build_type=0','window_icon=','render_commands=true','render_thread=false','csharp_enabled=true','csharp_runtime_root=Runtime/dotnet','csharp_managed_root=Managed','csharp_project_assembly=Managed/GameScripts.dll','csharp_assembly_name=GameScripts',''];
fs.writeFileSync(settings,ini.join('\n'));

 run(prefix+'-create',process.execPath,[path.join(root,'Scripts/Windows/PackageCSharpGame.mjs'),'--configuration',config,'--assets',assets,'--settings',settings,'--destination',destination,'--runtime',dotnetRoot,'--host',hostDir,'--game',gameDir,'--crt',crt,'--probe',probe,'--redist-notice',redistNotice]);
 run(prefix+'-imports',path.join(compiler,'dumpbin.exe'),['/dependents',path.join(destination,'OvGame.exe')]);
 const result=run(prefix+'-isolated',python,[path.join(root,'Tests/Scripting/Integration/run_s7_isolated.py'),'--package',destination,'--sdk',dotnetRoot],{expected:null,timeout:240000});
 const receipt=JSON.parse(fs.readFileSync(path.join(destination,'Evidence/isolation-verification.json'),'utf8'));
 // Runtime evidence is outside the immutable artifact list. Every shipped file must remain unchanged.
 const manifest=JSON.parse(fs.readFileSync(path.join(destination,'s7-package.json'),'utf8'));
 const modified=manifest.files.filter(f=>crypto.createHash('sha256').update(fs.readFileSync(path.join(destination,f.path))).digest('hex')!==f.sha256);
 if(modified.length)throw Error('Packaged artifacts mutated: '+modified.map(f=>f.path));
 if(result.status!==0||!receipt.success)throw Error('Isolated packaged OvGame failed: '+receipt.error);
 return receipt;
}
