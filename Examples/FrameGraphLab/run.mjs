// VS2022 example host. Uses the current native product objects, never copies models.
import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';
const example=path.dirname(fileURLToPath(import.meta.url)),root=path.resolve(example,'../..');
const config=process.argv.includes('--debug')?'Debug':'Release';
const buildOnly=process.argv.includes('--build-only');
const verify=process.argv.includes('--verify');
const owner=process.argv.includes('--main')?'main':'render';
const build=path.join(root,'Build/FrameGraphLab',config);
const output=path.join(root,'Build/FrameGraphLabValidation',config+'-'+owner);
fs.mkdirSync(build,{recursive:true});
const vswhere='C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe';
const vs=process.env.VSINSTALLDIR||cp.execFileSync(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],{windowsHide:true,encoding:'utf8'}).trim();
const version=fs.readFileSync(path.join(vs,'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'),'utf8').trim();
const msvc=path.join(vs,'VC/Tools/MSVC',version),sdk=process.env.WindowsSdkDir||'C:/Program Files (x86)/Windows Kits/10';
const sdkVersion=fs.readdirSync(path.join(sdk,'Include')).filter(x=>fs.existsSync(path.join(sdk,'Lib',x,'um/x64/kernel32.lib'))).sort((a,b)=>a.localeCompare(b,undefined,{numeric:true})).at(-1);
const libs=['assimp','baregl','bullet3','freetype','glfw','ImGui','lua','OvAudio','OvCore','OvDebug','OvMaths','OvPhysics','OvReflection','OvRendering','OvTools','OvUI','OvWindowing','soloud','tinyxml2','tracy'].map(x=>path.join(root,'Bin',config,x,x+'.lib'));
const project=fs.readFileSync(path.join(root,'Sources/OvEditor/OvEditor.vcxproj'),'utf8');
const active=new Set([...project.matchAll(/<ClCompile Include="([^"]+\.cpp)"/g)].map(m=>path.win32.basename(m[1],'.cpp')+'.obj'));
const objects=fs.readdirSync(path.join(root,'Bin-Int',config,'OvEditor')).filter(x=>active.has(x)&&!['Main.obj','Context.obj'].includes(x)).map(x=>path.join(root,'Bin-Int',config,'OvEditor',x));
for(const file of [...libs,...[...active].filter(x=>!['Main.obj','Context.obj'].includes(x)).map(x=>path.join(root,'Bin-Int',config,'OvEditor',x))])
  if(!fs.existsSync(file))throw Error('Build the '+config+' solution first; missing '+file);
// Isolated editor preferences, plus an opt-in hidden window for native verification.
let context=fs.readFileSync(path.join(root,'Sources/OvEditor/src/OvEditor/Core/Context.cpp'),'utf8');
if(!context.includes('window = std::make_unique<OvWindowing::Window>'))throw Error('Context window boundary changed');
context='#include <cstdlib>\n'+context.replace('window = std::make_unique<OvWindowing::Window>','windowSettings.visible = std::getenv("OV_FRAMEGRAPH_LAB_HIDDEN") == nullptr; windowSettings.focused = windowSettings.visible;\n\twindow = std::make_unique<OvWindowing::Window>');
fs.writeFileSync(path.join(build,'Context.cpp'),context);
let calls=fs.readFileSync(path.join(root,'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp'),'utf8');
const start=calls.indexOf('std::string OvTools::Utils::SystemCalls::GetPathToAppdata()');
if(start<0)throw Error('Appdata isolation boundary changed');
const body=calls.indexOf('{',start);let end=body+1,depth=1;
for(;depth&&end<calls.length;++end){if(calls[end]==='{')++depth;else if(calls[end]==='}')--depth;}
if(depth)throw Error('Appdata isolation body changed');
fs.writeFileSync(path.join(build,'SystemCalls.cpp'),calls.slice(0,body)+'{ return '+JSON.stringify(path.join(build,'Profile').replaceAll('\\','/'))+'; }'+calls.slice(end));
let window=fs.readFileSync(path.join(root,'Sources/OvWindowing/src/OvWindowing/Window.cpp'),'utf8');
if(!window.includes('glfwSwapBuffers(m_glfwWindow);'))throw Error('Window swap boundary changed');
fs.writeFileSync(path.join(build,'Window.cpp'),'extern void FrameGraphLabBeforePresent();\n'+window.replace('glfwSwapBuffers(m_glfwWindow);','FrameGraphLabBeforePresent();\n\tglfwSwapBuffers(m_glfwWindow);'));
const depIncludes=['baregl/include','ImGui/include','lua/include','sol/include','tinyxml2/include','tracy','glfw/include','bullet3/src','soloud/include','assimp/include'];
const exe=path.join(build,'FrameGraphLabEditor.exe');
const args=['/nologo','/std:c++20','/EHsc','/W3','/WX','/bigobj','/Gy',config==='Debug'?'/MDd':'/MD',config==='Debug'?'/Od':'/O2','/Zi',
  '/DTRACY_ENABLE','/DTRACY_ON_DEMAND','/DTRACY_MEMORY_ENABLE','/DLUA_SCRIPTING','/DSOL_NO_LUA_HPP','/D_CRT_SECURE_NO_WARNINGS',config==='Debug'?'/D_DEBUG':'/DNDEBUG',
  '/I'+path.join(msvc,'include'),...['ucrt','shared','um','winrt'].map(x=>'/I'+path.join(sdk,'Include',sdkVersion,x)),
  ...depIncludes.map(x=>'/I'+path.join(root,'Dependencies',x)),...fs.readdirSync(path.join(root,'Sources')).filter(x=>x.startsWith('Ov')).map(x=>'/I'+path.join(root,'Sources',x,'include')),
  path.join(example,'EditorLab.cpp'),...['Context','SystemCalls','Window'].map(x=>path.join(build,x+'.cpp')),...objects,...libs,
  '/Fe:'+exe,'/Fo:'+build+path.sep,'/Fd:'+path.join(build,'compile.pdb'),'/link','/DEBUG:NONE','/INCREMENTAL:NO','/SUBSYSTEM:CONSOLE',
  '/LIBPATH:'+path.join(msvc,'lib/x64'),...['ucrt','um'].map(x=>'/LIBPATH:'+path.join(sdk,'Lib',sdkVersion,x+'/x64')),
  ...['gdi32','advapi32','ole32','oleaut32','shell32','comdlg32','uuid','opengl32','dbghelp','ws2_32','winmm','imm32','version','dwmapi'].map(x=>x+'.lib')];
function logged(file,args,log,env=process.env,timeout=240000){
  const fd=fs.openSync(log,'w');const result=cp.spawnSync(file,args,{cwd:build,env,windowsHide:true,stdio:['ignore',fd,fd],timeout});fs.closeSync(fd);
  if(result.error)fs.appendFileSync(log,String(result.error));
  if(result.status!==0)throw Error(path.basename(file)+' failed; see '+log);
}
// A response file avoids the Windows command line length limit.
const response=path.join(build,'compile.rsp');
fs.writeFileSync(response,args.map(x=>'"'+x.replaceAll('\\','/')+'"').join(' '));
logged(path.join(msvc,'bin/Hostx64/x64/cl.exe'),['@'+response],path.join(build,'compile.log'));
console.log('Built '+exe);
if(buildOnly)process.exit(0);
// Use resource directory junctions, independent of stale published Build/Data.
const runtime=path.join(build,'Runtime');fs.mkdirSync(path.join(runtime,'Data'),{recursive:true});
for(const area of ['Engine','Editor']){
  const link=path.join(runtime,'Data',area),source=path.join(root,'Resources',area);
  if(!fs.existsSync(link))fs.symlinkSync(source,link,'junction');
  if(fs.realpathSync(link).toLowerCase()!==fs.realpathSync(source).toLowerCase())throw Error('Unexpected existing resource path '+link);
}
let launchProject=example;
if(verify){
  launchProject=path.join(output,'Project');fs.mkdirSync(launchProject,{recursive:true});
  fs.writeFileSync(path.join(launchProject,'FrameGraphLab.ovproject'),fs.readFileSync(path.join(example,'FrameGraphLab.ovproject'),'utf8').replace('render_thread=true','render_thread='+String(owner==='render')));
  const link=path.join(launchProject,'Assets');if(!fs.existsSync(link))fs.symlinkSync(path.join(example,'Assets'),link,'junction');
  if(fs.realpathSync(link).toLowerCase()!==fs.realpathSync(path.join(example,'Assets')).toLowerCase())throw Error('Unexpected project asset path');
  const fd=fs.openSync(path.join(output,'run.log'),'w');
  const result=cp.spawnSync(exe,[launchProject,output],{cwd:runtime,env:{...process.env,OV_FRAMEGRAPH_LAB_HIDDEN:'1'},windowsHide:true,stdio:['ignore',fd,fd],timeout:240000});fs.closeSync(fd);
  if(result.status!==0)throw Error('Native lab verification failed; see '+path.join(output,'run.log'));
  console.log('Native captures: '+output);
}else{
  const child=cp.spawn(exe,[launchProject],{cwd:runtime,windowsHide:true,stdio:'inherit'});
  child.on('exit',code=>process.exit(code??1));
}
