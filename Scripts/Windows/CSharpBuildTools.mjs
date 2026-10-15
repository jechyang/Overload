import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

// The pinned private SDK, driven the same way as BuildCSharp.mjs: Roslyn runs as `dotnet csc.dll`
// (the SDK csc.exe apphost fails CET checks on some Windows builds), without a shared compiler server.
export function dotnetToolchain(root, configuration) {
  const lock = JSON.parse(fs.readFileSync(path.join(root, 'Tools/OvCSharp/toolchain.lock.json'), 'utf8'));
  const pin = JSON.parse(fs.readFileSync(path.join(root, 'global.json'), 'utf8')).sdk;
  if (pin.version !== lock.sdk_version || pin.rollForward !== 'disable') throw Error('SDK lock/global.json mismatch.');
  const sdk = path.join(root, 'Build/Toolchains/dotnet-' + lock.sdk_version);
  const dotnet = path.join(sdk, 'dotnet.exe');
  const compiler = path.join(sdk, 'sdk', lock.sdk_version, 'Roslyn/bincore/csc.dll');
  for (const file of [dotnet, compiler]) if (!fs.existsSync(file)) throw Error('Missing ' + file + '; run InstallCSharpToolchain.mjs.');
  const env = { ...process.env };
  const overrides = /^(CscToolExe|CscToolPath|CscEnvironment|RoslynTargetsPath|RoslynCompilerType|SkipCompilerExecution|UseSharedCompilation|UseHostCompilerIfAvailable|MSBuildSDKsPath|CompilerToolPath)$/i;
  for (const key of Object.keys(env)) if (overrides.test(key)) delete env[key];
  Object.assign(env, { DOTNET_ROOT: sdk, DOTNET_ROOT_X64: sdk, DOTNET_HOST_PATH: dotnet,
    DOTNET_MULTILEVEL_LOOKUP: '0', DOTNET_CLI_HOME: path.join(root, 'Build/CSharpCompat/cli'),
    NUGET_PACKAGES: path.join(root, 'Build/CSharpCompat/packages'), DOTNET_CLI_TELEMETRY_OPTOUT: '1',
    DOTNET_NOLOGO: '1', DOTNET_ADD_GLOBAL_TOOLS_TO_PATH: '0', OV_CSC_DOTNET: dotnet, OV_CSC_DLL: compiler,
    // English diagnostics: the editor Console font covers Latin only.
    DOTNET_CLI_UI_LANGUAGE: 'en-US' });
  const properties = ['-p:Configuration=' + configuration, '-p:ReflectionConfiguration=' + configuration,
    '-p:UseSharedCompilation=false', '-p:UseHostCompilerIfAvailable=false', '-p:SkipCompilerExecution=false',
    '-p:CscToolPath=' + path.join(root, 'Tools/OvCSharp'), '-p:CscToolExe=csc-dotnet.cmd',
    '-p:CustomAfterMicrosoftCommonTargets=' + path.join(root, 'Sources/OvManaged/Serialization.targets')];
  // Compiler diagnostics stay one per line (file(line,col): error CSxxxx: ...) for the editor Console.
  const run = (argv) => {
    const result = spawnSync(dotnet, [...argv, '-nologo'], { cwd: root, env, windowsHide: true, encoding: 'utf8', timeout: 600000, maxBuffer: 64 * 1024 * 1024 });
    const text = (result.stdout || '') + (result.stderr || '') + (result.error ? String(result.error) : '');
    process.stdout.write(text);
    return { ok: !result.error && result.status === 0, text };
  };
  return { sdkVersion: lock.sdk_version, runtimeVersion: lock.runtime_version, properties, run };
}

// Probe in the build worker, never on the editor Main thread. No shell or PATH changes.
export function resolvePython(root, explicit, env = process.env) {
  const candidates = [];
  const add = (exe, args = []) => { if (exe) candidates.push({exe,args}); };
  if (explicit) {
    if (!path.isAbsolute(explicit)) throw Error('csharp_python/--python must be an absolute Python 3.10+ executable path');
    add(explicit);
  } else {
    add(env.OV_REFLECTION_PYTHON);
    add(path.join(root,'Build/Toolchains/python/python.exe'));
    // Reuse the tool selected for the native reflection build, when available.
    const project = path.join(root,'Tools/OvReflectGen/Projects/OvReflectionGen.vcxproj');
    if (fs.existsSync(project)) {
      const text = fs.readFileSync(project,'utf8');
      const command = text.match(/<NMakeBuildCommandLine>([^<]+)<\/NMakeBuildCommandLine>/)?.[1]
        .replaceAll('&quot;','"').replaceAll('&amp;','&');
      const exe = command?.match(/^\s*"([^"]+)"\s+"[^"]*reflection\.py"/)?.[1];
      if (exe && path.isAbsolute(exe)) add(exe);
    }
    add('py.exe',['-3']); add('python3.exe'); add('python.exe');
  }
  const attempted = [], seen = new Set();
  for (const {exe,args} of candidates) {
    const key = JSON.stringify([exe,args]); if (seen.has(key)) continue; seen.add(key);
    const result = spawnSync(exe,[...args,'-c','import sys,json; print(json.dumps([sys.executable,list(sys.version_info[:3])]))'],
      {env, windowsHide:true, encoding:'utf8', timeout:5000, maxBuffer:65536});
    try {
      if (result.error || result.status !== 0) throw Error(result.error?.code || 'exit '+result.status);
      const [resolved,version] = JSON.parse(result.stdout.trim());
      if (!path.isAbsolute(resolved) || !fs.statSync(resolved).isFile()) throw Error('invalid executable');
      if (version[0] !== 3 || version[1] < 10) throw Error('Python '+version.join('.')+'; requires 3.10+');
      console.log('Reflection Python: '+resolved+' ('+version.join('.')+')');
      return resolved;
    } catch (error) { attempted.push(exe+': '+error.message); }
  }
  throw Error('No usable Python 3.10+ for C# reflection. Set csharp_python in the .ovproject file to an absolute Python 3 executable, then retry Build.\n'+attempted.join('\n'));
}
