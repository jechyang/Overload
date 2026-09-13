# C# 游戏脚本接入 S1 实施与验收记录

日期：2026-09-13。基线和验证时 HEAD：`80ef9de`（S0）。
本文件与 S1 代码同一提交；提交号可用 `git log -1 -- CS_SCRIPTING_S1_VALIDATION.md`
查询。本轮只实现 S1，没有执行 S2–S7，也没有 push。

**S1 原生宿主及下述本地验收完成；标准 `dotnet build` 仍因 CET 失败。**
不能把直接调用 SDK 编译器、原生启动 CLR 或验收器 exit 0 解释为标准构建通过。

## S0 先行复验

在改动 S1 源码之前执行：

```text
node Scripts/Windows/InstallCSharpToolchain.mjs --verify-only
node Tests/Scripting/Integration/verify_s0.mjs
```

两条入口均 exit 0；实际证据是
`Build/Tests/Scripting/s0-p5sqsq/verification.json`。其中：

- 私有 SDK/归档、版本锁、Host ABI 签名、纯 C/C++ 布局、缺依赖和 x86 负例通过。
- locked restore、SDK 10.0.401 的 `csc.dll` 实编、重复编译确定性、10.0.12 x64
  实际执行通过。
- **`dotnet-build` exit 1，未通过**，`external_blockers` 非空，仍是：

```text
Fatal error.
Your Windows doesn't fully support CET. Please install all available Windows updates.
```

系统仍为 Windows 10.0.22000 系列（S0 记录补丁号 318），没有证据表明已经完成
所需更新。本轮没有修改 SDK 二进制、关闭 CET、降级 SDK/runtime 或安装系统更新。
更新并重启后必须重跑；只有标准构建实际 exit 0 才能改记通过。

## 实现范围

- `RuntimeHost` 使用原生 nethost 的 `get_hostfxr_path`、hostfxr 的
  `initialize_for_runtime_config` / `get_runtime_delegate`，取得
  `load_assembly_and_get_function_pointer` 并调用常驻 ManagedHost。
  使用真实 EXE 路径、明确的私有 runtime root、runtimeconfig 和 DLL 路径。
  相对配置以 EXE 目录解析；不搜索 cwd/PATH、不下载 SDK、不默默 roll-forward。
- 加载前验证文件/x64 PE；nethost 返回的 hostfxr 及实际加载的 CoreCLR 必须位于
  指定的 10.0.12 目录。已存在不同 CLR 时拒绝接入。hostfxr 错误 writer/handle
  使用作用域清理；native 模块与 CLR 保持进程常驻。
- Host ABI 1 的 184 字节完整表，版本/大小/32 字节签名精确握手；失败清零完整
  输出、成功后 native 再检查签名和所有 18 个入口。S0 JSON/header 未修改。
- `BootstrapABI.h` 的独立 32 字节服务配置传递 native-owned R6 Context ID、Main
  判断和同步 UTF-8 日志回调。它不更改 Host ABI 或 R6 ABI 签名。
- 空项目会话使用单调 ID/generation，支持同一已关闭句柄重复关闭，拒绝过期代次、
  工作线程和重入。错误按线程/会话读取并消费，owned UTF-8 验证归属、释放后清零。
- 绑定 OvCore 时先检查调用方元数据，再构造真实生成的 R6 Api，让其调用 native
  entry 执行独立握手。生成器仅新增供宿主使用的 native 签名宏和 managed TableSize
  常量；R6 签名、表顺序和操作语义没有改变。两侧使用同一个原生链接映像中的
  OvReflection Context 注册表。
- Editor/Game 默认不初始化 C#；启用时在 ServiceLocator 提供必要服务后创建宿主
  与空会话。初始化失败记录诊断并保留 Lua/Null 服务。配置和构建方法见
  `Tools/OvCSharp/Schema/BOOTSTRAP_SERVICES.md`。
- Context 显式销毁顺序：关闭 managed 会话/清除绑定 → Detach 回调 → 删除 R6
  Context，使 context ID 失效 → 卸载 native scene → 卸载资源 → 其余服务。
  host 成员最后声明，也覆盖构造异常时的成员逆序清理。`hostfxr_close` 不冒充 CLR
  卸载；原生宿主可以在同一 Main 上重新接入常驻 CLR。

S1 只接受空 assembly_path、flags=0、project_generation=0（由宿主分配 generation）
的 bootstrap 会话，用户类型计数为零。后续 ABI 槽位有明确失败返回；没有加载用户
程序集，没有 Behaviour、字段或生命周期实现，也没有 collectible ALC/热重载。

## 最终固定源码验收

入口：

```text
node Tests/Scripting/Integration/verify_s1.mjs --python C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe
```

最终证据：`Build/Tests/Scripting/s1-hWT1Li/verification.json`。
记录了 21 条命令的完整 argv/exit code、30 个源文件的 SHA-256、生成文件签名收据
及实际二进制哈希；结束前验证源码未变。每个子进程有超时，GL 测试窗口隐藏。

| 项目 | 实测结果 |
|---|---|
| SDK / runtime | 10.0.401 / Microsoft.NETCore.App 10.0.12，Windows x64 |
| 原生工具链 | VS2022 MSVC 14.42.34433；Windows SDK 10.0.22621.0 |
| 实际 GPU | NVIDIA GeForce RTX 3070 Ti；OpenGL 4.5，驱动字符串 616.92 |
| 完整 Debug solution（含 OvEditor/OvGame） | exit 0 |
| 原生无修改再构建 | exit 0；全部 Debug `.obj` 哈希和 mtime 不变 |
| ManagedHost locked restore | exit 0，无远端包源 |
| 标准 ManagedHost `dotnet build --no-restore` | **exit 1：CET，未通过** |
| SDK csc.dll 编译生产 DLL / 验收 DLL | 分别 exit 0；同一验收响应文件重复编译哈希不变 |
| 生产 DLL，由 native EXE 启动和绑定 R6 | exit 0；确认生产 DLL 不含测试 Smoke 入口 |
| 默认渲染模式 native→CLR→ManagedHost→R6 smoke | exit 0 |
| RenderThread 模式同一 smoke | exit 0；RenderThread 调用被拒绝 |

核心运行证据来自 `native-host.exe`：启动前检查 `coreclr.dll` 尚未加载，随后由
该进程的原生宿主主动启动 CLR。测试把真实 Scene/Actor 的 `CPhysicalBox` 与
`CTransform` 经 `PublishComponent` 发布给其 R6 Context；managed Smoke 调用该
会话内的生成包装，native 再直接读取组件，确认 Box.Size 为 `(3,4,5)`、Transform
位置为 `(6,7,8)`。不是检查打印字符串代替组件值，也不是 dotnet 启动托管 EXE。

验收入口只通过 `S1_ACCEPTANCE` 编译符号加入同一 Bootstrap 类，使用生产的
session/binding/异常 guard；生产 DLL 单独编译并单独用 native EXE 验证。
没有添加项目脚本调度来完成该 smoke。

最终二进制 SHA-256：

```text
native EXE:    95c560a0e060cef64049d8c90c67a029a25cf31b43b12596d23c309a54e8f891
验收 DLL:      9ae3119494526cafc1aa57791843626185e83fd421f7e417bd7796c50edbdab1
生产 DLL:      220e42f0c6951e1c652fe637cce4d89233e4fc9dd0dfd9e18e5a32966d513660
```

## 失败矩阵与生命周期边界验证

| 场景 | 预期 / 实际 |
|---|---|
| runtimeconfig 缺失 | native exit 1，包含缺失绝对路径 |
| ManagedHost DLL 缺失 | native exit 1，包含缺失绝对路径 |
| nethost DLL 缺失 | native exit 1，包含缺失绝对路径 |
| nethost / ManagedHost 错误架构 | 各 exit 1，明确要求 x64 PE 0x8664 |
| runtimeconfig JSON 损坏 | native exit 1，hostfxr 初始化状态码及原始诊断 |
| Host ABI version / size / signature 错误 | 各返回 AbiMismatch；预填 0xcc 的完整表清零，无入口发布 |
| R6 version / size / signature 错误 | 各返回 NativeAbiMismatch，未发布绑定 |
| 元数据匹配但 native R6 entry 拒绝 | NativeAbiMismatch；断言该 entry 确实被托管包装调用一次 |
| native host / managed entry 的 worker 调用 | 分别拒绝；不关闭 Main 的会话 |
| RenderThread 调用托管入口 | WrongThread |
| 托管主动抛异常 | ManagedException；native 日志含操作/会话/消息/堆栈；不跨 ABI 展开 |
| 错误读取与 owned buffer | 读取消费错误；释放清零；复制后重复释放返回 BufferOwnership |
| 删除真实 Box 后继续使用其句柄 | R6 拒绝失效句柄，异常经 Host 状态返回 |
| 关闭会话后调用 / 错误 generation | SessionNotFound |
| 同一已关闭句柄再次关闭 | Success |
| 连续 25 次创建/绑定/关闭 | 全部成功，ID 单调且不复用（两种模式分别执行） |
| Shutdown 后 native Context ID | InvalidContext；此时 native Actor/Transform 仍存在 |
| Shutdown 后 CLR / 再次 Start | CLR 仍常驻；同 Main 重新接入与绑定成功 |

开发过程中的非最终失败也保留在 Build：`s1-0KPd2k` 是沙箱 MSBuild FileTracker
E_ACCESSDENIED，提升权限后解决；`s1-Uf37ij` / `s1-Liv5D2` 是夹具编译诊断，修复后
重编；`s1-TBCZsH` 是较早通过的原生运行。它们不替代最终固定源码收据。

## 未验证项及下一阶段入口

- 标准 dotnet build 的 CET 系统前提仍未恢复；更新系统后重跑 S0 和 S1，并要求
  `dotnet-build` exit 0、`standard_dotnet_build_passed=true`、`external_blockers=[]`。
- Editor/Game 已完整编译并接入可选初始化/销毁；本次没有交互启动两个完整应用
  去执行项目设置 UI、Play/Stop 或游戏循环。真实组件/GPU 运行使用上述原生夹具。
- 没有重跑完整 R0–R6 矩阵，没有声称之前未完成的 R6 复查通过；本轮直接运行了
  S1 依赖的真实 R6 组件写入、线程和失效句柄路径。
- 没有执行 Release/Publish 三配置、无 SDK 发布主机或远端 CI；它们属于后续验收。
- 下一阶段是 S2 的后端门面与用户类型身份/发现；不能把空会话视为 C# Behaviour
  已可挂载和运行。S2–S7 需要新的明确执行请求。

工作区的 `RENDERING_ARCHITECTURE.md`、`RENDERING_ARCHITECTURE_DIAGRAM.zh-CN.md`、
`.zcode/`、`CODEX_CHANGE_LIST_TEST.txt`、`Docs/`、`Examples/PerformanceLab/` 保留原样，
不纳入本次提交。SDK、构建产物和本地日志均不提交。
