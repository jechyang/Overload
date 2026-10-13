// Builds and runs shell_validation.cpp against the current Debug editor objects, following
// Examples/FrameGraphLab/run.mjs: isolated AppData profile, hidden window, resource junctions.
import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import zlib from 'node:zlib';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url)), root = path.resolve(here, '../..');
const work = path.join(root, 'Build/EditorUnity');
const config = 'Debug';
const build = path.join(work, 'ShellLab'), output = path.join(work, 'ShellLabOutput');
const visible = process.argv.includes('--visible');
fs.rmSync(path.join(build, 'Profile'), {recursive: true, force: true});
fs.rmSync(output, {recursive: true, force: true});
fs.mkdirSync(build, {recursive: true}); fs.mkdirSync(output, {recursive: true}); fs.mkdirSync(path.join(work, 'Temp'), {recursive: true});

const vswhere = 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe';
const vs = cp.execFileSync(vswhere, ['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'], {windowsHide: true, encoding: 'utf8'}).trim();
const version = fs.readFileSync(path.join(vs, 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'), 'utf8').trim();
const msvc = path.join(vs, 'VC/Tools/MSVC', version), sdk = 'C:/Program Files (x86)/Windows Kits/10';
const sdkVersion = fs.readdirSync(path.join(sdk, 'Include')).filter(x => fs.existsSync(path.join(sdk, 'Lib', x, 'um/x64/kernel32.lib'))).sort((a, b) => a.localeCompare(b, undefined, {numeric: true})).at(-1);
const libs = ['assimp','baregl','bullet3','freetype','glfw','ImGui','lua','OvAudio','OvCore','OvDebug','OvMaths','OvPhysics','OvReflection','OvRendering','OvTools','OvUI','OvWindowing','soloud','tinyxml2','tracy'].map(x => path.join(root, 'Bin', config, x, x + '.lib'));
const project = fs.readFileSync(path.join(root, 'Sources/OvEditor/OvEditor.vcxproj'), 'utf8');
const active = [...project.matchAll(/<ClCompile Include="([^"]+\.cpp)"/g)].map(m => path.win32.basename(m[1], '.cpp') + '.obj').filter(x => !['Main.obj','Context.obj'].includes(x));
const objects = active.map(x => path.join(root, 'Bin-Int', config, 'OvEditor', x));
for (const file of [...libs, ...objects]) if (!fs.existsSync(file)) throw Error('Missing ' + file);

let context = fs.readFileSync(path.join(root, 'Sources/OvEditor/src/OvEditor/Core/Context.cpp'), 'utf8');
if (!context.includes('window = std::make_unique<OvWindowing::Window>')) throw Error('Context window boundary changed');
context = '#include <cstdlib>\n' + context.replace('window = std::make_unique<OvWindowing::Window>', 'windowSettings.visible = std::getenv("OV_SHELL_LAB_HIDDEN") == nullptr; windowSettings.focused = windowSettings.visible;\n\twindow = std::make_unique<OvWindowing::Window>');
fs.writeFileSync(path.join(build, 'Context.cpp'), context);
let calls = fs.readFileSync(path.join(root, 'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp'), 'utf8');
const start = calls.indexOf('std::string OvTools::Utils::SystemCalls::GetPathToAppdata()');
if (start < 0) throw Error('Appdata isolation boundary changed');
const body = calls.indexOf('{', start); let end = body + 1, depth = 1;
for (; depth && end < calls.length; ++end) { if (calls[end] === '{') ++depth; else if (calls[end] === '}') --depth; }
fs.writeFileSync(path.join(build, 'SystemCalls.cpp'), calls.slice(0, body) + '{ return ' + JSON.stringify(path.join(build, 'Profile').replaceAll('\\', '/')) + '; }' + calls.slice(end));
let window = fs.readFileSync(path.join(root, 'Sources/OvWindowing/src/OvWindowing/Window.cpp'), 'utf8');
if (!window.includes('glfwSwapBuffers(m_glfwWindow);')) throw Error('Window swap boundary changed');
fs.writeFileSync(path.join(build, 'Window.cpp'), 'extern void EditorShellBeforePresent();\n' + window.replace('glfwSwapBuffers(m_glfwWindow);', 'EditorShellBeforePresent();\n\tglfwSwapBuffers(m_glfwWindow);'));

const depIncludes = ['baregl/include','ImGui/include','lua/include','sol/include','tinyxml2/include','tracy','glfw/include','bullet3/src','soloud/include','assimp/include'];
const exe = path.join(build, 'ShellLab.exe');
const args = ['/nologo','/std:c++20','/EHsc','/W3','/WX','/bigobj','/Gy','/MDd','/Od','/Zi',
  '/DTRACY_ENABLE','/DTRACY_ON_DEMAND','/DTRACY_MEMORY_ENABLE','/DLUA_SCRIPTING','/DSOL_NO_LUA_HPP','/D_CRT_SECURE_NO_WARNINGS','/D_DEBUG',
  '/I' + path.join(msvc, 'include'), ...['ucrt','shared','um','winrt'].map(x => '/I' + path.join(sdk, 'Include', sdkVersion, x)),
  ...depIncludes.map(x => '/I' + path.join(root, 'Dependencies', x)), ...fs.readdirSync(path.join(root, 'Sources')).filter(x => x.startsWith('Ov')).map(x => '/I' + path.join(root, 'Sources', x, 'include')),
  path.join(here, 'shell_validation.cpp'), ...['Context','SystemCalls','Window'].map(x => path.join(build, x + '.cpp')), ...objects, ...libs,
  '/Fe:' + exe, '/Fo:' + build + path.sep, '/Fd:' + path.join(build, 'compile.pdb'), '/link', '/DEBUG:NONE', '/INCREMENTAL:NO', '/SUBSYSTEM:CONSOLE',
  '/LIBPATH:' + path.join(msvc, 'lib/x64'), ...['ucrt','um'].map(x => '/LIBPATH:' + path.join(sdk, 'Lib', sdkVersion, x + '/x64')),
  ...['gdi32','advapi32','ole32','oleaut32','shell32','comdlg32','uuid','opengl32','dbghelp','ws2_32','winmm','imm32','version','dwmapi'].map(x => x + '.lib')];
const response = path.join(build, 'compile.rsp');
fs.writeFileSync(response, args.map(x => '"' + x.replaceAll('\\', '/') + '"').join(' '));
const env = {...process.env, TEMP: path.join(work, 'Temp'), TMP: path.join(work, 'Temp')};
let result = cp.spawnSync(path.join(msvc, 'bin/Hostx64/x64/cl.exe'), ['@' + response], {cwd: build, env, windowsHide: true, encoding: 'utf8', maxBuffer: 1 << 26});
fs.writeFileSync(path.join(build, 'compile.log'), result.stdout + result.stderr);
if (result.status !== 0) throw Error('Compile failed; see ' + path.join(build, 'compile.log'));
console.log('Built ' + exe);

const runtime = path.join(build, 'Runtime'); fs.mkdirSync(path.join(runtime, 'Data'), {recursive: true});
for (const area of ['Engine','Editor']) {
  const link = path.join(runtime, 'Data', area);
  if (!fs.existsSync(link)) fs.symlinkSync(path.join(root, 'Resources', area), link, 'junction');
}
const example = path.join(root, 'Examples/FrameGraphLab'), launchProject = path.join(build, 'Project');
fs.mkdirSync(launchProject, {recursive: true});
fs.writeFileSync(path.join(launchProject, 'FrameGraphLab.ovproject'), fs.readFileSync(path.join(example, 'FrameGraphLab.ovproject'), 'utf8'));
const assets = path.join(launchProject, 'Assets');
if (!fs.existsSync(assets)) fs.symlinkSync(path.join(example, 'Assets'), assets, 'junction');

result = cp.spawnSync(exe, [launchProject, output], {cwd: runtime, env: visible ? env : {...env, OV_SHELL_LAB_HIDDEN: '1'}, windowsHide: true, encoding: 'utf8', timeout: 300000, maxBuffer: 1 << 26});
fs.writeFileSync(path.join(output, 'run.log'), (result.stdout || '') + (result.stderr || '') + String(result.error || ''));
console.log((result.stdout || '').trim());
if (result.status !== 0) { console.error((result.stderr || '') + String(result.error || '')); throw Error('ShellLab failed; see ' + path.join(output, 'run.log')); }

// Convert bottom-up RGBA captures to PNG.
const crcTable = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc = buf => { let c = 0xffffffff; for (const b of buf) c = crcTable[(c ^ b) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([len, td, c]); };
const [width, height] = fs.readFileSync(path.join(output, 'size.txt'), 'utf8').trim().split(/\s+/).map(Number);
for (const file of fs.readdirSync(output).filter(x => x.endsWith('.rgba'))) {
  const rgba = fs.readFileSync(path.join(output, file));
  const raw = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; ++y) {
    const src = (height - 1 - y) * width * 4, dst = y * (width * 3 + 1);
    for (let x = 0; x < width; ++x) { raw[dst + 1 + x * 3] = rgba[src + x * 4]; raw[dst + 2 + x * 3] = rgba[src + x * 4 + 1]; raw[dst + 3 + x * 3] = rgba[src + x * 4 + 2]; }
  }
  const header = Buffer.alloc(13); header.writeUInt32BE(width, 0); header.writeUInt32BE(height, 4); header[8] = 8; header[9] = 2;
  fs.writeFileSync(path.join(output, file.replace('.rgba', '.png')), Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]), chunk('IHDR', header), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
  fs.rmSync(path.join(output, file));
}
console.log('Captures: ' + output);
