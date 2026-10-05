import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const output = path.join(root, 'Build/EditorPanels/Validation');
fs.mkdirSync(output, {recursive:true});
const generated = path.join(root, 'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug');
const args = JSON.parse(fs.readFileSync(path.join(generated, 'OvCore/compile_commands.json'), 'utf8'))[0].arguments;
const system = args.filter(x => x.startsWith('/clang:C:/')).map(x => x.slice(7));
const msvc = path.dirname(system.find(x => x.includes('/MSVC/') && x.endsWith('/include')));
const ucrt = system.find(x => x.endsWith('/ucrt'));
const version = path.basename(path.dirname(ucrt)), sdk = path.resolve(ucrt, '../../..');
const env = {};
for (const [key,value] of Object.entries(process.env))
  if (!Object.keys(env).some(k => k.toLowerCase() === key.toLowerCase())) env[key] = value;
env.LIB = [path.join(msvc, 'lib/x64'), ...['ucrt','um'].map(x => path.join(sdk, 'Lib', version, x, 'x64'))].join(';');
env.TEMP = env.TMP = path.join(output, 'Temp');
fs.mkdirSync(env.TEMP, {recursive:true});
const includes = [...args.filter(x => (x.startsWith('/I') || x.startsWith('/D')) && !x.includes('OV_REFLECTION_ANALYSIS')),
  ...system.map(x => '/I' + x), '/I' + root, '/I' + generated, '/I' + path.join(root, 'Sources/OvEditor/include'),
  ...['glfw/include','bullet3/src'].map(x => '/I' + path.join(root, 'Dependencies', x))];
const checks = [];
function run(name, exe, argv, cwd = root) {
  const fd = fs.openSync(path.join(output, name + '.log'), 'w');
  const result = cp.spawnSync(exe, argv, {cwd, env, windowsHide:true, stdio:['ignore',fd,fd], timeout:180000});
  fs.closeSync(fd);
  checks.push({name, exit:result.status, error:String(result.error || '')});
  fs.writeFileSync(path.join(output, 'results.json'), JSON.stringify(checks, null, 2));
  if (result.status !== 0) throw Error(name + ' failed; see ' + path.join(output, name + '.log'));
  console.log('PASS: ' + name);
}

// Isolate the OS profile only; link production editor and UI objects unchanged.
const profile = path.join(output, 'Profile').replaceAll('\\','/');
fs.mkdirSync(path.join(profile, 'OverloadTech/OvEditor'), {recursive:true});
const original = fs.readFileSync(path.join(root, 'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp'), 'utf8');
const start = original.indexOf('std::string OvTools::Utils::SystemCalls::GetPathToAppdata()');
const body = original.indexOf('{', start); let end = body + 1, depth = 1;
for (; depth && end < original.length; ++end) { if (original[end] === '{') ++depth; else if (original[end] === '}') --depth; }
if (start < 0 || depth) throw Error('AppData isolation boundary changed');
const calls = path.join(output, 'SystemCalls.cpp'), callsObj = path.join(output, 'SystemCalls.obj');
fs.writeFileSync(calls, original.slice(0,body) + '{ return "' + profile + '"; }' + original.slice(end));
const bin = path.join(msvc, 'bin/Hostx64/x64');
const options = ['/nologo','/std:c++20','/EHsc','/MDd','/c', ...includes];
run('profile-compile', path.join(bin, 'cl.exe'), [...options, '/UUNICODE', '/U_UNICODE', calls, '/Fo' + callsObj]);
const obj = path.join(output, 'panels.obj'), exe = path.join(output, 'panels.exe');
run('test-compile', path.join(bin, 'cl.exe'), [...options, path.join(root, 'Tests/Editor/panel_validation.cpp'), '/Fo' + obj]);
const objectsDir = path.join(root, 'Bin-Int/Debug/OvEditor');
const objects = fs.readdirSync(objectsDir).filter(x => x.endsWith('.obj') && x !== 'Main.obj').map(x => path.join(objectsDir,x));
const libraries = ['OvReflection','OvCore','OvRendering','OvUI','OvTools','OvMaths','OvAudio','OvDebug','OvPhysics','OvWindowing',
  'assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'];
run('test-link', path.join(bin, 'link.exe'), ['/nologo','/OPT:REF','/OPT:ICF','/OUT:' + exe, obj, callsObj, ...objects,
  ...libraries.map(x => path.join(root, 'Bin/Debug', x, x + '.lib')),
  ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x => x + '.lib')]);
const data = path.join(output, 'Data'); fs.mkdirSync(data, {recursive:true});
for (const name of ['Engine','Editor'])
  if (!fs.existsSync(path.join(data,name))) fs.symlinkSync(path.join(root,'Resources',name), path.join(data,name), 'junction');
const project = path.join(data, 'User'); fs.mkdirSync(path.join(project, 'Assets'), {recursive:true});
// Valid PCM fixture exercises the production sound loader without playback.
const tone = Buffer.alloc(52); tone.write('RIFF'); tone.writeUInt32LE(44,4); tone.write('WAVEfmt ',8);
tone.writeUInt32LE(16,16); tone.writeUInt16LE(1,20); tone.writeUInt16LE(1,22);
tone.writeUInt32LE(8000,24); tone.writeUInt32LE(16000,28); tone.writeUInt16LE(2,32); tone.writeUInt16LE(16,34);
tone.write('data',36); tone.writeUInt32LE(8,40);
fs.writeFileSync(path.join(project, 'Assets/ReflectionTone.wav'), tone);
fs.writeFileSync(path.join(project, 'Assets/spawn.lua'), `local Script = { spawned = false }
function Script:OnUpdate(dt)
  if not self.spawned then
    self.spawned = true
    local entity = Scenes.GetCurrentScene():CreateActor("Runtime Entity", "Runtime")
    entity:SetParent(self.owner)
  end
end
return Script
`);
for (const mode of ['default','render-thread']) {
  fs.writeFileSync(path.join(project, 'Panels.ovproject'),
    `gravity=0\nx_resolution=960\ny_resolution=720\nfullscreen=false\nexecutable_name=Panels\nstart_scene=\nvsync=false\nmultisampling=false\nsamples=1\nbuild_type=0\nwindow_icon=\nrender_commands=true\nrender_thread=${mode === 'render-thread'}\ncsharp_enabled=false\ncsharp_auto_build=false\n`);
  run(mode, exe, [project], output);
}
console.log('Evidence: ' + output);
