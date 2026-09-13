// Native EXE is the runtime test launcher. dotnet is used only to build the DLL.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const args = process.argv.slice(2);
const python = args[args.indexOf('--python') + 1];
if (!args.includes('--python') || !path.isAbsolute(python)) throw new Error('Supply --python <absolute Python 3 executable>');
const output = fs.mkdtempSync(path.join(root, 'Build/Tests/Scripting/s1-'));
console.log('S1 evidence: ' + output);
const read = p => JSON.parse(fs.readFileSync(p, 'utf8'));
const lock = read(path.join(root, 'Tools/OvCSharp/toolchain.lock.json'));
const dotnetRoot = path.join(root, 'Build/Toolchains/dotnet-' + lock.sdk_version);
const dotnet = path.join(dotnetRoot, 'dotnet.exe');
const env = { ...process.env, GIT_CONFIG_GLOBAL: 'NUL', DOTNET_ROOT: dotnetRoot, DOTNET_MULTILEVEL_LOOKUP: '0',
  DOTNET_CLI_HOME: path.join(output, 'cli'), NUGET_PACKAGES: path.join(output, 'packages'),
  DOTNET_CLI_TELEMETRY_OPTOUT: '1', DOTNET_NOLOGO: '1', PYTHONDONTWRITEBYTECODE: '1' };
const checks = [], blockers = [];
const hash = p => crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
function filesBelow(directory) {
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap(e => {
    const p = path.join(directory, e.name);
    return e.isDirectory() ? (['bin', 'obj'].includes(e.name) ? [] : filesBelow(p)) : [p];
  });
}
const sourceFiles = [
  ...['Sources/OvCore/include/OvCore/Scripting/CSharp','Sources/OvCore/src/OvCore/Scripting/CSharp',
    'Sources/OvManaged/Overload.ManagedHost','Tests/Scripting','Tools/OvCSharp'].flatMap(p => filesBelow(path.join(root, p))),
  ...['Sources/OvEditor/include/OvEditor/Core/Context.h','Sources/OvEditor/src/OvEditor/Core/Context.cpp',
    'Sources/OvGame/include/OvGame/Core/Context.h','Sources/OvGame/src/OvGame/Core/Context.cpp',
    'Sources/OvReflection/Managed/ScriptBridge.cs','Tools/OvReflectGen/Emitters/script.py', 'global.json'].map(p => path.join(root, p))
].sort();
const receipt = () => Object.fromEntries(sourceFiles.map(p => [path.relative(root, p).replaceAll('\\','/'), hash(p)]));
const before = receipt();
const nativeSnapshot = () => Object.fromEntries(filesBelow(path.join(root, 'Bin-Int/Debug')).filter(p => p.endsWith('.obj'))
  .map(p => [path.relative(root, p), { hash: hash(p), mtime_ms: fs.statSync(p).mtimeMs }]));
function run(name, exe, argv, expect = 0, timeout = 300000) {
  console.log(name);
  const result = spawnSync(exe, argv, { cwd: root, env, encoding: 'utf8', windowsHide: true, timeout, maxBuffer: 64 * 1024 * 1024 });
  const text = (result.stdout || '') + (result.stderr || '') + (result.error ? String(result.error) : '');
  fs.writeFileSync(path.join(output, name + '.log'), text);
  checks.push({ name, argv: [exe, ...argv], exit_code: result.status, signal: result.signal, error: result.error?.message });
  if (expect !== null && result.status !== expect) throw new Error(`${name}: exit ${result.status}, expected ${expect}; see log`);
  return { text, status: result.status };
}
try {
  run('baseline', 'git', ['-c', 'safe.directory=H:/overload/Overload', 'merge-base', '--is-ancestor', '80ef9de', 'HEAD']);
  run('export', python, [path.join(root, 'Tools/OvReflectGen/reflection.py'), 'export', '--output', path.join(output, 'Export')]);
  const tc = read(path.join(output, 'Export/toolchain.json'));
  const compiler = path.join(tc.msvc_root, 'bin/Hostx64/x64');
  env.INCLUDE = tc.system_includes.join(';');
  env.LIB = [path.join(tc.msvc_root, 'lib/x64'), ...['ucrt', 'um'].map(p => path.join(tc.sdk_root, 'Lib', tc.sdk_version, p, 'x64'))].join(';');
  const pathKey = Object.keys(env).find(k => k.toLowerCase() === 'path');
  const prior = env[pathKey]; delete env[pathKey]; env.PATH = compiler + ';' + prior;
  run('premake', path.join(root, 'Dependencies/premake5/bin/premake5.exe'), ['--reflection-python=' + python, 'vs2022']);
  const msbuild = path.join(tc.vs_root, 'MSBuild/Current/Bin/amd64/MSBuild.exe');
  const buildArgs = [path.join(root, 'Overload.sln'), '/nologo', '/m:2', '/nr:false', '/v:minimal', '/t:Build',
    '/p:Configuration=Debug', '/p:Platform=x64', '/p:PreferredToolArchitecture=x64', '/p:PROCESSOR_ARCHITECTURE=AMD64',
    '/p:VcpkgEnabled=false', '/p:VcpkgApplocalDeps=false', '/p:VCToolsVersion=' + tc.msvc_version, '/p:WindowsTargetPlatformVersion=' + tc.sdk_version];
  run('native-build', msbuild, buildArgs, 0, 900000);
  const gen = path.join(root, 'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug');
  const managedProject = path.join(root, 'Sources/OvManaged/Overload.ManagedHost');
  const managed = path.join(output, 'managed 空格'); fs.mkdirSync(managed);
  const properties = ['-p:BaseIntermediateOutputPath=' + path.join(output, 'obj').replaceAll('\\','/') + '/',
    '-p:OutputPath=' + managed.replaceAll('\\','/') + '/', '-p:ReflectionConfiguration=Debug'];
  run('managed-restore', dotnet, ['restore', path.join(managedProject, 'Overload.ManagedHost.csproj'), '--locked-mode',
    '--configfile', path.join(managedProject, 'NuGet.Config'), ...properties]);
  const standard = run('dotnet-build', dotnet, ['build', path.join(managedProject, 'Overload.ManagedHost.csproj'), '--no-restore', '-c', 'Debug', ...properties], null);
  if (standard.status !== 0) {
    if (!standard.text.includes("Your Windows doesn't fully support CET")) throw new Error('Standard build failed for an unexpected reason');
    blockers.push({ check: 'dotnet-build', exit_code: standard.status, reason: 'OS CET prerequisite; standard build FAILED. SDK csc.dll fallback is separate evidence.' });
  }
  const dll = path.join(managed, 'Overload.ManagedHost.dll');
  const sources = fs.readdirSync(managedProject).filter(f => f.endsWith('.cs')).map(f => path.join(managedProject, f));
  sources.push(path.join(root, 'Sources/OvReflection/Managed/ScriptBridge.cs'), path.join(gen, 'OvCore/Script.generated.cs'));
  const refs = path.join(dotnetRoot, 'packs/Microsoft.NETCore.App.Ref', lock.runtime_version, 'ref', lock.target_framework);
  const rsp = path.join(output, 'managed.rsp');
  const options = ['/nologo','/nostdlib+','/unsafe+','/langversion:14.0','/nullable:enable','/warnaserror+',
    '/deterministic+','/platform:x64','/target:library','/optimize+', '/out:' + JSON.stringify(dll),
    ...fs.readdirSync(refs).filter(f => f.endsWith('.dll')).map(f => '/reference:' + JSON.stringify(path.join(refs, f))), ...sources.map(s => JSON.stringify(s))];
  const csc = path.join(dotnetRoot, 'sdk', lock.sdk_version, 'Roslyn/bincore/csc.dll');
  // Compile the production assembly separately, without acceptance-only entries.
  fs.writeFileSync(rsp, options.join('\n'));
  run('managed-production-compile', dotnet, [csc, '@' + rsp]);
  const production = path.join(output, 'production'); fs.mkdirSync(production);
  fs.copyFileSync(dll, path.join(production, 'Overload.ManagedHost.dll'));
  fs.copyFileSync(path.join(managedProject, 'Overload.ManagedHost.runtimeconfig.json'), path.join(production, 'Overload.ManagedHost.runtimeconfig.json'));
  fs.writeFileSync(rsp, [...options, '/define:S1_ACCEPTANCE'].join('\n'));
  run('managed-acceptance-compile', dotnet, [csc, '@' + rsp]);
  const firstHash = hash(dll);
  run('managed-determinism', dotnet, [csc, '@' + rsp]);
  if (hash(dll) !== firstHash) throw new Error('Non-deterministic managed build');
  fs.copyFileSync(path.join(managedProject, 'Overload.ManagedHost.runtimeconfig.json'), path.join(managed, 'Overload.ManagedHost.runtimeconfig.json'));
  const entry = read(path.join(output, 'Export/x86_64-pc-windows-msvc/Debug/OvCore/compile_commands.json'))[0];
  const nativeOptions = entry.arguments.filter(a => a.startsWith('/I') || a.startsWith('/D') || a.startsWith('/U')).filter(a => a !== '/DOV_REFLECTION_ANALYSIS=1');
  const obj = path.join(output, 'native-host.obj'), exe = path.join(output, 'native-host.exe');
  run('native-smoke-compile', path.join(compiler, 'cl.exe'), ['/nologo','/std:c++20','/EHsc','/MDd','/W4','/WX','/c',
    '/I' + gen, '/I' + path.join(root, 'Dependencies/glfw/include'), '/I' + path.join(root, 'Dependencies/bullet3/src'),
    ...nativeOptions, path.join(root, 'Tests/Scripting/Integration/native_host_validation.cpp'), '/Fo' + obj]);
  const libraries = ['OvReflection','OvAudio','OvCore','OvDebug','OvMaths','OvPhysics','OvRendering','OvTools','OvUI','OvWindowing',
    'assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'].map(p => path.join(root, 'Bin/Debug', p, p + '.lib'));
  run('native-smoke-link', path.join(compiler, 'link.exe'), ['/nologo','/OPT:REF','/OPT:ICF','/OUT:' + exe, obj, ...libraries,
    ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(s => s + '.lib')]);
  const fake = Buffer.alloc(0x86); fake.write('MZ'); fake.writeUInt32LE(0x80, 0x3c); fake.write('PE\0\0', 0x80); fake.writeUInt16LE(0x14c, 0x84);
  fs.writeFileSync(path.join(managed, 'fake-x86.dll'), fake);
  fs.writeFileSync(path.join(managed, 'Overload.ManagedHost.runtimeconfig.json.invalid'), '{ invalid runtime config');
  run('production-native-start', exe, [dotnetRoot, production, root, 'production'], 0, 60000);
  for (const mode of ['missing-config','missing-assembly','missing-nethost','wrong-architecture','wrong-assembly-architecture','invalid-config']) {
    const result = run(mode, exe, [dotnetRoot, managed, root, mode], 1, 60000);
    const diagnostic = mode.includes('architecture') ? 'x64 PE' : mode === 'invalid-config' ? 'hostfxr_initialize_for_runtime_config failed' : 'Missing absolute CSharp dependency:';
    if (!result.text.includes(diagnostic)) throw new Error('Missing actionable failure diagnostic: ' + mode);
  }
  for (const mode of ['default','render-thread']) {
    const result = run('smoke-' + mode, exe, [dotnetRoot, managed, root, mode], 0, 120000);
    if (!result.text.includes('same resident CLR reattaches')) throw new Error('Incomplete native smoke');
  }
  const objectsBefore = nativeSnapshot();
  run('native-no-change-build', msbuild, buildArgs, 0, 900000);
  if (JSON.stringify(objectsBefore) !== JSON.stringify(nativeSnapshot())) throw new Error('Unchanged native build recompiled objects');
  if (JSON.stringify(before) !== JSON.stringify(receipt())) throw new Error('Sources changed during acceptance; rerun fixed sources');
  fs.writeFileSync(path.join(output, 'verification.json'), JSON.stringify({ stage: 'S1', success: true, timestamp_utc: new Date().toISOString(),
    os: os.release(), baseline: '80ef9de', runtime_launcher: exe, standard_dotnet_build_passed: standard.status === 0,
    external_blockers: blockers, checks, source_receipt: before,
    generated_receipt: Object.fromEntries(['OvCore/Script.generated.h','OvCore/Script.generated.cs','OvCore/script-abi.json'].map(p => [p, hash(path.join(gen, p))])),
    artifacts: { native_exe_sha256: hash(exe), managed_sha256: hash(dll), production_managed_sha256: hash(path.join(production, 'Overload.ManagedHost.dll')) } }, null, 2));
  console.log('S1 native acceptance completed: ' + output);
} catch (error) {
  fs.writeFileSync(path.join(output, 'failure.json'), JSON.stringify({ error: String(error), checks, blockers }, null, 2));
  console.error(String(error)); process.exitCode = 1;
}
