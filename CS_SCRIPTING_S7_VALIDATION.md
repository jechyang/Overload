# C# 接入 S7 实施与验收记录

日期：2026-09-16。起始提交：`ae2c3df`（S6）。S7 本地提交不 push；实际提交号以 `git log -1 -- CS_SCRIPTING_S7_VALIDATION.md` 为准。

状态：S7 的本地构建、发布包、无 GPU 运行、AppContainer 隔离、真实 GPU 回归和性能采样已完成。标准 `dotnet build` 仍因 Windows CET 失败，兼容入口通过；独立机器/VM 和远端 CI 未执行，CI 用例已入库并可执行。

## 实现

- `BuildCSharp.mjs` 的显式完整 MSBuild 兼容入口支持 Debug/Release/Publish 和 MSBuild publish，保持 SDK 10.0.401、runtime 10.0.12、net10.0，不调用直接 csc 伪造 build/publish。
- `PackageCSharpGame.mjs` 生成应用私有 Runtime、Managed、GameScripts、原生游戏依赖、app-local CRT、Game.ini、包文件清单和 SHA-256；`Tools/OvCSharp/Licenses/dependencies.json` 与打包清单覆盖依赖和许可来源。
- `RuntimeHost.cpp` 启动时记录并校验包内 hostfxr/CoreCLR；发布运行路径使用可执行文件相对的 `Runtime/dotnet` 和 `Managed`，不包含开发机绝对路径。
- AppContainer 驱动只给新包 RX 权限，只给 Evidence 目录写权限；清除 PATH、DOTNET、NuGet 和用户目录依赖，并用 SDK probe 验证已安装 SDK 入口不可访问。
- `Tools/OvReflectGen/incremental.py` 的 owner 归属使用 manifest 声明并为隔离 fixture 兼容绝对 `Sources/<module>` 路径；这是 R3 回归暴露的真实 owner 错误修复。
- `Metrics.cs` 和 `native_s7_headless.cpp` 提供无分配计数探针、生命周期/跨 ABI 计数、Transform 实际结果和原始 JSONL 样本；计时只包围 `Scene.Update`。

## 固定验收入口

完整入口：

```text
node Tests/Scripting/Integration/verify_s7.mjs --evidence <absolute-evidence-directory> --python <absolute-python-path>
```

本次最终证据逻辑路径为 `Build/Tests/Scripting/s7-70i0EU`，物理证据位于本会话允许写入的 C 盘目录；H 盘上的路径是 junction，未删除旧证据。入口完成：

- Debug、Release、Publish 原生完整 MSBuild 构建；三配置原生无修改构建保持对象内容和时间戳。
- 三配置托管 restore/build/publish；host 和 GameScripts 的无修改 publish 保持 DLL 内容和时间戳。
- 标准 `dotnet restore/build -t:Rebuild`：三配置均为 CET 失败，未通过；错误与 OS CET 前置条件一致。
- 显式 `BuildCSharp.mjs --compat` 完整 build/publish：三配置均通过。
- 包内容包含 `OvGame.exe`、ManagedHost/ScriptCore/GameScripts、runtimeconfig/deps、私有 `hostfxr.dll`、私有 `coreclr.dll`、完整 .NET runtime、原生依赖、CRT、资源、许可清单和 `s7-package.json` 文件哈希。
- Debug、Release、Publish 包均在新 AppContainer 中启动；SDK probe exit 0，已安装 SDK 入口不可访问，PATH 不包含开发机 dotnet，包内 hostfxr/CoreCLR 均被实际加载，包内文件哈希在运行后保持不变。
- 每个配置的 `default` 和 `render-thread` 均通过真实 OvGame：C# `awake`/`start`/更新/销毁轨迹、Transform/Box 结果和 Lua 场景记录通过；退出码为 0。默认/RenderThread 运行使用独立宿主进程，不能由 headless 静态输出替代。

## 性能原始数据

每个配置、`noop`/`transform`、1/100/1000 Behaviour 均预热至少 500 帧且至少 1 秒，然后记录 500 个原始样本到各配置目录的 `performance.jsonl`，汇总在 `performance-summary.json`。计时不包含渲染、物理步进或计数探针；GC 字节为托管 Main 线程分配，GC 次数为进程级；每个 Transform 更新验证实际位置结果。

Publish 的代表性汇总如下，完整 Debug/Release/Publish 数据保留在收据目录：

| workload | count | mean ms | p50 ms | p95 ms | allocated bytes | native→managed | managed→native |
|---|---:|---:|---:|---:|---:|---:|---:|
| noop | 1 | 0.0005052 | 0.0005 | 0.0006 | 52,000 | 500 | 0 |
| noop | 100 | 0.0296656 | 0.0293 | 0.0318 | 5,200,000 | 50,000 | 0 |
| noop | 1000 | 0.4149962 | 0.3544 | 0.6058 | 52,000,000 | 500,000 | 0 |
| transform | 1 | 0.0009044 | 0.0009 | 0.0012 | 108,000 | 500 | 2,000 |
| transform | 100 | 0.0806114 | 0.0800 | 0.0815 | 10,800,000 | 50,000 | 200,000 |
| transform | 1000 | 0.8533886 | 0.8421 | 0.8868 | 108,000,000 | 500,000 | 2,000,000 |

这些数据只描述当前机器、当前配置和 `Scene.Update` 范围，不预设相对 Lua 或整帧渲染性能结论。

## 回归矩阵与边界

- R0、R1、R2、R3、R4、R5 均通过；R2/R4/R5 的 Debug/Release/Publish 默认和 RenderThread 真实 GPU 用例通过。R3 包含 fixture 事务、失败恢复、并发配置、无修改 MSBuild 和全解决方案矩阵。
- R6 的 `--skip-build --engine` 三配置通过生成 ABI、C/C++/Clang、CoreCLR、错配拒绝、默认和 RenderThread 引擎用例。R6 额外的全量 solution build 在 Debug link 处因 H 盘空间不足报 `LNK1180`，因此未把该次全量 build 写成通过；S7 自身三配置 native build 已在同一入口通过。
- S1–S6 的固定收据分别沿用既有验收记录；S6 收据 `s6-aaF4Qf/verification.json` 已证明 WeakReference 回收和 20 次两模式热重载。S5 指定基线本会话第一次沙箱运行因 FileTracker `E_ACCESSDENIED` 失败，沙箱外复核随后在 S4 OvGame link 因 H 盘空间报 `LNK1180`；既有 S5/S6 成功收据保留，不能把本次失败尝试改写为通过。
- 本地真实 GPU：已执行，R2/R4/R5 记录 NVIDIA GeForce RTX 3070 Ti、OpenGL 4.5，默认/RenderThread 均通过。
- 本地无 GPU：已执行，S7 headless 三配置通过，不创建窗口或 GPU。
- 本地隔离环境：已执行，三配置 AppContainer 无 capability 包运行通过。
- 独立机器/VM：未执行；远端 CI：未执行。`.github/workflows/scripting.yml` 提供完整三配置无 GPU CI 入口，不能把入库的 workflow 标成远端通过。

恢复 R6 全量 native build 的最小步骤是为 H 盘释放足够空间后重跑：

```text
python Tests/Reflection/Integration/verify_r6.py --engine --config Debug Release Publish --dotnet Build/Toolchains/dotnet-10.0.401/dotnet.exe --runtime Build/Toolchains/dotnet-10.0.401/shared/Microsoft.NETCore.App/10.0.12
```

不需要更新 Windows、关闭 CET、修改 SDK 二进制、修改全局 PATH 或降级框架。
