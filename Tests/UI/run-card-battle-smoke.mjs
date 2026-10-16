import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url)), root = path.resolve(here, '../..');
const work = path.join(root, 'Build/CardBattleValidation');
const config = 'Debug';
const build = path.join(work, 'Smoke');
fs.mkdirSync(build, {recursive: true});

// 1. Build CardBattle C# scripts first
console.log('Building CardBattle C# assemblies...');
const buildMjs = path.join(root, 'Examples/CardBattle/build.mjs');
const buildRes = cp.spawnSync(process.execPath, [buildMjs, 'C:/Users/Administrator/AppData/Local/Programs/Python/Python312/python.exe'], {cwd: root, encoding: 'utf8'});
if (buildRes.status !== 0) {
  console.error(buildRes.stdout, buildRes.stderr);
  throw Error('Failed to build CardBattle scripts');
}

// 2. Discover toolchain
const vswhere = 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe';
const vs = cp.execFileSync(vswhere, ['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'], {windowsHide: true, encoding: 'utf8'}).trim();
const version = fs.readFileSync(path.join(vs, 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'), 'utf8').trim();
const msvc = path.join(vs, 'VC/Tools/MSVC', version), sdk = 'C:/Program Files (x86)/Windows Kits/10';
const sdkVersion = fs.readdirSync(path.join(sdk, 'Include')).filter(x => fs.existsSync(path.join(sdk, 'Lib', x, 'um/x64/kernel32.lib'))).sort((a, b) => a.localeCompare(b, undefined, {numeric: true})).at(-1);

const libs = ['assimp','baregl','bullet3','freetype','glfw','ImGui','lua','OvAudio','OvCore','OvDebug','OvMaths','OvPhysics','OvReflection','OvRendering','OvTools','OvUI','OvWindowing','soloud','tinyxml2','tracy'].map(x => path.join(root, 'Bin', config, x, x + '.lib'));
const project = fs.readFileSync(path.join(root, 'Sources/OvEditor/OvEditor.vcxproj'), 'utf8');
const active = [...project.matchAll(/<ClCompile Include="([^"]+\.cpp)"/g)].map(m => path.win32.basename(m[1], '.cpp') + '.obj').filter(x => !['Main.obj'].includes(x));
const objects = active.map(x => path.join(root, 'Bin-Int', config, 'OvEditor', x));

// SystemCalls isolation
const profile = path.join(build, 'Profile').replaceAll('\\', '/');
fs.mkdirSync(path.join(profile, 'OverloadTech/OvEditor'), {recursive: true});
const callsSource = fs.readFileSync(path.join(root, 'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp'), 'utf8');
const start = callsSource.indexOf('std::string OvTools::Utils::SystemCalls::GetPathToAppdata()');
const body = callsSource.indexOf('{', start); let end = body + 1, depth = 1;
for (; depth && end < callsSource.length; ++end) { if (callsSource[end] === '{') ++depth; else if (callsSource[end] === '}') --depth; }
const isolatedCalls = callsSource.slice(0, body) + '{ return ' + JSON.stringify(profile) + '; }' + callsSource.slice(end);
fs.writeFileSync(path.join(build, 'SystemCalls.cpp'), isolatedCalls);

const depIncludes = ['baregl/include','ImGui/include','lua/include','sol/include','tinyxml2/include','tracy','glfw/include','bullet3/src','soloud/include','assimp/include'];
const exe = path.join(build, 'CardBattleSmoke.exe');
const args = ['/nologo','/std:c++20','/EHsc','/W3','/bigobj','/Gy','/MDd','/Od','/Zi',
  '/DTRACY_ENABLE','/DTRACY_ON_DEMAND','/DTRACY_MEMORY_ENABLE','/DLUA_SCRIPTING','/DSOL_NO_LUA_HPP','/D_CRT_SECURE_NO_WARNINGS','/D_DEBUG',
  '/I' + path.join(msvc, 'include'), ...['ucrt','shared','um','winrt'].map(x => '/I' + path.join(sdk, 'Include', sdkVersion, x)),
  ...depIncludes.map(x => '/I' + path.join(root, 'Dependencies', x)), ...fs.readdirSync(path.join(root, 'Sources')).filter(x => x.startsWith('Ov')).map(x => '/I' + path.join(root, 'Sources', x, 'include')),
  path.join(here, 'card_battle_app_validation.cpp'), path.join(build, 'SystemCalls.cpp'), ...objects.filter(x => !x.endsWith('SystemCalls.obj')), ...libs,
  '/Fe:' + exe, '/Fo:' + build + path.sep, '/Fd:' + path.join(build, 'compile.pdb'), '/link', '/DEBUG:NONE', '/INCREMENTAL:NO', '/SUBSYSTEM:CONSOLE',
  '/LIBPATH:' + path.join(msvc, 'lib/x64'), ...['ucrt','um'].map(x => '/LIBPATH:' + path.join(sdk, 'Lib', sdkVersion, x + '/x64')),
  ...['gdi32','advapi32','ole32','oleaut32','shell32','comdlg32','uuid','opengl32','dbghelp','ws2_32','winmm','imm32','version','dwmapi'].map(x => x + '.lib')];

const response = path.join(build, 'compile.rsp');
fs.writeFileSync(response, args.map(x => '"' + x.replaceAll('\\', '/') + '"').join(' '));
console.log('Compiling CardBattle validation harness...');
const compRes = cp.spawnSync(path.join(msvc, 'bin/Hostx64/x64/cl.exe'), ['@' + response], {cwd: build, encoding: 'utf8'});
if (compRes.status !== 0) {
  fs.writeFileSync(path.join(build, 'compile.log'), compRes.stdout + compRes.stderr);
  console.error(compRes.stdout, compRes.stderr);
  throw Error('Compile failed: ' + path.join(build, 'compile.log'));
}
const runtime = path.join(build, 'Runtime');
fs.mkdirSync(path.join(runtime, 'Data'), {recursive: true});
for (const area of ['Engine', 'Editor']) {
  const link = path.join(runtime, 'Data', area);
  if (!fs.existsSync(link)) fs.symlinkSync(path.join(root, 'Resources', area), link, 'junction');
}

console.log('Running CardBattle validation...');
const runRes = cp.spawnSync(exe, [path.join(root, 'Examples/CardBattle')], {cwd: runtime, encoding: 'utf8', timeout: 60000});
console.log(runRes.stdout);
if (runRes.status !== 0) {
  console.error(runRes.stderr);
  throw Error('CardBattle smoke validation failed');
}
console.log('ALL CARD BATTLE VALIDATION CHECKS PASSED!');
