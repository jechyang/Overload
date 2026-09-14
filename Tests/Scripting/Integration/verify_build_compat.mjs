// Build-toolchain acceptance only; native behavior is covered by verify_s2 --compat-build.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const parent = path.join(root, 'Build/Tests/Scripting'); fs.mkdirSync(parent, { recursive: true });
const output = fs.mkdtempSync(path.join(parent, 'msbuild-compat-'));
const copy = path.join(output, 'project 空格'); fs.mkdirSync(copy);
const lock = JSON.parse(fs.readFileSync(path.join(root, 'Tools/OvCSharp/toolchain.lock.json'), 'utf8'));
const dotnetRoot = path.join(root, 'Build/Toolchains/dotnet-' + lock.sdk_version);
const dotnet = path.join(dotnetRoot, 'dotnet.exe');
const env = { ...process.env, DOTNET_ROOT: dotnetRoot, DOTNET_HOST_PATH: dotnet, DOTNET_ROOT_X64: dotnetRoot,
  DOTNET_CLI_HOME: path.join(output, 'cli'), NUGET_PACKAGES: path.join(output, 'packages'),
  DOTNET_NOLOGO: '1', DOTNET_CLI_TELEMETRY_OPTOUT: '1', DOTNET_ADD_GLOBAL_TOOLS_TO_PATH: '0' };
for (const key of Object.keys(env)) if (/^(CscToolExe|CscToolPath|CscEnvironment|RoslynTargetsPath|RoslynCompilerType|SkipCompilerExecution|UseSharedCompilation|UseHostCompilerIfAvailable|MSBuildSDKsPath|CompilerToolPath)$/i.test(key)) delete env[key];
const hash = p => crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const assert = (test, message) => { if (!test) throw new Error(message); };
const receipt = { baseline: '3c9f9fa', success: false, checks: [], source_receipt: {}, standard_dotnet_build_passed: false, compatibility_msbuild_passed: false };
const save = () => fs.writeFileSync(path.join(output, 'verification.json'), JSON.stringify(receipt, null, 2));
function run(name, exe, argv, expected = 0) {
  const result = spawnSync(exe, argv, { cwd: root, env, windowsHide: true, timeout: 300000, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
  const text = (result.stdout || '') + (result.stderr || '') + (result.error ? String(result.error) : '');
  fs.writeFileSync(path.join(output, name + '.log'), text);
  receipt.checks.push({ name, argv: [exe, ...argv], exit_code: result.status, error: result.error?.message }); save();
  console.log(name + ': exit ' + result.status);
  assert(!result.error && (expected === null || result.status === expected), name + ' failed; see ' + output);
  return { status: result.status, text };
}
const wrapper = path.join(root, 'Scripts/Windows/BuildCSharp.mjs');
function compat(name, project, artifacts, destination, rebuild = true, expected = 0) {
  return run(name, process.execPath, [wrapper, '--compat', project, '--artifacts-path', artifacts,
    '--output', destination, ...(rebuild ? ['--rebuild'] : [])], expected);
}
try {
  console.log('Compatibility evidence: ' + output);
  for (const relative of ['global.json','Sources/OvManaged','Sources/OvReflection/Managed','Tests/Scripting/Fixtures/S0',
    'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug']) {
    fs.cpSync(path.join(root, relative), path.join(copy, relative), { recursive: true, filter: p => {
      if (/[\\/](bin|obj)([\\/]|$)/.test(p)) return false;
      if (fs.statSync(p).isFile()) receipt.source_receipt[path.relative(root, p.startsWith('\\\\?\\') ? p.slice(4) : p)] = hash(p);
      return true;
    } });
  }
  const sdkFiles = ['dotnet.exe','sdk/' + lock.sdk_version + '/Roslyn/bincore/csc.exe',
    'sdk/' + lock.sdk_version + '/Roslyn/bincore/csc.dll','sdk/' + lock.sdk_version + '/Roslyn/Microsoft.CSharp.Core.targets'];
  const sdkReceipt = () => Object.fromEntries(sdkFiles.map(p => [p, hash(path.join(dotnetRoot, p))]));
  receipt.sdk_before = sdkReceipt();
  const smoke = path.join(copy, 'Tests/Scripting/Fixtures/S0/Overload.ToolchainSmoke.csproj');
  const host = path.join(copy, 'Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj');
  const standards = [];
  for (const [name, project] of [['smoke',smoke],['host',host]]) {
    run(name + '-restore', dotnet, ['restore', project, '--locked-mode', '--configfile', path.join(path.dirname(project), 'NuGet.Config')]);
    const standard = run('standard-' + name + '-rebuild', dotnet, ['build', project, '--no-restore', '-t:Rebuild', '-v:normal', '-nr:false'], null);
    standards.push(standard.status === 0);
    assert(standard.status === 0 || standard.text.includes("Your Windows doesn't fully support CET"), 'Unexpected standard build failure');
  }
  receipt.standard_dotnet_build_passed = standards.every(Boolean);
  const smokeBin = path.join(output, 'smoke-bin'), smokeArtifacts = path.join(output, 'smoke-artifacts');
  compat('compat-smoke-rebuild', smoke, smokeArtifacts, smokeBin);
  run('compat-smoke-run', dotnet, [path.join(smokeBin, 'Overload.ToolchainSmoke.dll')]);
  const hostBin = path.join(output, 'host-bin'), hostArtifacts = path.join(output, 'host-artifacts');
  compat('compat-host-rebuild', host, hostArtifacts, hostBin);
  const dlls = ['Overload.ManagedHost.dll','Overload.ScriptCore.dll'];
  const snapshot = () => Object.fromEntries(dlls.map(p => [p, { sha256: hash(path.join(hostBin,p)), mtime: fs.statSync(path.join(hostBin,p)).mtimeMs }]));
  const first = snapshot();
  const noChange = compat('compat-host-incremental', host, hostArtifacts, hostBin, false);
  assert(JSON.stringify(first) === JSON.stringify(snapshot()), 'Incremental build changed DLL bytes/timestamps');
  assert(!noChange.text.includes('csc-dotnet.cmd /noconfig'), 'Incremental build recompiled');
  compat('compat-host-forced-rebuild', host, hostArtifacts, hostBin);
  const second = snapshot();
  for (const file of dlls) assert(first[file].sha256 === second[file].sha256 && first[file].mtime !== second[file].mtime, 'Rebuild must regenerate deterministic ' + file);
  const badSource = path.join(copy, 'Sources/OvManaged/Overload.ScriptCore/IntentionalError.cs');
  fs.writeFileSync(badSource, '#error OV_COMPAT_EXPECTED_COMPILER_FAILURE\n');
  const failed = compat('compat-reference-compile-error', host, hostArtifacts, hostBin, true, 1);
  assert(failed.text.includes('CS1029') && failed.text.includes('OV_COMPAT_EXPECTED_COMPILER_FAILURE'), 'Expected real C# diagnostic');
  fs.unlinkSync(badSource);
  compat('compat-recovery-rebuild', host, hostArtifacts, hostBin);
  const state = path.join(copy, 'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug/generation-state.json');
  fs.renameSync(state, state + '.held');
  const missing = compat('compat-missing-native-generation', host, hostArtifacts, hostBin, true, 1);
  assert(missing.text.includes('Build the matching native configuration'), 'Native generation guard bypassed');
  fs.renameSync(state + '.held', state);
  compat('compat-final-rebuild', host, hostArtifacts, hostBin);
  // Reversibility: original command still follows SDK defaults after successful compatibility builds.
  const reverted = run('standard-after-compat', dotnet, ['build', host, '--no-restore', '-t:Rebuild', '-v:normal', '-nr:false'], null);
  assert(reverted.status === 0 || reverted.text.includes("Your Windows doesn't fully support CET"), 'Unexpected default build result');
  receipt.artifacts = snapshot();
  for (const file of ['Overload.ManagedHost.deps.json','Overload.ManagedHost.runtimeconfig.json','Overload.ScriptCore.pdb']) assert(fs.existsSync(path.join(hostBin,file)), 'Missing MSBuild artifact: ' + file);
  receipt.sdk_after = sdkReceipt();
  assert(JSON.stringify(receipt.sdk_before) === JSON.stringify(receipt.sdk_after), 'SDK files changed');
  for (const [p, value] of Object.entries(receipt.source_receipt)) assert(hash(path.join(root,p)) === value, 'Original source changed: ' + p);
  receipt.compatibility_msbuild_passed = true; receipt.success = true;
} catch (error) { receipt.error = String(error); console.error(error); process.exitCode = 1; }
finally { save(); console.log(path.join(output, 'verification.json')); }
