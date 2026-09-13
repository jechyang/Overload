#!/usr/bin/env node
// Install the locked Windows x64 .NET SDK without changing global PATH.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(scriptDirectory, '..', '..');
const lockPath = path.join(root, 'Tools', 'OvCSharp', 'toolchain.lock.json');

function fail(message) {
  throw new Error(message);
}

function parseArguments(argv) {
  const result = { destination: undefined, archive: undefined, showPaths: false, verifyOnly: false };
  for (let index = 0; index < argv.length; ++index) {
    const argument = argv[index];
    if (argument === '--destination' || argument === '--archive') {
      if (index + 1 >= argv.length) fail(`Missing value for ${argument}`);
      result[argument.slice(2)] = argv[++index];
    } else if (argument === '--show-paths') {
      result.showPaths = true;
    } else if (argument === '--verify-only') {
      result.verifyOnly = true;
    } else {
      fail(`Unknown argument: ${argument}`);
    }
  }
  return result;
}

function digest(file, algorithm) {
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

function run(command, args) {
  return spawnSync(command, args, {
    cwd: root,
    encoding: 'utf8',
    windowsHide: true,
    stdio: ['ignore', 'pipe', 'pipe']
  });
}

function validateSdk(dotnet, lock) {
  if (!fs.existsSync(dotnet)) fail(`Incomplete .NET SDK: missing ${dotnet}`);
  const version = run(dotnet, ['--version']);
  const actual = `${version.stdout || ''}${version.stderr || ''}`.trim();
  if (version.status !== 0 || actual !== lock.sdk_version) {
    fail(`Expected .NET SDK ${lock.sdk_version} at ${dotnet}; dotnet --version returned ${version.status}: ${actual}`);
  }
  const runtime = path.join(path.dirname(dotnet), 'shared', 'Microsoft.NETCore.App', lock.runtime_version);
  for (const name of ['coreclr.dll', 'hostpolicy.dll', 'System.Private.CoreLib.dll']) {
    const candidate = path.join(runtime, name);
    if (!fs.existsSync(candidate)) fail(`Incomplete .NET runtime ${lock.runtime_version}: missing ${candidate}`);
  }
  const hostfxr = path.join(path.dirname(dotnet), 'host', 'fxr', lock.runtime_version, 'hostfxr.dll');
  if (!fs.existsSync(hostfxr)) fail(`Incomplete .NET host: missing ${hostfxr}`);
}

try {
  if (process.platform !== 'win32' || process.arch !== 'x64') {
    fail(`Host ABI 1 toolchain requires Windows x64; actual=${process.platform}-${process.arch}`);
  }
  const args = parseArguments(process.argv.slice(2));
  const lock = JSON.parse(fs.readFileSync(lockPath, 'utf8'));
  const toolchains = path.join(root, 'Build', 'Toolchains');
  const destination = path.resolve(args.destination || path.join(toolchains, `dotnet-${lock.sdk_version}`));
  const archive = path.resolve(args.archive || path.join(toolchains, lock.archive));
  const dotnet = path.join(destination, 'dotnet.exe');

  if (args.showPaths) {
    console.log(`Repository: ${root}`);
    console.log(`Destination: ${destination}`);
    console.log(`Archive: ${archive}`);
    console.log(`URL: ${lock.download_url}`);
    console.log(`SHA512: ${lock.sha512}`);
    process.exit(0);
  }
  if (args.verifyOnly) {
    validateSdk(dotnet, lock);
    console.log(`.NET SDK is ready: ${destination}`);
    process.exit(0);
  }

  fs.mkdirSync(toolchains, { recursive: true });
  if (!fs.existsSync(archive)) {
    const partial = `${archive}.partial`;
    console.log(`Downloading official .NET SDK: ${lock.download_url}`);
    const download = spawnSync('curl.exe', [
      '--fail', '--location', '--retry', '5', '--retry-delay', '3', '--continue-at', '-',
      '--output', partial, lock.download_url
    ], { cwd: root, stdio: 'inherit', windowsHide: true });
    if (download.status !== 0) fail(`Download failed with exit ${download.status}; rerun to resume ${partial}`);
    fs.renameSync(partial, archive);
  }

  const actualHash = digest(archive, 'sha512');
  if (actualHash.toLowerCase() !== lock.sha512.toLowerCase()) {
    fail(`SHA512 mismatch for ${archive}: expected ${lock.sha512}, got ${actualHash}. Delete only this archive and retry.`);
  }

  if (fs.existsSync(destination)) {
    validateSdk(dotnet, lock);
  } else {
    const staging = path.join(toolchains, `.${path.basename(destination)}.extracting-${process.pid}`);
    if (fs.existsSync(staging)) fail(`Stale extraction directory exists: ${staging}; remove only that directory and retry`);
    fs.mkdirSync(staging, { recursive: false });
    try {
      const extracted = spawnSync('tar.exe', ['-xf', archive, '-C', staging], {
        cwd: root, stdio: 'inherit', windowsHide: true
      });
      if (extracted.status !== 0) fail(`SDK extraction failed with exit ${extracted.status}`);
      validateSdk(path.join(staging, 'dotnet.exe'), lock);
      fs.renameSync(staging, destination);
    } catch (error) {
      const resolvedStaging = path.resolve(staging);
      if (resolvedStaging.startsWith(path.resolve(toolchains) + path.sep) && fs.existsSync(resolvedStaging)) {
        fs.rmSync(resolvedStaging, { recursive: true, force: false });
      }
      throw error;
    }
  }

  fs.writeFileSync(path.join(destination, '.csharp-toolchain-complete.json'), JSON.stringify({
    sdk_version: lock.sdk_version,
    runtime_version: lock.runtime_version,
    target_rid: lock.target_rid,
    archive: path.basename(archive),
    sha512: actualHash,
    download_url: lock.download_url
  }, null, 2) + '\n', 'utf8');
  console.log(`.NET SDK is ready: ${destination}`);
} catch (error) {
  console.error(`C# toolchain installation failed: ${error.message}`);
  process.exit(1);
}
