// Explicit, process-scoped MSBuild compatibility entry. Never edits the SDK.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const help = 'node Scripts/Windows/BuildCSharp.mjs --compat <project.csproj> [--configuration Debug|Release|Publish] [--publish <directory>] [--rebuild] [--artifacts-path <directory>] [--output <directory>] [--define <symbol>]';
try {
  const args = process.argv.slice(2);
  if (args.length === 1 && args[0] === '--help') { console.log(help); process.exit(0); }
  if (process.platform !== 'win32' || process.arch !== 'x64') throw new Error('Requires Windows x64 Node.js.');
  if (args.shift() !== '--compat') throw new Error('Compatibility mode must be explicitly enabled. ' + help);
  const project = path.resolve(args.shift() || '');
  if (!project.endsWith('.csproj') || !fs.existsSync(project)) throw new Error('Supply an existing .csproj. ' + help);
  let configuration = 'Debug', artifacts = path.join(root, 'Build/CSharpCompat/artifacts'), output, define, publish, rebuild = false;
  while (args.length) {
    const arg = args.shift();
    if (arg === '--rebuild') { rebuild = true; continue; }
    if (!['--configuration','--artifacts-path','--output','--define','--publish'].includes(arg) || !args.length) throw new Error('Invalid option: ' + arg);
    const value = args.shift();
    if (arg === '--configuration') configuration = value;
    if (arg === '--artifacts-path') artifacts = path.resolve(value);
    if (arg === '--output') output = path.resolve(value);
    if (arg === '--define') define = value;
    if (arg === '--publish') publish = path.resolve(value);
  }
  if (!['Debug','Release','Publish'].includes(configuration)) throw new Error('Configuration must be Debug, Release or Publish.');
  if (define && !/^[A-Za-z_][A-Za-z0-9_]*$/.test(define)) throw new Error('--define accepts one compiler symbol.');
  const lock = JSON.parse(fs.readFileSync(path.join(root, 'Tools/OvCSharp/toolchain.lock.json'), 'utf8'));
  const pin = JSON.parse(fs.readFileSync(path.join(root, 'global.json'), 'utf8')).sdk;
  if (pin.version !== lock.sdk_version || pin.rollForward !== 'disable') throw new Error('SDK lock/global.json mismatch.');
  const dotnetRoot = path.join(root, 'Build/Toolchains/dotnet-' + lock.sdk_version);
  const dotnet = path.join(dotnetRoot, 'dotnet.exe');
  const compiler = path.join(dotnetRoot, 'sdk', lock.sdk_version, 'Roslyn/bincore/csc.dll');
  for (const file of [dotnet, compiler]) if (!fs.existsSync(file)) throw new Error('Missing ' + file + '; run InstallCSharpToolchain.mjs.');
  const config = path.join(path.dirname(project), 'NuGet.Config');
  if (!fs.existsSync(config)) throw new Error('Project must provide NuGet.Config for explicit locked restore: ' + config);
  const logRoot = path.join(root, 'Build/CSharpCompat/logs'); fs.mkdirSync(logRoot, { recursive: true });
  const logs = fs.mkdtempSync(path.join(logRoot, 'build-'));
  const env = { ...process.env };
  const overrides = /^(CscToolExe|CscToolPath|CscEnvironment|RoslynTargetsPath|RoslynCompilerType|SkipCompilerExecution|UseSharedCompilation|UseHostCompilerIfAvailable|MSBuildSDKsPath|CompilerToolPath)$/i;
  for (const key of Object.keys(env)) if (overrides.test(key)) delete env[key];
  Object.assign(env, { DOTNET_ROOT: dotnetRoot, DOTNET_ROOT_X64: dotnetRoot, DOTNET_HOST_PATH: dotnet,
    DOTNET_MULTILEVEL_LOOKUP: '0', DOTNET_CLI_HOME: path.join(root, 'Build/CSharpCompat/cli'),
    NUGET_PACKAGES: path.join(root, 'Build/CSharpCompat/packages'), DOTNET_CLI_TELEMETRY_OPTOUT: '1',
    DOTNET_NOLOGO: '1', DOTNET_ADD_GLOBAL_TOOLS_TO_PATH: '0',
    OV_CSC_DOTNET: dotnet, OV_CSC_DLL: compiler });
  const receipt = { mode: 'explicit-msbuild-csc-dll', sdk: lock.sdk_version, runtime: lock.runtime_version, project, checks: [] };
  function run(name, argv) {
    const result = spawnSync(dotnet, argv, { cwd: root, env, windowsHide: true, encoding: 'utf8', timeout: 300000, maxBuffer: 64 * 1024 * 1024 });
    const text = (result.stdout || '') + (result.stderr || '') + (result.error ? String(result.error) : '');
    process.stdout.write(text);
    fs.writeFileSync(path.join(logs, name + '.log'), text);
    receipt.checks.push({ name, argv: [dotnet, ...argv], exit_code: result.status, signal: result.signal, error: result.error?.message });
    fs.writeFileSync(path.join(logs, 'verification.json'), JSON.stringify(receipt, null, 2));
    if (result.error || result.status !== 0) process.exit(result.status > 0 && result.status < 256 ? result.status : 1);
    return text;
  }
  console.log('Explicit compatibility build; evidence: ' + logs);
  if (run('sdk-version', ['--version']).trim() !== lock.sdk_version) throw new Error('Selected SDK differs from lock.');
  const properties = ['--artifacts-path', artifacts, '-p:Configuration=' + configuration, '-p:ReflectionConfiguration=' + configuration,
    '-p:UseSharedCompilation=false', '-p:UseHostCompilerIfAvailable=false', '-p:SkipCompilerExecution=false',
    '-p:CscToolPath=' + path.join(root, 'Tools/OvCSharp'), '-p:CscToolExe=csc-dotnet.cmd',
    '-p:CustomAfterMicrosoftCommonTargets=' + path.join(root, 'Sources/OvManaged/Serialization.targets')];
  if (output) properties.push('-p:OutputPath=' + output.replaceAll('\\', '/') + '/');
  if (configuration === 'Publish') properties.push('-p:Optimize=true', '-p:DebugType=portable');
  if (define) properties.push('-p:DefineConstants=' + define);
  run('restore', ['restore', project, '--locked-mode', '--configfile', config, ...properties]);
  run('compat-build', ['build', project, '--no-restore', ...(rebuild ? ['-t:Rebuild'] : []), '-v:normal', '-nr:false', ...properties]);
  if (publish) run('compat-publish', ['publish', project, '--no-restore', '--no-build', '-v:normal', '-nr:false', ...properties, '-p:PublishDir=' + publish.replaceAll('\\', '/') + '/']);
} catch (error) { console.error(String(error)); process.exitCode = 1; }
