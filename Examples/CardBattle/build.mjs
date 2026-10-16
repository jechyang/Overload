import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {resolvePython} from '../../Scripts/Windows/CSharpBuildTools.mjs';

const project = path.dirname(fileURLToPath(import.meta.url)), root = path.resolve(project, '../..');
const userProfile = process.env.USERPROFILE || 'C:\\Users\\Administrator';
Object.assign(process.env, {
  USERPROFILE: userProfile,
  USERNAME: process.env.USERNAME === 'SYSTEM' ? 'Administrator' : (process.env.USERNAME || 'Administrator'),
  APPDATA: process.env.APPDATA || path.join(userProfile, 'AppData', 'Roaming'),
  LOCALAPPDATA: process.env.LOCALAPPDATA || path.join(userProfile, 'AppData', 'Local'),
  HOMEDRIVE: process.env.HOMEDRIVE || 'C:',
  HOMEPATH: process.env.HOMEPATH || '\\Users\\Administrator',
  PROGRAMDATA: process.env.PROGRAMDATA || 'C:\\ProgramData',
  ProgramFiles: process.env.ProgramFiles || 'C:\\Program Files',
  'ProgramFiles(x86)': process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)',
  CommonProgramFiles: process.env.CommonProgramFiles || 'C:\\Program Files\\Common Files',
  CommonProgramW6432: process.env.CommonProgramW6432 || 'C:\\Program Files\\Common Files',
  SystemRoot: process.env.SystemRoot || 'C:\\Windows',
  SYSTEMDRIVE: process.env.SYSTEMDRIVE || 'C:',
});

const tempDir = path.join(project, '.overload/temp');
fs.mkdirSync(tempDir, {recursive: true});
fs.mkdirSync(path.join(project, '.overload/managed-shadow'), {recursive: true});
process.env.TEMP = tempDir; process.env.TMP = tempDir;
process.env.OV_SCRIPTING_SHADOW_ROOT = path.join(project, '.overload/managed-shadow');

const localProject = path.join(project, 'CardBattle.local.ovproject');
const savedPython = fs.existsSync(localProject) ? fs.readFileSync(localProject, 'utf8').match(/^csharp_python=(.+)$/m)?.[1].trim() : undefined;
const python = resolvePython(root, process.argv[2] || process.env.OV_REFLECTION_PYTHON || savedPython);

const cache = path.join(project, '.overload/csharp');
fs.mkdirSync(cache, {recursive: true});
const result = path.join(cache, 'build.xml'), log = path.join(cache, 'build.log'), fd = fs.openSync(log, 'w');

const build = cp.spawnSync(process.execPath, [
    path.join(root, 'Scripts/Windows/BuildGameScripts.mjs'),
    '--project', project,
    '--cache', cache,
    '--python', python,
    '--configuration', 'Debug',
    '--result', result
], {cwd: root, windowsHide: true, stdio: ['ignore', fd, fd], timeout: 300000, env: process.env});

fs.closeSync(fd);
if (build.status !== 0) throw Error('CardBattle build failed: ' + log + ' ' + (build.error || ''));

const receipt = fs.readFileSync(result, 'utf8');
const escaped = receipt.match(/generation="([^"]+)"/)?.[1];
if (!escaped) throw Error('Build receipt has no generation');
const generation = escaped.replaceAll('&quot;', '"').replaceAll('&amp;', '&').replaceAll('&lt;', '<').replaceAll('&gt;', '>');

const lock = JSON.parse(fs.readFileSync(path.join(root, 'Tools/OvCSharp/toolchain.lock.json'), 'utf8'));
const settings = fs.readFileSync(path.join(project, 'CardBattle.ovproject'), 'utf8') +
    '\ncsharp_engine_root=' + root.replaceAll('\\', '/') +
    '\ncsharp_node=' + process.execPath.replaceAll('\\', '/') +
    '\ncsharp_python=' + python.replaceAll('\\', '/') +
    '\ncsharp_runtime_root=' + path.join(root, 'Build/Toolchains/dotnet-' + lock.sdk_version).replaceAll('\\', '/') +
    '\ncsharp_managed_root=' + path.join(generation, 'host').replaceAll('\\', '/') +
    '\ncsharp_project_assembly=' + path.join(generation, 'GameScripts.dll').replaceAll('\\', '/') + '\n';

const output = path.join(project, 'CardBattle.local.ovproject');
fs.writeFileSync(output, settings);
console.log('Built CardBattle successfully!');
console.log('Open ' + output + ' in the editor, then open Assets/Scenes/CardBattle.ovscene and Play.');
