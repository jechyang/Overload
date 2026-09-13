# C# 游戏脚本接入 S0 实施与验收记录

日期：2026-09-13。阶段：S0 协议与工具链冻结完成；S1–S7 未开始。
基线：`c59edf3` 的 R0–R6 实现，计划提交为其直接后继 `1f30f0a`。
本轮只新增 S0 契约、探测/安装入口和 smoke，没有实现 CoreCLR 原生宿主、
托管 Behaviour、脚本后端分发、编辑器工作流或热重载。

## 交付内容

- `global.json` 精确固定 .NET SDK 10.0.401，禁止 SDK roll-forward 和预览版。
- `Tools/OvCSharp/toolchain.lock.json` 固定 net10.0、运行时 10.0.12、
  Windows x64 官方 ZIP URL、发布日期和 SHA-512。
- `Scripts/Windows/InstallCSharpToolchain.mjs` 将 SDK 安装/校验在被忽略的
  `Build/Toolchains/dotnet-10.0.401`；不写全局 PATH，不静默降级版本。
- `Overload.ManagedHost.runtimeconfig.json` 固定 Microsoft.NETCore.App
  10.0.12，并禁止运行时 roll-forward。这里仅冻结 S1 将使用的配置，尚未
  创建或启动 ManagedHost。
- `HostABI.h` 和 `host-abi.json` 冻结 Host ABI 1；规范 JSON 的 SHA-256
  签名同时以十六进制和 32 字节形式写入头文件。纯 C/C++ 布局夹具固定
  POD 大小、表前缀和 184 字节函数表大小。
- `HOST_ABI.md` 冻结 ScriptIdentity、会话/实例归属、生命周期状态、重入
  安全点、销毁次序、错误文本与输出内存所有权、字段首版矩阵。
- `Tests/Scripting/Fixtures/S0` 是无外部包的 net10.0 x64 smoke；
  `packages.lock.json` 为空依赖，`NuGet.Config` 清除远端源。
- `verify_s0.mjs` 校验基线祖先、所有版本锁和 ABI 签名，实编纯 C/C++，
  运行依赖/位数负例，执行锁定 restore、确定性托管编译和真实运行。

## 固定协议摘要

Host ABI 1 与 R6 Native API ABI 1 分开握手和版本化。目标为 Windows x64、
cdecl、pack 8、小端、64 位指针；入口固定为
`Overload.ManagedHost.Bootstrap.GetHostApi`。握手必须精确匹配版本、
`sizeof(OvCsHostApi)` 和 32 字节签名，任一不符不得发布函数指针。

Host ABI 表共 18 个入口，覆盖输出释放/错误读取、项目会话创建关闭、R6
模块绑定、类型与别名查询、字段元数据、实例创建销毁、生命周期分派和
字段读写。所有调用同步、Main-only、异常转状态。Host ABI 1 签名为：

`1f7e282e2f1353962d004dfe3f9b88a52fe17311d6b4d51491d8622a529a1b10`

持久 ScriptIdentity 默认为 `backend / assembly logical name / canonical type
name`；允许显式稳定 ID 和旧身份别名，不使用文件名、版本化
AssemblyQualifiedName、GCHandle、地址、会话代次或 token。Lua 的规范化
项目相对路径继续可读，且 backend 参与身份，避免与 C# 文本碰撞。

实例在 Owner 注入和字段恢复后才进入 Awake。回调内增删、激活切换、换
场景、关会话和应用新代都排队到 Main 安全点；待删除实例停止本阶段后续
分派。销毁次序固定为停止分派/标记 Destroying、在句柄仍有效时最多调用
一次托管 Destroy、清除托管引用、使 native epoch 失效、删除 native
对象，最后仅在关闭整个会话时尝试回收项目 ALC。

首版字段协议保留 bool、全部固定宽度有/无符号整数、float/double、严格
UTF-8 string、精确底层类型 enum、Vector3、Quaternion、ActorRef 和
AssetRef。long/ulong 不经过 double。数组、nullable、对象图、属性、
static/readonly 和不支持类型不猜测转换，给诊断并保留未知存档数据。

## 实测环境与下载收据

| 项目 | 本次实测 |
|---|---|
| OS | Windows 10.0.22000.318，x64 |
| Node（验收编排） | 20.12.0 |
| .NET SDK | 10.0.401，commit e34a38d2ae |
| .NET runtime/host | Microsoft.NETCore.App 10.0.12，x64 |
| RID | win-x64 |
| 私有安装目录 | H:/overload/Overload/Build/Toolchains/dotnet-10.0.401 |
| 官方归档 | dotnet-sdk-10.0.401-win-x64.zip，300608304 bytes |
| 实测 SHA-512 | `24b670ad3d923bfcf47df6c3b034152398b42f6dbc388e10d783aee1cfb5e5817d399fc0ae2a12cfa822a55e61d34830ccb15c50ef6efee437ab874bb7c79430` |
| ABI 编译器 | clang-cl 18.1.8，x86_64-pc-windows-msvc |
| R6 复查解释器 | Codex 隔离 Python 3.12.14（系统 `python` 仍是 2.7.18） |

下载前官方 URL 的 HEAD 请求返回 HTTP 200、Content-Length 300608304；下载
后 SHA-512 与锁文件和微软发布元数据一致。安装器随后用绝对路径确认 SDK
版本、runtime、hostfxr、hostpolicy、CoreCLR 和基础类库都存在。SDK ZIP、
展开目录、CLI home、NuGet cache 和测试输出均在已忽略的 `Build/` 下，
不进入提交。

## S0 验收结果

最终入口：

~~~text
node Scripts/Windows/InstallCSharpToolchain.mjs --verify-only
node Tests/Scripting/Integration/verify_s0.mjs
~~~

最终固定源码证据：
`Build/Tests/Scripting/s0-d5zozt/verification.json`。

| 检查 | 实际结果 |
|---|---|
| `c59edf3` 为当前 HEAD 祖先 | 通过，exit 0 |
| SDK 归档 SHA-512 | 通过 |
| Host ABI JSON/header 签名与表顺序 | 通过 |
| Host ABI 纯 C（C11，`/W4 /WX /Zs`） | 通过 |
| Host ABI C++20（`/W4 /WX /Zs`） | 通过 |
| 缺 SDK 负例 | 通过；指出安装命令与缺失绝对路径 |
| x86 PE 负例 | 通过；指出需要 0x8664、实际 0x014c |
| 无远端源、locked restore | 通过；assets 中外部 libraries 为空 |
| SDK 10.0.401 Roslyn 编译 x64 Release smoke | 通过，真实生成托管 PE |
| 相同响应文件重复编译 | 通过；程序集/runtimeconfig SHA-256 不变 |
| runtime 10.0.12 x64 执行 smoke | 通过，exit 0 |
| 标准 `dotnet build --no-restore` | **失败，未标为通过**；见下一节 |

实际运行输出：

~~~text
PASS: S0 .NET toolchain smoke; framework=.NET 10.0.12; architecture=X64; utf16-length=17
~~~

正常 `dotnet build` 失败后，验收器没有跳过编译或复用旧 DLL。它通过已
能运行的同一 10.0.401 `dotnet` host 直接启动该 SDK 自带的
`Roslyn/bincore/csc.dll`，显式引用锁定的 10.0.12 net10.0 reference pack，
重新编译 smoke 两次并实际运行。因此“固定 SDK 能编译并运行最小项目”
已有真实证据；但 MSBuild 到 CET 标记 `csc.exe` 的标准项目构建路径在这台
机器上仍然是失败项，不得引用上面的直接编译结果声称 `dotnet build` 通过。

## 已执行但未通过/未完成

### 当前 Windows 的 CET 阻塞

`Build/Tests/Scripting/s0-d5zozt/dotnet-build.log` 的原始错误为：

~~~text
Fatal error.
Your Windows doesn't fully support CET. Please install all available Windows updates.
~~~

该命令实际执行并 exit 1。`UseSharedCompilation=false` 和
`CETCompat=false` 的额外探测仍得到同一错误；后者不能改变 SDK 自带
`csc.exe` 的既有标记。本轮没有篡改官方 SDK、没有编辑 csc.exe、没有关闭
系统或全局 CET，也没有把标准构建写成通过。

最小恢复步骤：

1. 在 Windows Update 安装所有可用累积/安全更新并重启；若该系统映像不能
   更新，则在已更新的 Windows x64 主机或 VM 复现。
2. 保留或重新校验私有 SDK：
   `node Scripts/Windows/InstallCSharpToolchain.mjs --verify-only`。
3. 重新运行 `node Tests/Scripting/Integration/verify_s0.mjs`。
4. 只有新 `verification.json` 中 `dotnet-build` exit 0 且
   `external_blockers` 为空，才可补记标准项目构建通过。不要以全局关闭
   CET 作为首选恢复方式。

### R6 本轮复查

原 R6 固定源码完整证据 `Build/Tests/Reflection/r6-3a6n95b4/verification.json`
仍存在，记录 114 项、三配置、两种编译器/CoreCLR 和真实引擎两渲染模式
全部通过；这是 `c59edf3` 的既有基线，本段引用不表示本轮重跑。

本轮另外用显式 Python 3.12.14 各启动一次 `verify_r6.py --skip-build`：

- .NET 10.0.12：`Build/Tests/Reflection/r6-cjwmvtpz`；
- 原 CoreCLR 6.0.15：`Build/Tests/Reflection/r6-ngne56x9`。

两轮均实际完成到 Debug/MSVC/CoreCLR 正例，ABI mismatch 负例日志也得到
预期 `BridgeException: status 8`，但未处理异常子进程在当前桌面主机上未
退出，旧验收器又没有超时，故人工中止。两轮都没有 `verification.json`，
均记为**未完成**，不能标为 R6 本轮通过。未为 S0 修改 R6 验收器或拼接
两轮片段；S1 前如需新 R6 固定源码矩阵，应先在更新后的系统重跑完整入口。

## 未验证范围和阶段边界

- 没有实现或验证 native 进程通过 nethost/hostfxr 启动 CLR；这是 S1。
- 没有加载用户程序集、发现 Behaviour、创建托管实例或调用生命周期。
- 没有更改 Lua/Null ScriptEngine、Actor/Behaviour、Scene 或编辑器菜单。
- 没有执行 Editor/OvGame 场景、GPU、RenderThread、Play/Stop、序列化、
  重载、发布、性能或无 SDK 发布环境测试；这些属于 S1–S7。
- 没有运行远端 CI，没有 push。

提交前工作区中的 `RENDERING_ARCHITECTURE.md`、
`RENDERING_ARCHITECTURE_DIAGRAM.zh-CN.md`、`.zcode/`、
`CODEX_CHANGE_LIST_TEST.txt`、`Docs/`、`Examples/PerformanceLab/` 均为无关
改动，本次不修改、不暂存、不清理。
