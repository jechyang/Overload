// Native EXE is the runtime test launcher. dotnet is used only to build the DLL.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const args = process.argv.slice(2);
const s2 = args.includes('--s2');
const s3 = args.includes('--s3');
const s4 = args.includes('--s4');
const compat = args.includes('--compat-build');
const stage = s4 ? 'S4' : s3 ? 'S3' : s2 ? 'S2' : 'S1';
const baseline = s4 ? 'ef3ea8f' : s3 ? 'c3ae12d' : s2 ? 'a1c3a1a' : '80ef9de';
const python = args[args.indexOf('--python') + 1];
if (!args.includes('--python') || !path.isAbsolute(python)) throw new Error('Supply --python <absolute Python 3 executable>');
const evidenceRoot = process.env.OV_SCRIPTING_EVIDENCE_ROOT || path.join(root, 'Build/Tests/Scripting');
if(!path.isAbsolute(evidenceRoot)) throw Error('OV_SCRIPTING_EVIDENCE_ROOT must be absolute');
fs.mkdirSync(evidenceRoot,{recursive:true});
const physicalOutput = fs.mkdtempSync(path.join(evidenceRoot, stage.toLowerCase() + '-'));
const output = path.join(root,'Build/Tests/Scripting',path.basename(physicalOutput));
if(path.resolve(output)!==path.resolve(physicalOutput)) fs.symlinkSync(physicalOutput,output,'junction');
console.log(stage + ' evidence: ' + output);
const read = p => JSON.parse(fs.readFileSync(p, 'utf8'));
const lock = read(path.join(root, 'Tools/OvCSharp/toolchain.lock.json'));
const dotnetRoot = path.join(root, 'Build/Toolchains/dotnet-' + lock.sdk_version);
const dotnet = path.join(dotnetRoot, 'dotnet.exe');
const env = { ...process.env, GIT_CONFIG_GLOBAL: 'NUL', DOTNET_ROOT: dotnetRoot, DOTNET_MULTILEVEL_LOOKUP: '0',
  DOTNET_CLI_HOME: path.join(output, 'cli'), NUGET_PACKAGES: path.join(output, 'packages'),
  DOTNET_CLI_TELEMETRY_OPTOUT: '1', DOTNET_NOLOGO: '1', PYTHONDONTWRITEBYTECODE: '1' };
for (const key of Object.keys(env)) if (/^(CscToolExe|CscToolPath|CscEnvironment|RoslynTargetsPath|RoslynCompilerType|SkipCompilerExecution|UseSharedCompilation|UseHostCompilerIfAvailable|MSBuildSDKsPath|CompilerToolPath)$/i.test(key)) delete env[key];
const checks = [], blockers = [];
const hash = p => crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
function filesBelow(directory) {
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap(e => {
    const p = path.join(directory, e.name);
    return e.isDirectory() ? (['bin', 'obj'].includes(e.name) ? [] : filesBelow(p)) : [p];
  });
}
const sourceFiles = [
  ...(s4 ? ['Sources/OvCore/src/OvCore/ECS/Components/BehaviourFields.cpp',
    'Sources/OvCore/src/OvCore/SceneSystem/PrefabOperations.cpp','Sources/OvEditor/src/OvEditor/Core/EditorActions.cpp'].map(p => path.join(root,p)) : []),
  ...(s3 ? [...filesBelow(path.join(root, 'Examples/CSharpGameplay')),
    ...['Sources/OvCore/src/OvCore/SceneSystem/Scene.cpp', 'Sources/OvCore/src/OvCore/SceneSystem/SceneManager.cpp',
    'Sources/OvCore/include/OvCore/SceneSystem/Scene.h', 'Sources/OvCore/include/OvCore/ECS/Actor.inl',
    'Sources/OvTools/include/OvTools/Utils/DispatchScope.h', 'Sources/OvPhysics/src/OvPhysics/Core/PhysicsEngine.cpp',
    'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp',
    'Sources/OvEditor/src/OvEditor/Core/Editor.cpp', 'Sources/OvGame/src/OvGame/Core/Game.cpp'].map(p => path.join(root, p))] : []),
  ...['Sources/OvCore/include/OvCore/Scripting','Sources/OvCore/src/OvCore/Scripting',
    'Sources/OvManaged/Overload.ManagedHost','Sources/OvManaged/Overload.ScriptCore','Tests/Scripting','Tools/OvCSharp'].flatMap(p => filesBelow(path.join(root, p))),
  ...['Scripts/Windows/BuildCSharp.mjs', 'Sources/OvCore/include/OvCore/ECS/Actor.h','Sources/OvCore/src/OvCore/ECS/Actor.cpp',
    'Sources/OvCore/include/OvCore/ECS/Components/Behaviour.h','Sources/OvCore/src/OvCore/ECS/Components/Behaviour.cpp',
    'Sources/OvEditor/src/OvEditor/Panels/Inspector.cpp',
    'Sources/OvEditor/include/OvEditor/Core/Context.h','Sources/OvEditor/src/OvEditor/Core/Context.cpp',
    'Sources/OvGame/include/OvGame/Core/Context.h','Sources/OvGame/src/OvGame/Core/Context.cpp',
    'Sources/OvReflection/Managed/ScriptBridge.cs','Tools/OvReflectGen/Emitters/script.py', 'global.json'].map(p => path.join(root, p))
].sort();
const receipt = () => Object.fromEntries(sourceFiles.map(p => [path.relative(root, p).replaceAll('\\','/'), hash(p)]));
const before = receipt();
const nativeSnapshot = () => Object.fromEntries(filesBelow(path.join(root, 'Bin-Int/Debug')).filter(p => p.endsWith('.obj'))
  .map(p => [path.relative(root, p), { hash: hash(p), mtime_ms: fs.statSync(p).mtimeMs }]));
function run(name, exe, argv, expect = 0, timeout = 300000, cwd = root) {
  console.log(name);
  const result = spawnSync(exe, argv, { cwd, env, encoding: 'utf8', windowsHide: true, timeout, maxBuffer: 64 * 1024 * 1024 });
  const text = (result.stdout || '') + (result.stderr || '') + (result.error ? String(result.error) : '');
  fs.writeFileSync(path.join(output, name + '.log'), text);
  checks.push({ name, cwd, argv: [exe, ...argv], exit_code: result.status, signal: result.signal, error: result.error?.message });
  if (expect !== null && result.status !== expect) throw new Error(`${name}: exit ${result.status}, expected ${expect}; see log`);
  return { text, status: result.status };
}
try {
  run('baseline', 'git', ['-c', 'safe.directory=H:/overload/Overload', 'merge-base', '--is-ancestor', baseline, 'HEAD']);
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
  const properties = ['-p:OutputPath=' + managed.replaceAll('\\','/') + '/', '-p:ReflectionConfiguration=Debug'];
  run('managed-restore', dotnet, ['restore', path.join(managedProject, 'Overload.ManagedHost.csproj'), '--locked-mode',
    '--configfile', path.join(managedProject, 'NuGet.Config'), ...properties]);
  const standard = run('dotnet-build', dotnet, ['build', path.join(managedProject, 'Overload.ManagedHost.csproj'), '--no-restore', '-t:Rebuild', '-c', 'Debug', ...properties], null);
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
  const core = path.join(managed, 'Overload.ScriptCore.dll');
  const coreOptions = options.filter(o => !o.startsWith('/out:') && !sources.some(s => JSON.stringify(s) === o));
  const production = path.join(output, 'production'); fs.mkdirSync(production);
  function compatBuild(name, projectFile, destination, define = '', artifacts = path.join(output, name + '-artifacts')) {
    run(name, process.execPath, [path.join(root, 'Scripts/Windows/BuildCSharp.mjs'), '--compat', projectFile,
      '--rebuild', '--artifacts-path', artifacts, '--output', destination, ...(define ? ['--define', define] : [])]);
  }
  if (compat) {
    const projectFile = path.join(managedProject, 'Overload.ManagedHost.csproj');
    compatBuild('compat-production-rebuild', projectFile, production);
    compatBuild('compat-acceptance-rebuild', projectFile, managed, 'S1_ACCEPTANCE');
    const firstHash = hash(dll);
    compatBuild('compat-acceptance-determinism', projectFile, managed, 'S1_ACCEPTANCE', path.join(output, 'compat-acceptance-rebuild-artifacts'));
    if (hash(dll) !== firstHash) throw new Error('Non-deterministic MSBuild rebuild');
  } else {
  fs.writeFileSync(path.join(output, 'core.rsp'), [...coreOptions, '/out:' + JSON.stringify(core),
    ...fs.readdirSync(path.join(root, 'Sources/OvManaged/Overload.ScriptCore')).filter(p => p.endsWith('.cs'))
      .map(p => JSON.stringify(path.join(root, 'Sources/OvManaged/Overload.ScriptCore', p)))].join('\n'));
  run('scriptcore-compile', dotnet, [csc, '@' + path.join(output, 'core.rsp')]);
  options.push('/reference:' + JSON.stringify(core));
  // Compile the production assembly separately, without acceptance-only entries.
  fs.writeFileSync(rsp, options.join('\n'));
  run('managed-production-compile', dotnet, [csc, '@' + rsp]);
  fs.copyFileSync(core, path.join(production, 'Overload.ScriptCore.dll'));
  fs.copyFileSync(dll, path.join(production, 'Overload.ManagedHost.dll'));
  fs.copyFileSync(path.join(managedProject, 'Overload.ManagedHost.runtimeconfig.json'), path.join(production, 'Overload.ManagedHost.runtimeconfig.json'));
  fs.writeFileSync(rsp, [...options, '/define:S1_ACCEPTANCE'].join('\n'));
  run('managed-acceptance-compile', dotnet, [csc, '@' + rsp]);
  const firstHash = hash(dll);
  run('managed-determinism', dotnet, [csc, '@' + rsp]);
  if (hash(dll) !== firstHash) throw new Error('Non-deterministic managed build');
  fs.copyFileSync(path.join(managedProject, 'Overload.ManagedHost.runtimeconfig.json'), path.join(managed, 'Overload.ManagedHost.runtimeconfig.json'));
  }
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
  if (s2) {
    const project = path.join(output, 'project 空格'); fs.mkdirSync(project);
    fs.copyFileSync(path.join(root, 'Tests/Scripting/Fixtures/S2/legacy.lua'), path.join(project, 'legacy.lua'));
    fs.copyFileSync(path.join(root, 'Tests/Scripting/Fixtures/S2/legacy.lua'), path.join(project, 'First.Same'));
    const moved = path.join(project, 'moved'); fs.mkdirSync(moved);
    fs.copyFileSync(path.join(root, 'Tests/Scripting/Fixtures/S2/Types.cs'), path.join(moved, 'UnrelatedFilename.cs'));
    // Generated test projects keep MSBuild reference resolution and artifact management active.
    function buildFixture(name, source, define, dependency = '') {
      const directory = path.join(output, 'fixture-projects', name); fs.mkdirSync(directory, { recursive: true });
      const xml = value => value.replaceAll('&', '&amp;').replaceAll('"', '&quot;').replaceAll('<', '&lt;');
      const coreProject = path.join(root, 'Sources/OvManaged/Overload.ScriptCore/Overload.ScriptCore.csproj');
      const projectFile = path.join(directory, name + '.csproj');
      fs.writeFileSync(projectFile, '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework><PlatformTarget>x64</PlatformTarget><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="' + xml(source) + '"/><ProjectReference Include="' + xml(coreProject) + '"/>' +
        (name === 'S3' ? '<Compile Include="' + xml(path.join(root, 'Examples/CSharpGameplay/MoveAndResize.cs')) + '"/>' : '') +
        (dependency ? '<ProjectReference Include="../' + dependency + '/' + dependency + '.csproj"/>' : '') + '</ItemGroup></Project>');
      fs.copyFileSync(path.join(managedProject, 'NuGet.Config'), path.join(directory, 'NuGet.Config'));
      // All references here are projects, with no package dependencies.
      const dependencies = { 'Overload.ScriptCore': { type: 'Project' } };
      if (dependency) dependencies[dependency] = { type: 'Project', dependencies: { 'Overload.ScriptCore': '[1.0.0, )' } };
      fs.writeFileSync(path.join(directory, 'packages.lock.json'), JSON.stringify({ version: 1, dependencies: { 'net10.0': dependencies } }));
      compatBuild('compat-project-' + name, projectFile, project, define);
    }
    // Real compilations of a moved source and renamed class, all from the pinned SDK.
    for (const [variant, define] of [['original',''], ['moved',''], ['renamed','RENAMED'],
      ['duplicate-id','DUPLICATE_ID'], ['duplicate-alias','DUPLICATE_ALIAS'], ['alias-primary','ALIAS_PRIMARY'], ['empty-id','EMPTY_ID']]) {
      const source = variant === 'moved' ? path.join(moved, 'UnrelatedFilename.cs') : path.join(root, 'Tests/Scripting/Fixtures/S2/Types.cs');
      if (compat) {
        buildFixture(variant, source, define);
      } else {
      const projectOptions = [...coreOptions, '/reference:' + JSON.stringify(core), '/out:' + JSON.stringify(path.join(project, variant + '.dll')),
        ...(define ? ['/define:' + define] : []), JSON.stringify(source)];
      const response = path.join(output, variant + '.rsp'); fs.writeFileSync(response, projectOptions.join('\n'));
      run('project-compile-' + variant, dotnet, [csc, '@' + response]);
      }
    }
    // A project-local copy cannot replace the default-context shared ScriptCore.
    fs.copyFileSync(core, path.join(project, 'Overload.ScriptCore.dll'));
    for (const [name, source] of [['Dependency','Dependency.cs'], ['Dependent','Dependent.cs']]) {
      if (compat) {
        buildFixture(name, path.join(root, 'Tests/Scripting/Fixtures/S2', source), '', name === 'Dependent' ? 'Dependency' : '');
      } else {
      const response = path.join(output, name + '.rsp');
      fs.writeFileSync(response, [...coreOptions, '/reference:' + JSON.stringify(core),
        ...(name === 'Dependent' ? ['/reference:' + JSON.stringify(path.join(project, 'Dependency.dll'))] : []),
        '/out:' + JSON.stringify(path.join(project, name + '.dll')),
        JSON.stringify(path.join(root, 'Tests/Scripting/Fixtures/S2', source))].join('\n'));
      run('project-compile-' + name, dotnet, [csc, '@' + response]);
      }
    }
    fs.writeFileSync(path.join(project, 'corrupt.dll'), 'invalid managed assembly');
    const s2obj = path.join(output, 's2.obj'), s2exe = path.join(output, 's2.exe');
    run('s2-compile', path.join(compiler, 'cl.exe'), ['/nologo','/std:c++20','/EHsc','/MDd','/W4','/WX','/c',
      '/I' + gen, '/I' + path.join(root, 'Dependencies/glfw/include'), '/I' + path.join(root, 'Dependencies/bullet3/src'),
      ...nativeOptions, path.join(root, 'Tests/Scripting/Integration/native_s2_validation.cpp'), '/Fo' + s2obj]);
    run('s2-link', path.join(compiler, 'link.exe'), ['/nologo','/OPT:REF','/OPT:ICF','/OUT:' + s2exe, s2obj, ...libraries,
      ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(s => s + '.lib')]);
    for (const mode of ['default', 'render-thread']) run('s2-' + mode, s2exe, [dotnetRoot, production, project, mode], 0, 120000);
    // The facade has a backend-independent pimpl layout. Link its real Null
    // implementation in place of the Lua object, with the same Actor/Behaviour library.
    const nullOptions = nativeOptions.filter(o => !o.startsWith('/DLUA_SCRIPTING'));
    const nullObjects = [];
    for (const [name, source] of [['null-facade', 'Sources/OvCore/src/OvCore/Scripting/ScriptEngine.cpp'],
      ['null-test', 'Tests/Scripting/Integration/native_s2_null_validation.cpp']]) {
      const object = path.join(output, name + '.obj'); nullObjects.push(object);
      run(name + '-compile', path.join(compiler, 'cl.exe'), ['/nologo','/std:c++20','/EHsc','/MDd','/W4','/WX','/wd4100','/wd4201','/c',
        '/I' + gen, '/I' + path.join(root, 'Dependencies/glfw/include'), '/I' + path.join(root, 'Dependencies/bullet3/src'),
        ...nullOptions, '/ULUA_SCRIPTING', path.join(root, source), '/Fo' + object]);
    }
    const nullExe = path.join(output, 's2-null.exe');
    run('null-link', path.join(compiler, 'link.exe'), ['/nologo','/OPT:REF','/OPT:ICF','/OUT:' + nullExe, ...nullObjects, ...libraries,
      ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(s => s + '.lib')]);
    run('s2-null', nullExe, [dotnetRoot, production, project, 'null'], 0, 120000);
    if (s3) {
      compatBuild('compat-example-rebuild', path.join(root, 'Examples/CSharpGameplay/GameScripts.csproj'), path.join(output, 'example'));
      buildFixture('S3', path.join(root, 'Tests/Scripting/Fixtures/S3/Types.cs'), '');
      fs.copyFileSync(path.join(root, 'Tests/Scripting/Fixtures/S3/legacy.lua'), path.join(project, 's3.lua'));
      const object = path.join(output, 's3.obj'), executable = path.join(output, 's3.exe');
      run('s3-compile', path.join(compiler, 'cl.exe'), ['/nologo','/std:c++20','/EHsc','/MDd','/W4','/WX','/c',
        '/I' + gen, '/I' + path.join(root, 'Dependencies/glfw/include'), '/I' + path.join(root, 'Dependencies/bullet3/src'),
        ...nativeOptions, path.join(root, 'Tests/Scripting/Integration/native_s3_validation.cpp'), '/Fo' + object]);
      run('s3-link', path.join(compiler, 'link.exe'), ['/nologo','/OPT:REF','/OPT:ICF','/OUT:' + executable, object, ...libraries,
        ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(s => s + '.lib')]);
      for (const mode of ['default', 'render-thread']) {
        env.OV_S3_TRACE = path.join(output, 's3-' + mode + '-events.txt');
        env.OV_S3_SCENE = path.join(output, 's3-next.ovscene');
        run('s3-' + mode, executable, [dotnetRoot, production, project, mode], 0, 120000);
      }
      env.OV_S3_TRACE = path.join(output, 's3-closed-facades-events.txt');
      run('s3-closed-facades', executable, [dotnetRoot, managed, project, 'closed-facades'], 0, 120000);
      const { verifyApps } = await import('./verify_s3_apps.mjs');
      verifyApps({ root, output, run, env, compiler, nativeOptions, libraries, gen, dotnetRoot, production, project });
      if(s4) {
        for(const [name,define] of [['S4',''],['S4Renamed','RENAMED'],['S4Duplicate','DUPLICATE_FIELD']])
          buildFixture(name,path.join(root,'Tests/Scripting/Fixtures/S4/Types.cs'),define);
        const { verifyFields } = await import('./verify_s4_cases.mjs');
        verifyFields({ root, output, run, env, compiler, nativeOptions, libraries, gen, dotnetRoot, production, project });
      }
    }
  }
  const objectsBefore = nativeSnapshot();
  run('native-no-change-build', msbuild, buildArgs, 0, 900000);
  if (JSON.stringify(objectsBefore) !== JSON.stringify(nativeSnapshot())) throw new Error('Unchanged native build recompiled objects');
  if (JSON.stringify(before) !== JSON.stringify(receipt())) throw new Error('Sources changed during acceptance; rerun fixed sources');
  fs.writeFileSync(path.join(output, 'verification.json'), JSON.stringify({ stage, success: true, timestamp_utc: new Date().toISOString(),
    os: os.release(), baseline, runtime_launcher: exe, standard_dotnet_build_passed: standard.status === 0, compatibility_msbuild_passed: compat ? true : null,
    s3_artifacts: s3 ? Object.fromEntries(['s3.exe', 'project 空格/S3.dll', 'example/GameScripts.dll', 'apps/OvEditor-s3.exe', 'apps/OvGame-s3.exe'].map(p => [p, hash(path.join(output, p))])) : undefined,
    s4_artifacts: s4 ? Object.fromEntries(['s4.exe','project 空格/S4.dll','project 空格/S4Renamed.dll','apps-s4/OvEditor-s4.exe','apps-s4/OvGame-s4.exe'].map(p=>[p,hash(path.join(output,p))])) : undefined,
    external_blockers: blockers, checks, source_receipt: before,
    generated_receipt: Object.fromEntries(['OvCore/Script.generated.h','OvCore/Script.generated.cs','OvCore/script-abi.json'].map(p => [p, hash(path.join(gen, p))])),
    artifacts: { native_exe_sha256: hash(exe), managed_sha256: hash(dll), production_managed_sha256: hash(path.join(production, 'Overload.ManagedHost.dll')) } }, null, 2));
  console.log(stage + ' native acceptance completed: ' + output);
} catch (error) {
  fs.writeFileSync(path.join(output, 'failure.json'), JSON.stringify({ error: String(error), checks, blockers }, null, 2));
  console.error(String(error)); process.exitCode = 1;
}
