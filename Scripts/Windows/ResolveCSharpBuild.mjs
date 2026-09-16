// Manual OS-update guidance and strict standard-build verification. No fallback compiler.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { createInterface } from 'node:readline/promises';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const windowsUpdate = 'ms-settings:windowsupdate';
const downloadPage = 'https://www.microsoft.com/zh-cn/software-download/windows11';
const help = `用法：
  ResolveCSharpBuild.cmd             交互菜单
  ResolveCSharpBuild.cmd --verify    诊断并强制执行标准 dotnet build
  ResolveCSharpBuild.cmd --update    打开 Windows 更新设置
  ResolveCSharpBuild.cmd --download  打开微软 Windows 11 官方升级页面

更新由你在系统/微软安装程序界面完成。脚本不会自动安装或重启，
不会关闭 CET、修改 SDK、修改全局 PATH 或调用备用编译器。
更新并重启后，请再次运行 --verify。日志保存到 Build/Tests/Scripting/dotnet-repair-*。`;

async function openPage(url) {
  await new Promise((resolve, reject) => {
    const child = spawn(path.join(process.env.SystemRoot || 'C:\\Windows', 'explorer.exe'), [url], {
      cwd: root, stdio: 'ignore', windowsHide: false, detached: true,
    });
    child.once('error', reject);
    child.once('spawn', () => { child.unref(); resolve(); });
  });
  console.log(`已请求打开：${url}\n请在界面中完成更新并重启，然后重新运行本脚本的 --verify。\n打开页面不代表系统或构建已经修复。`);
}

async function verify() {
  const parent = path.join(root, 'Build/Tests/Scripting'); fs.mkdirSync(parent, { recursive: true });
  const output = fs.mkdtempSync(path.join(parent, 'dotnet-repair-'));
  const lock = JSON.parse(fs.readFileSync(path.join(root, 'Tools/OvCSharp/toolchain.lock.json'), 'utf8'));
  const dotnetRoot = path.join(root, 'Build/Toolchains/dotnet-' + lock.sdk_version);
  const dotnet = path.join(dotnetRoot, 'dotnet.exe');
  const env = { ...process.env };
  // Command-scoped isolation only; neither the OS environment nor SDK files change.
  const overrides = new Set(['csctoolexe', 'csctoolpath', 'cscenvironment', 'roslyntargetspath',
    'skipcompilerexecution', 'msbuildsdkspath', 'usehostcompilerifavailable', 'compilertoolpath']);
  for (const key of Object.keys(env)) if (overrides.has(key.toLowerCase())) delete env[key];
  Object.assign(env, { DOTNET_ROOT: dotnetRoot, DOTNET_ROOT_X64: dotnetRoot,
    DOTNET_MULTILEVEL_LOOKUP: '0', DOTNET_NOLOGO: '1', DOTNET_CLI_TELEMETRY_OPTOUT: '1',
    DOTNET_ADD_GLOBAL_TOOLS_TO_PATH: '0', DOTNET_GENERATE_ASPNET_CERTIFICATE: '0',
    DOTNET_CLI_HOME: path.join(output, 'cli'), NUGET_PACKAGES: path.join(output, 'packages') });
  const receipt = { timestamp_utc: new Date().toISOString(), os: os.release(), windows: {},
    sdk: lock.sdk_version, runtime: lock.runtime_version, standard_dotnet_build_passed: false,
    compiler_apphost_passed: false, checks: [], output };
  let sawCet = false;
  const save = () => fs.writeFileSync(path.join(output, 'verification.json'), JSON.stringify(receipt, null, 2));
  async function run(name, exe, argv, timeout = 180000) {
    console.log(`\n[${name}]`);
    const log = fs.createWriteStream(path.join(output, name + '.log'));
    let text = '', expired = false, error;
    const child = spawn(exe, argv, { cwd: root, env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    const timer = setTimeout(() => { expired = true; child.kill(); }, timeout);
    const consume = chunk => { const value = chunk.toString('utf8'); text += value; log.write(value); process.stdout.write(value); };
    child.stdout.on('data', consume); child.stderr.on('data', consume);
    const exit = await new Promise(resolve => {
      child.once('error', ex => { error = ex.message; });
      child.once('close', code => resolve(code));
    });
    clearTimeout(timer);
    if (error) { text += error; log.write(error); console.error(error); }
    await new Promise(resolve => log.end(resolve));
    if (text.includes("Your Windows doesn't fully support CET")) sawCet = true;
    receipt.checks.push({ name, argv: [exe, ...argv], exit_code: exit, timed_out: expired, error }); save();
    return { passed: exit === 0 && !expired && !error, text };
  }
  console.log(`检查结果将保存到：${output}`);
  try {
    // Query only OS version fields, not license/product identifiers.
    for (const key of ['CurrentBuildNumber', 'UBR', 'DisplayVersion', 'EditionID']) {
      const result = await run('windows-' + key, 'reg.exe', ['query',
        'HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion', '/v', key, '/reg:64'], 10000);
      const value = result.text.match(new RegExp(key + '\\s+REG_\\w+\\s+([^\\r\\n]+)'))?.[1]?.trim();
      if (value) receipt.windows[key] = key === 'UBR' ? Number(value) : value;
    }
    const toolchain = await run('toolchain-verify', process.execPath,
      [path.join(root, 'Scripts/Windows/InstallCSharpToolchain.mjs'), '--verify-only']);
    if (!toolchain.passed) throw new Error('私有 SDK 校验失败。先按日志修复 SDK 安装，再重试。');

    // This is the exact executable selected by normal SDK builds, with the private
    // runtime root supplied. No csc.dll fallback can make this check pass.
    receipt.compiler_apphost_passed = (await run('compiler-apphost',
      path.join(dotnetRoot, 'sdk', lock.sdk_version, 'Roslyn/bincore/csc.exe'), ['-version'], 30000)).passed;

    const smoke = path.join(output, 'smoke'); fs.mkdirSync(smoke);
    for (const file of ['Overload.ToolchainSmoke.csproj', 'Program.cs', 'packages.lock.json', 'NuGet.Config'])
      fs.copyFileSync(path.join(root, 'Tests/Scripting/Fixtures/S0', file), path.join(smoke, file));
    const smokeProject = path.join(smoke, 'Overload.ToolchainSmoke.csproj');
    const restoredSmoke = await run('smoke-restore', dotnet, ['restore', smokeProject, '--locked-mode', '--configfile', path.join(smoke, 'NuGet.Config')]);
    const builtSmoke = restoredSmoke.passed && (await run('standard-smoke-build', dotnet,
      ['build', smokeProject, '--no-restore', '--no-incremental', '-c', 'Release'])).passed;
    const ranSmoke = builtSmoke && (await run('standard-smoke-run', dotnet,
      [path.join(smoke, 'bin/Release', lock.target_framework, 'Overload.ToolchainSmoke.dll')])).passed;

    const host = path.join(root, 'Sources/OvManaged/Overload.ManagedHost');
    const production = path.join(output, 'production');
    const properties = ['-p:ReflectionConfiguration=Debug', '-p:OutputPath=' + production.replaceAll('\\', '/') + '/'];
    const restoredHost = await run('managed-restore', dotnet, ['restore', path.join(host, 'Overload.ManagedHost.csproj'),
      '--locked-mode', '--configfile', path.join(host, 'NuGet.Config'), ...properties]);
    const builtHost = restoredHost.passed && (await run('standard-managed-build', dotnet,
      ['build', path.join(host, 'Overload.ManagedHost.csproj'), '--no-restore', '--no-incremental', '-c', 'Debug', ...properties])).passed;
    receipt.standard_dotnet_build_passed = Boolean(builtSmoke && ranSmoke && builtHost && receipt.compiler_apphost_passed);
    receipt.cet_failure_observed = sawCet;
    if (receipt.standard_dotnet_build_passed) {
      receipt.artifacts_sha256 = Object.fromEntries(['Overload.ManagedHost.dll', 'Overload.ScriptCore.dll'].map(name =>
        [name, crypto.createHash('sha256').update(fs.readFileSync(path.join(production, name))).digest('hex')]));
      console.log('\n通过：编译器启动、标准 smoke 强制构建/运行、标准 ManagedHost + ScriptCore 强制构建均 exit 0。');
    } else if (sawCet) {
      console.log(`\n未修复：标准构建仍有 CET 系统兼容性失败。当前 Windows：${receipt.windows.DisplayVersion || '?'} / ${receipt.windows.CurrentBuildNumber || '?'} . ${receipt.windows.UBR ?? '?'}。
下一步：运行 ResolveCSharpBuild.cmd --update，安装系统提供的功能更新和累积更新，然后重启。
如果旧系统不提供升级，运行 --download，使用微软官方 Windows 11 安装助手或 ISO 进行就地升级。
在安装程序中确认保留个人文件和应用；具体可升级版本以微软对本机的兼容性检查为准。
完成后再运行 --verify；只有上述所有标准构建实际 exit 0 才算通过。`);
    } else {
      console.log('\n未通过：请检查失败日志。若提示原生生成文件缺失，先构建 Debug 原生 solution。');
    }
  } catch (error) { receipt.error = String(error); console.error(String(error)); }
  finally { save(); console.log(`\n结果文件：${path.join(output, 'verification.json')}`); }
  return receipt.standard_dotnet_build_passed ? 0 : sawCet ? 2 : 1;
}

try {
  if (process.platform !== 'win32') throw new Error('此脚本仅适用于 Windows x64。');
  let mode = process.argv[2];
  if (!mode) {
    console.log(help);
    const prompt = createInterface({ input: process.stdin, output: process.stdout });
    const choice = await prompt.question('\n1 检查/验证构建   2 Windows 更新   3 微软升级页面   0 退出\n请选择：');
    prompt.close();
    mode = ({ '1': '--verify', '2': '--update', '3': '--download', '0': '--help' })[choice.trim()];
  }
  if (mode === '--verify') process.exitCode = await verify();
  else if (mode === '--update') await openPage(windowsUpdate);
  else if (mode === '--download') await openPage(downloadPage);
  else { console.log(help); if (mode !== '--help') process.exitCode = 1; }
} catch (error) { console.error(String(error)); process.exitCode = 1; }
