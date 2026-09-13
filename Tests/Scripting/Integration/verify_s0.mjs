#!/usr/bin/env node
// S0 acceptance: locked .NET 10 x64 SDK, Host ABI contract, build and run smoke.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(scriptDirectory, '..', '..', '..');
const lockPath = path.join(root, 'Tools', 'OvCSharp', 'toolchain.lock.json');
const abiPath = path.join(root, 'Tools', 'OvCSharp', 'Schema', 'host-abi.json');
const abiHeader = path.join(root, 'Sources', 'OvCore', 'include', 'OvCore', 'Scripting', 'CSharp', 'HostABI.h');
const runtimeconfig = path.join(root, 'Sources', 'OvManaged', 'Overload.ManagedHost', 'Overload.ManagedHost.runtimeconfig.json');
const fixture = path.join(root, 'Tests', 'Scripting', 'Fixtures', 'S0');
const machineX64 = 0x8664;

function fail(message) {
  throw new Error(message);
}

function require(condition, message) {
  if (!condition) fail(message);
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function hashFile(file, algorithm = 'sha256') {
  const hash = crypto.createHash(algorithm);
  const descriptor = fs.openSync(file, 'r');
  const buffer = Buffer.allocUnsafe(1024 * 1024);
  try {
    for (;;) {
      const count = fs.readSync(descriptor, buffer, 0, buffer.length, null);
      if (count === 0) break;
      hash.update(buffer.subarray(0, count));
    }
  } finally {
    fs.closeSync(descriptor);
  }
  return hash.digest('hex');
}

function stableJson(value) {
  if (Array.isArray(value)) return `[${value.map(stableJson).join(',')}]`;
  if (value !== null && typeof value === 'object') {
    return `{${Object.keys(value).sort().map(key => `${JSON.stringify(key)}:${stableJson(value[key])}`).join(',')}}`;
  }
  return JSON.stringify(value);
}

function canonicalAbiSignature(contract) {
  const signed = { ...contract };
  delete signed.signature_sha256;
  return crypto.createHash('sha256').update(stableJson(signed), 'utf8').digest('hex');
}

function peMachine(file) {
  if (!fs.existsSync(file)) fail(`Missing executable: ${file}`);
  const data = fs.readFileSync(file);
  if (data.length < 0x40 || data[0] !== 0x4d || data[1] !== 0x5a) fail(`Not a Windows PE executable: ${file}`);
  const peOffset = data.readUInt32LE(0x3c);
  if (peOffset + 6 > data.length || data.toString('binary', peOffset, peOffset + 4) !== 'PE\x00\x00') {
    fail(`Invalid Windows PE header: ${file}`);
  }
  return data.readUInt16LE(peOffset + 4);
}

function requireX64(file) {
  const machine = peMachine(file);
  if (machine !== machineX64) {
    const names = new Map([[0x014c, 'x86'], [0xaa64, 'arm64'], [machineX64, 'x64']]);
    fail(`S0 requires Windows x64 (PE machine 0x${machineX64.toString(16)}); found ${names.get(machine) || 'unknown'} (0x${machine.toString(16).padStart(4, '0')}) at ${file}`);
  }
}

function run(command, args, environment = process.env) {
  return spawnSync(command, args.map(String), {
    cwd: root,
    env: environment,
    encoding: 'utf8',
    windowsHide: true,
    stdio: ['ignore', 'pipe', 'pipe']
  });
}

function outputOf(completed) {
  return `${completed.stdout || ''}${completed.stderr || ''}`;
}

function probeSdk(dotnet, lock) {
  if (!fs.existsSync(dotnet)) {
    fail(`Missing .NET executable: ${dotnet}. Run node Scripts/Windows/InstallCSharpToolchain.mjs`);
  }
  requireX64(dotnet);
  const version = run(dotnet, ['--version']);
  const actual = outputOf(version).trim();
  if (version.status !== 0 || actual !== lock.sdk_version) {
    fail(`Expected .NET SDK ${lock.sdk_version} at ${dotnet}; dotnet --version returned ${version.status}: ${actual}`);
  }
  const runtime = path.join(path.dirname(dotnet), 'shared', 'Microsoft.NETCore.App', lock.runtime_version);
  for (const name of ['coreclr.dll', 'hostpolicy.dll', 'System.Private.CoreLib.dll']) {
    const candidate = path.join(runtime, name);
    if (!fs.existsSync(candidate)) {
      fail(`Missing locked .NET runtime dependency: ${candidate}. Re-run Scripts/Windows/InstallCSharpToolchain.mjs after restoring the locked archive.`);
    }
  }
  const hostfxr = path.join(path.dirname(dotnet), 'host', 'fxr', lock.runtime_version, 'hostfxr.dll');
  if (!fs.existsSync(hostfxr)) fail(`Missing locked .NET host dependency: ${hostfxr}`);
}

function macroName(prefix, name) {
  return prefix + name.replace(/([a-z0-9])([A-Z])/g, '$1_$2').toUpperCase();
}

function sameJson(left, right) {
  return stableJson(left) === stableJson(right);
}

function validateContract(lock) {
  require(sameJson(readJson(path.join(root, 'global.json')), {
    sdk: { version: lock.sdk_version, rollForward: 'disable', allowPrerelease: false }
  }), 'global.json does not exactly pin the locked stable SDK');

  const runtimeOptions = readJson(runtimeconfig).runtimeOptions;
  require(sameJson(runtimeOptions, {
    tfm: lock.target_framework,
    framework: { name: 'Microsoft.NETCore.App', version: lock.runtime_version },
    rollForward: 'Disable'
  }), 'ManagedHost runtimeconfig does not exactly pin the locked runtime');

  require(sameJson(readJson(path.join(fixture, 'packages.lock.json')), {
    version: 1, dependencies: { [lock.target_framework]: {} }
  }), 'S0 dependency lock must contain no external packages');
  require(fs.readFileSync(path.join(fixture, 'NuGet.Config'), 'utf8').includes('<clear />'),
    'S0 NuGet.Config must clear all remote sources');

  const contract = readJson(abiPath);
  const signature = canonicalAbiSignature(contract);
  require(contract.signature_sha256 === signature, `Host ABI signature mismatch: expected ${signature}`);
  const header = fs.readFileSync(abiHeader, 'utf8');
  const signatureMatch = header.match(/OVCS_HOST_ABI_SIGNATURE_SHA256\s+"([0-9a-f]{64})"/);
  require(signatureMatch && signatureMatch[1] === signature, 'Host ABI header signature is stale');
  const bytesSection = header.match(/#define OVCS_HOST_ABI_SIGNATURE_BYTES([\s\S]*?)\n\n/);
  const signatureBytes = bytesSection ? [...bytesSection[1].matchAll(/0x([0-9a-f]{2})/g)].map(match => match[1]).join('') : '';
  require(signatureBytes === signature, 'Host ABI header signature byte initializer is stale');
  require(contract.abi_version === 1 && header.includes('OVCS_HOST_ABI_VERSION UINT32_C(1)'),
    'Host ABI version differs between JSON and header');

  for (const [category, prefix] of [
    ['status_codes', 'OVCS_STATUS_'], ['lifecycle_codes', 'OVCS_LIFECYCLE_'], ['field_tags', 'OVCS_FIELD_']
  ]) {
    const values = contract[category];
    require(new Set(Object.values(values)).size === Object.keys(values).length, `Duplicate value in ${category}`);
    for (const [name, value] of Object.entries(values)) {
      const macro = macroName(prefix, name);
      const expression = new RegExp(`#define\\s+${macro}\\s+\\(\\([^)]*\\)${value}\\)`);
      require(expression.test(header), `Header missing ${macro}=${value}`);
    }
  }

  const table = header.match(/typedef struct OvCsHostApi\s*\{([\s\S]*?)\}\s*OvCsHostApi;/);
  require(table, 'OvCsHostApi declaration is missing');
  const entries = [...table[1].matchAll(/\bOvCs[A-Za-z0-9]+Fn\s+([a-z0-9_]+);/g)].map(match => match[1]);
  require(sameJson(entries, contract.table_entries),
    `Host ABI table order differs: header=${JSON.stringify(entries)}, schema=${JSON.stringify(contract.table_entries)}`);
  require(sameJson(contract.target, {
    os: 'windows', architecture: 'x86_64', calling_convention: 'cdecl',
    endianness: 'little', pack: 8, pointer_bits: 64
  }), 'Host ABI 1 target tuple changed');
  return signature;
}

class Evidence {
  constructor(directory, environment) {
    this.directory = directory;
    this.environment = environment;
    this.commands = [];
  }

  capture(name, command, args) {
    const completed = run(command, args, this.environment);
    const log = path.join(this.directory, `${name.replaceAll('/', '-')}.log`);
    fs.writeFileSync(log, outputOf(completed), 'utf8');
    this.commands.push({ name, argv: [command, ...args].map(String), exit_code: completed.status, log: path.basename(log) });
    return { completed, log };
  }

  run(name, command, args, expectSuccess = true) {
    const { completed, log } = this.capture(name, command, args);
    if (expectSuccess && completed.status !== 0) fail(`${name} failed with exit ${completed.status}; see ${log}`);
    if (!expectSuccess && completed.status === 0) fail(`${name} unexpectedly succeeded; see ${log}`);
    return outputOf(completed);
  }
}

function sourceReceipt() {
  const files = [
    path.join(root, 'global.json'), path.join(root, 'CS_SCRIPTING_INTEGRATION_PLAN.md'),
    lockPath, abiPath, path.join(root, 'Tools', 'OvCSharp', 'Schema', 'HOST_ABI.md'),
    abiHeader, runtimeconfig,
    path.join(root, 'Scripts', 'Windows', 'InstallCSharpToolchain.mjs'),
    path.join(root, 'Tests', 'Scripting', 'Integration', 'verify_s0.mjs'),
    ...fs.readdirSync(fixture).map(name => path.join(fixture, name)).filter(file => fs.statSync(file).isFile())
  ].sort();
  return Object.fromEntries(files.map(file => [path.relative(root, file).replaceAll('\\', '/'), hashFile(file)]));
}

function expectError(checks, name, action, requiredText) {
  try {
    action();
  } catch (error) {
    require(error.message.includes(requiredText), `${name} diagnostic was not actionable: ${error.message}`);
    checks.push({ name, diagnostic: error.message });
    return;
  }
  fail(`Negative probe unexpectedly succeeded: ${name}`);
}

function parseArguments(argv) {
  const result = { dotnet: undefined, probeOnly: false };
  for (let index = 0; index < argv.length; ++index) {
    if (argv[index] === '--dotnet') {
      if (index + 1 >= argv.length) fail('Missing value for --dotnet');
      result.dotnet = argv[++index];
    } else if (argv[index] === '--probe-only') {
      result.probeOnly = true;
    } else {
      fail(`Unknown argument: ${argv[index]}`);
    }
  }
  return result;
}

function defaultDotnet(lock) {
  const local = path.join(root, 'Build', 'Toolchains', `dotnet-${lock.sdk_version}`, 'dotnet.exe');
  if (fs.existsSync(local)) return local;
  if (process.env.OVERLOAD_DOTNET) return process.env.OVERLOAD_DOTNET;
  const where = run('where.exe', ['dotnet.exe']);
  const first = outputOf(where).split(/\r?\n/).find(Boolean);
  return first || local;
}

try {
  require(process.platform === 'win32', 'S0 Host ABI 1 requires Windows');
  require(process.arch === 'x64', 'S0 verifier requires an x64 Node process');
  const args = parseArguments(process.argv.slice(2));
  const lock = readJson(lockPath);
  require(lock.schema_version === 1, 'Unsupported C# toolchain lock schema');
  require(lock.target_rid === 'win-x64', 'S0 requires locked RID win-x64');
  const dotnet = path.resolve(args.dotnet || defaultDotnet(lock));
  probeSdk(dotnet, lock);
  if (args.probeOnly) {
    console.log(`PASS: .NET SDK probe ${lock.sdk_version} x64 at ${dotnet}`);
    process.exit(0);
  }

  const outputParent = path.join(root, 'Build', 'Tests', 'Scripting');
  fs.mkdirSync(outputParent, { recursive: true });
  const output = fs.mkdtempSync(path.join(outputParent, 's0-'));
  console.log(`S0 evidence: ${output}`);
  const environment = {
    ...process.env,
    DOTNET_ROOT: path.dirname(dotnet),
    DOTNET_MULTILEVEL_LOOKUP: '0',
    DOTNET_CLI_HOME: path.join(output, 'DotNetHome'),
    DOTNET_SKIP_FIRST_TIME_EXPERIENCE: '1',
    DOTNET_CLI_TELEMETRY_OPTOUT: '1',
    DOTNET_NOLOGO: '1',
    NUGET_PACKAGES: path.join(output, 'NuGetPackages'),
    GIT_CONFIG_GLOBAL: 'NUL'
  };
  const evidence = new Evidence(output, environment);
  const checks = [];
  const before = sourceReceipt();

  evidence.run('baseline', 'git', [
    '-c', 'safe.directory=H:/overload/Overload', 'merge-base', '--is-ancestor', 'c59edf3', 'HEAD'
  ]);
  const signature = validateContract(lock);
  checks.push('baseline-c59edf3', 'global-sdk-pin', 'runtimeconfig-pin',
    'empty-locked-dependencies', 'host-abi-signature-and-table');

  const archive = path.join(root, 'Build', 'Toolchains', lock.archive);
  if (fs.existsSync(archive)) {
    require(hashFile(archive, 'sha512').toLowerCase() === lock.sha512.toLowerCase(),
      `Locked SDK archive checksum mismatch: ${archive}`);
    checks.push('installed-archive-sha512');
  }

  const info = evidence.run('dotnet-info', dotnet, ['--info']);
  require(info.includes(lock.sdk_version) && info.includes(lock.runtime_version) && info.includes('win-x64'),
    'dotnet --info omitted the locked SDK/runtime/RID');

  const negativeChecks = [];
  expectError(negativeChecks, 'missing-sdk',
    () => probeSdk(path.join(output, 'missing', 'dotnet.exe'), lock),
    'Run node Scripts/Windows/InstallCSharpToolchain.mjs');
  const fakeX86 = path.join(output, 'fake-x86.exe');
  const fake = Buffer.alloc(0x86);
  fake.write('MZ', 0, 'ascii');
  fake.writeUInt32LE(0x80, 0x3c);
  fake.write('PE\0\0', 0x80, 'binary');
  fake.writeUInt16LE(0x014c, 0x84);
  fs.writeFileSync(fakeX86, fake);
  expectError(negativeChecks, 'wrong-architecture', () => requireX64(fakeX86), 'requires Windows x64');
  checks.push('missing-sdk-diagnostic', 'wrong-architecture-diagnostic');

  const clangCl = path.join(root, 'Build', 'Toolchains', 'llvm-18.1.8', 'bin', 'clang-cl.exe');
  if (fs.existsSync(clangCl)) {
    const include = `/I${path.join(root, 'Sources', 'OvCore', 'include')}`;
    evidence.run('host-abi-c', clangCl, [
      '/nologo', '/TC', '/std:c11', '/W4', '/WX', '/Zs', include,
      path.join(fixture, 'host_abi_validation.c')
    ]);
    evidence.run('host-abi-cpp', clangCl, [
      '/nologo', '/TP', '/std:c++20', '/W4', '/WX', '/Zs', include,
      path.join(fixture, 'host_abi_validation.cpp')
    ]);
    checks.push('host-abi-pure-c-compile', 'host-abi-cpp-compile');
  }

  const obj = path.join(output, 'obj');
  const binary = path.join(output, 'bin');
  const commonProperties = [
    `-p:BaseIntermediateOutputPath=${obj.replaceAll('\\', '/')}/`,
    `-p:BaseOutputPath=${binary.replaceAll('\\', '/')}/`
  ];
  const project = path.join(fixture, 'Overload.ToolchainSmoke.csproj');
  evidence.run('restore', dotnet, [
    'restore', project, '--locked-mode', '--configfile', path.join(fixture, 'NuGet.Config'),
    '--packages', path.join(output, 'NuGetPackages'), ...commonProperties
  ]);
  const buildArguments = [
    'build', project, '--configuration', 'Release', '--no-restore', ...commonProperties
  ];
  const buildAttempt = evidence.capture('dotnet-build', dotnet, buildArguments);

  const assembly = path.join(binary, 'Release', 'net10.0', 'Overload.ToolchainSmoke.dll');
  const generatedConfig = assembly.replace(/\.dll$/, '.runtimeconfig.json');
  const assets = path.join(obj, 'project.assets.json');
  let standardBuild = buildAttempt.completed.status === 0;
  const externalBlockers = [];
  if (!standardBuild) {
    const buildOutput = outputOf(buildAttempt.completed);
    require(buildOutput.includes("Your Windows doesn't fully support CET"),
      `dotnet build failed for a reason other than the known OS CET prerequisite; see ${buildAttempt.log}`);
    externalBlockers.push({
      check: 'dotnet-build',
      result: 'failed',
      reason: "Windows 10.0.22000.318 does not fully support the CET-marked .NET 10 csc.exe apphost",
      evidence: path.basename(buildAttempt.log),
      recovery: 'Install all available Windows updates, reboot, then rerun this verifier; do not disable CET globally.'
    });

    fs.mkdirSync(path.dirname(assembly), { recursive: true });
    const referenceDirectory = path.join(
      path.dirname(dotnet), 'packs', 'Microsoft.NETCore.App.Ref', lock.runtime_version, 'ref', lock.target_framework
    );
    require(fs.existsSync(referenceDirectory), `Missing locked reference pack: ${referenceDirectory}`);
    const references = fs.readdirSync(referenceDirectory)
      .filter(name => name.endsWith('.dll')).sort().map(name => path.join(referenceDirectory, name));
    require(references.length > 0, `Locked reference pack is empty: ${referenceDirectory}`);
    const responseFile = path.join(output, 'csc.rsp');
    const response = [
      '/nologo', '/noconfig', '/nostdlib+', '/langversion:14.0', '/nullable:enable',
      '/warnaserror+', '/deterministic+', '/optimize+', '/platform:x64', '/target:exe',
      `/out:${JSON.stringify(assembly)}`,
      ...references.map(reference => `/reference:${JSON.stringify(reference)}`),
      JSON.stringify(path.join(fixture, 'Program.cs'))
    ];
    fs.writeFileSync(responseFile, response.join('\n') + '\n', 'utf8');
    const compiler = path.join(path.dirname(dotnet), 'sdk', lock.sdk_version, 'Roslyn', 'bincore', 'csc.dll');
    require(fs.existsSync(compiler), `Missing locked managed compiler: ${compiler}`);
    evidence.run('managed-compiler-smoke', dotnet, [compiler, `@${responseFile}`]);
    fs.copyFileSync(runtimeconfig, generatedConfig);
  }

  require(fs.existsSync(assembly) && fs.existsSync(generatedConfig) && fs.existsSync(assets),
    'SDK build omitted the assembly, runtimeconfig, or restore assets');
  requireX64(assembly);
  const generated = readJson(generatedConfig).runtimeOptions;
  require(generated.tfm === lock.target_framework, 'Generated runtimeconfig TFM drift');
  require(sameJson(generated.framework, { name: 'Microsoft.NETCore.App', version: lock.runtime_version }),
    'Generated runtimeconfig framework drift');
  require(generated.rollForward === 'Disable', 'Generated runtimeconfig roll-forward drift');
  require(sameJson(readJson(assets).libraries, {}), 'S0 smoke restored an external package');

  const firstHashes = { assembly: hashFile(assembly), runtimeconfig: hashFile(generatedConfig) };
  if (standardBuild) {
    evidence.run('no-change-build', dotnet, buildArguments);
  } else {
    const responseFile = path.join(output, 'csc.rsp');
    const compiler = path.join(path.dirname(dotnet), 'sdk', lock.sdk_version, 'Roslyn', 'bincore', 'csc.dll');
    evidence.run('no-change-managed-compiler-smoke', dotnet, [compiler, `@${responseFile}`]);
  }
  require(sameJson(firstHashes, { assembly: hashFile(assembly), runtimeconfig: hashFile(generatedConfig) }),
    'No-change managed build changed deterministic outputs');
  const smoke = evidence.run('run', dotnet, [assembly]);
  require(smoke.includes('PASS: S0 .NET toolchain smoke') && smoke.includes('.NET 10.0.12') && smoke.includes('X64'),
    'Managed smoke output did not prove locked runtime and x64 execution');
  checks.push('dotnet-10.0.401-x64', 'locked-offline-restore',
    standardBuild ? 'dotnet-managed-release-build' : 'sdk-managed-compiler-release-build',
    standardBuild ? 'generated-runtimeconfig' : 'runtimeconfig-execution-pin',
    'deterministic-no-change-build', 'managed-runtime-execution');

  require(sameJson(sourceReceipt(), before), 'S0 source files changed during validation; rerun fixed sources');
  const verification = {
    stage: 'S0', passed: true, timestamp_utc: new Date().toISOString(), baseline: 'c59edf3',
    sdk_version: lock.sdk_version, runtime_version: lock.runtime_version, target_rid: lock.target_rid,
    dotnet, host_abi_version: 1, host_abi_signature_sha256: signature,
    checks, external_blockers: externalBlockers, negative_probes: negativeChecks,
    commands: evidence.commands, source_receipt: before
  };
  const verificationPath = path.join(output, 'verification.json');
  fs.writeFileSync(verificationPath, JSON.stringify(verification, null, 2) + '\n', 'utf8');
  console.log(`PASS: S0 acceptance; evidence: ${verificationPath}`);
} catch (error) {
  console.error(`S0 verification failed: ${error.message}`);
  process.exit(1);
}
