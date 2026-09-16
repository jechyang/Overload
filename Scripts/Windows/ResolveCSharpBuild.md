# 修复标准 dotnet build 的 CET 系统前提

2026-09-14 实测系统为 Windows 11 21H2，build 22000.318。
私有 .NET SDK 10.0.401 校验通过，但其原版 `csc.exe -version` 在正确指定
私有 runtime 后仍报 CET fatal error。通过 `dotnet csc.dll` 启动同一编译器
可以执行；因此问题发生在可执行程序启动阶段，不是 C# 代码的编译诊断。

修复方向是更新 Windows。微软说明 .NET 9 起 apphost 默认启用 CET，
项目的 `CETCompat` 只影响构建出的应用，不能改写 SDK 自带的 csc.exe。
见 [微软 CET 兼容性说明](https://learn.microsoft.com/en-us/dotnet/core/compatibility/interop/9.0/cet-support)
和 [Windows 11 版本信息](https://learn.microsoft.com/en-us/windows/release-health/windows11-release-information)。

## 手动执行

双击同目录的 `ResolveCSharpBuild.cmd`，或在 CMD 执行：

```bat
H:\overload\Overload\Scripts\Windows\ResolveCSharpBuild.cmd --update
```

这会打开 Windows 更新。安装适用于本机的功能更新和累积更新，然后重启。
如果 21H2 无法通过设置页升级，可运行下面的命令打开微软官方升级页面，
使用安装助手或匹配系统语言/版本的 ISO 做就地升级；在安装界面确认保留个人文件和应用：

```bat
H:\overload\Overload\Scripts\Windows\ResolveCSharpBuild.cmd --download
```

升级是否适用于本机由微软安装程序检查。脚本不会自动下载、安装、重启或绕过兼容性检查。

重启后执行：

```bat
H:\overload\Overload\Scripts\Windows\ResolveCSharpBuild.cmd --verify
```

验证使用仓库锁定的 SDK，先检查原版 csc.exe，再新建隔离 smoke 项目，执行
**标准 `dotnet build --no-incremental` 与运行**，最后标准强制构建
ManagedHost 和 ScriptCore。不会转用 csc.dll 编译、覆盖编译器或关闭 CET。
原生 Debug 生成文件必须已存在；本工作区已有 S2 构建结果。

- exit 0：全部上述标准构建和 smoke 运行通过。
- exit 2：仍观察到 CET 错误，尚未修复。
- exit 1：其他失败，按对应日志处理。

每次完整日志和真实退出码写入 `Build/Tests/Scripting/dotnet-repair-*/verification.json`。
只有 `standard_dotnet_build_passed=true` 才表示该轮通过；打开更新页面不表示修复完成。
此脚本需要 Node.js 20+，入口自动查找 PATH 或本机 Codex 已有的 Node，完全不依赖 PowerShell。
