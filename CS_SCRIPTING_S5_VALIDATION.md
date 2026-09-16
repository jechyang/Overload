# C# 接入 S5 实施与验收记录

日期：2026-09-16。起始 HEAD：`194e339`（S4）；核实父阶段 S3 `ef3ea8f`、
兼容构建入口 `c3ae12d`、S2 `3c9f9fa`。本轮只实施 S5，单独提交，不 push。

## 基线与保护

- 先核对 HEAD/工作区，保存原有 587 个无关文件的 SHA-256；复核没有改变。
  原有渲染文档、`.zcode/`、Docs、PerformanceLab、CODEX_CHANGE_LIST_TEST、
  SDK_COMPATIBILITY_VALIDATION 和 ResolveCSharpBuild 三文件均未纳入本轮提交。
- 最小阅读计划全局约束/S5、S4 验收及 `s4-f618Hg/verification.json` 的结论，
  按依赖阅读宿主、脚本门面、编辑器、构建/验收代码及 bootstrap 协议。
- H 盘开始仅余 325,816,320 字节；新证据通过进程环境变量
  `OV_SCRIPTING_EVIDENCE_ROOT` 放在本会话 C 盘可写目录，旧文件未删除。
- 指定基线 `verify_s4.mjs --python <指定 Python>` 完整通过：
  `Build/Tests/Scripting/s4-vLwnkc/verification.json`，包含 S1–S3、Lua、
  默认/RenderThread、真实 Editor/Game。没有再单独重复 S1–S3。
- PowerShell 本身仍报 CET，使用 CMD/Node。第一次沙箱基线 `s4-tf2n9G` 在
  MSBuild FileTracker 报 E_ACCESSDENIED；沙箱外重试通过，未关闭 FileTracker。
  Git 仅使用命令级 `-c safe.directory=H:/overload/Overload`。

## 实现与编辑器用法

- 新增 `C# Scripts` 面板，提供 New C# script、Debug/Release Build / Reload、
  源码监听开关、Build/待 Stop/已应用/失败状态以及完整编译诊断。
  Asset Browser 的 Create 菜单同时提供 Lua Script 与 C# Script。
- 首次新建 `.cs` 创建 `GameScripts.csproj`、SDK pin、显式 NuGet.Config 和
  package lock，引用正式 ScriptCore。源码位于 Assets，模板带稳定 ScriptId。
  不覆盖已有文件，不把文件名当作类型身份；编译后通过 Inspector > Add Component
  中的 `C# / 完整类型名 [程序集]` 选择类型。`.cs` 的资源识别/导入/双击编辑已接入，
  Lua 仍按原文件工作流创建、选择、解释、挂载。
- 编辑器内 `CSharpProject` 只在 Main 调度。使用绝对 Node 路径、参数数组的
  Windows quoting 和 CreateProcessW 启动隐藏后台进程；进程树由 Job Object
  持有，编辑器关闭时结束构建。Node 对 Python、dotnet 的启动也使用绝对路径和
  参数数组，不拼 shell 命令，不改全局 PATH。
- `BuildGameScripts.mjs` 先运行 R6 `reflection.py ensure --config`，再通过
  现有显式 `BuildCSharp.mjs --compat` 完整 MSBuild 构建 ManagedHost/ScriptCore
  和 GameScripts。按规范化项目路径哈希/配置隔离缓存；host/game 的中间目录
  进一步分离，避免 MSBuild 文件跟踪清理另一个目标的依赖。
- 每次构建写独立 staging；检查必要 DLL/deps/runtimeconfig，收集整个输出树
  （含项目依赖、PDB、卫星资源等）的 SHA-256/尺寸清单，完成后原子改名成一代产物。
  编译或源码稳定性检查失败不产生成功收据，不改已应用程序集路径。
- 无变化请求仍运行完整 MSBuild 依赖图检查，随后比较完整输出内容；相同内容且
  上一代文件校验通过时返回上一代，不重复切换会话。项目/配置写锁协调独立进程，
  编辑器内请求合并为正在构建之后的最新配置请求。
- Editor::PreUpdate 在 Main 生命周期安全点检查候选。只有 EDIT（停止态）可以
  应用；Play、Pause、单步均保持原会话。应用前再次检查完整清单/每个依赖 SHA-256，
  再调用 S4 的 LoadCSharpProject 元数据预检及原有停止态替换路径；失败保留旧
  目录、类型清单及附件，仍可 Play。成功后刷新 Inspector 并保存项目程序集设置，
  重新打开可加载已应用版本。
- 文件监听为 250ms 轮询、600ms 稳定窗口，监测源码/项目/props/targets/锁文件；
  排除 bin、obj、.overload、Build、generations、artifacts、构建缓存和目录链接。
  变化发生在构建中时，过期候选不应用，稳定后调度后续构建。

开发工具路径可以在 `.ovproject` 中设置：`csharp_engine_root`（源码根目录）、
`csharp_node`、`csharp_python`（绝对可执行文件路径）、`csharp_build_cache`。
未设置时从编辑器位置向上查源码根，Node/Python 查找后固定为绝对路径；找不到时
显示明确诊断。默认缓存为项目 `.overload/csharp`。H 盘空间不足时应把缓存设置到
有空间的目录。SDK/runtime/目标固定为 10.0.401/10.0.12/net10.0。

## 验收入口与证据

```text
node Tests/Scripting/Integration/verify_s5.mjs --python C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe
```

该入口包含 S1–S4 完整回归及 S5 真实编辑器应用验收，不用打印 PASS 代替状态断言。
所有进程带超时，测试窗口隐藏。编辑器夹具链接生产 Editor/Context/Inspector/
EditorActions 对象，仅隔离测试 profile 与项目资源；通过生产控件事件选择类型、
编辑字段，使用真实后台编译、Host、Main 安全点及 Play/Stop。两种渲染模式分别
在中文/空格新项目中执行，随后另起编辑器进程重新打开场景并运行。

最终入口 **exit 0**，固定源码收据：
`Build/Tests/Scripting/s5-MuAls9/verification.json`，UTC `2026-09-16T04:25:31.266Z`，
stage=S5、success=true。记录 77 条外层命令及 191 个源文件 SHA-256；实际构建
子进程另外保留 UUID 日志和完整代清单。结束后再次核对全部源码哈希，没有漂移。
`apps-s5/workflow-verification.json` 记录两种模式及两个独立并发进程的相同无变化
代收据；`s5-final-audit.json` 记录源码、冻结协议/工具链、R6 生成物的最终核对。

| 验收 | 最终结果 |
|---|---|
| 标准 dotnet build，强制 Rebuild | CET，exit 1，**未通过**；standard_dotnet_build_passed=false |
| 显式兼容完整 MSBuild | 生产/验收宿主重建和全部项目构建通过；compatibility_msbuild_passed=true |
| 工具链 | 私有 SDK 10.0.401 / runtime 10.0.12 / net10.0；Windows 10.0.22000 x64；MSVC 14.42.34433 |
| S1/S2/Lua/Null | 宿主、依赖/架构/配置负例、类型身份、Lua/Null 门面全部通过 |
| S3/S4 | 生命周期、安全点、精确字段/存档/引用/Inspector、真实 Editor/Game 默认/RenderThread 全部通过 |
| 新建与类型选择 | 两模式实际编译生成模板；同一文件两个类型，生产 C# Build 按钮事件提交构建，Inspector Picker 明确选择第二个类型，未挂载第一个 |
| 编辑器闭环 | 真实字段控件改值→场景保存→Play 的 Awake 读到覆盖值→Transform 可观察变化→Stop 恢复编辑值；另起进程重新打开场景可再次运行；Lua 同场景更新 |
| 停止态应用 | Play 中成功构建待应用、Pause 继续等待；Stop 后 Main 安全点才切换，非 Main 调用被拒绝 |
| 失败保留旧版 | 语法错误给出 .cs 文件/行列；等长损坏的依赖被 SHA-256 拒绝；编译成功但 ScriptId 冲突预检失败；旧代和附件仍可读/Play |
| 路径/监听 | 中文与空格新项目、源目录/文件、项目内自定义缓存及项目外缓存通过；源码防抖触发构建，obj 噪声不触发，输出不会造成循环 |
| 并发/配置/无变化 | 构建中多次请求合并到最新 Release；两个独立 worker 协调同项目/配置，返回相同成功代；无变化仍执行 MSBuild 并保持已加载代 |
| 原生无修改构建 | exit 0；obj 哈希和 mtime 不变 |
| 工作区/冻结协议 | 原有 587 文件不变；Host ABI/bootstrap、SDK 锁和 R6 生成物与基线相同 |

最终真实编辑器验收 EXE SHA-256：
`f8f12c0f4581f8a2f7d856b16ceed5e82ae6e0878f8fd3feb2be6655848589b1`。
两模式的 GameScripts/全部依赖的尺寸及 SHA-256 位于各自成功代的 manifest.json/xml。
本文件随 S5 独立提交；提交号可用 `git log -1 -- CS_SCRIPTING_S5_VALIDATION.md` 查询。

专项开发证据 `s5-dev-TEseCF`：默认/RenderThread 的新建→编译→选第二类型→挂载→
字段→存档→Play/Stop→重新打开、语法错误保留旧版、配置合并、无变化、文件监听及
跨进程并发通过。后续增加了完整清单哈希负例、类型冲突和完整 MSBuild 无变化判断，
`s5-dev-x4S5qM` 进一步通过产物哈希及类型冲突负例；最终结果以上述固定源码完整
收据为准，没有将开发轮次片段拼接为最终通过。

## 已发现并修正的问题

- 首轮原生编译暴露 Windows 精简头文件所需的 objbase/wincrypt 显式 include，
  以及仓库 tinyxml2 没有 Unsigned64Attribute；修正后真实原生构建通过。
- 验收新项目最初缺少既有必填设置，触发 Context 重置配置；夹具补齐原有字段，
  没有修改应用的配置重置规则来绕过测试。
- `s5-dev-77Ih8g` 发现两个不同 OutputPath 共用中间目录，后构建会清理先前 host
  的 ScriptCore，造成首次宿主启动失败。隔离 host/game 中间目录，并把 resident
  host 的依赖也加入必要产物检查；没有把这次失败计为通过。
- 审查发现项目内自定义缓存目录需同时从原生监听和 Node 输入清单排除；修正
  Node 的规范化项目输出根排除，并在默认模式使用项目内中文/空格自定义缓存，
  RenderThread 使用项目外缓存。`s5-PkmXAu` 是为这项修正主动中止的完整回归，
  保留其日志，不计为通过；随后从头执行固定源码最终入口。

## 范围与限制

- 标准 dotnet build 的 CET 失败与显式兼容成功分别记录。未更新 Windows、关闭
  CET、修改 SDK 二进制或全局 PATH、降级框架。
- S5 的产物代是构建输出事务，复用 S2/S4 停止态项目关闭/加载及编辑字段恢复。
  没有实现运行中热重载、字段迁移钩子、ALC 可回收证明、S6 的 20 次热重载矩阵，
  也没有实现 S7 发布、无 SDK 运行环境或性能验收。
- UI 验收为真实控件/事件与生产编辑器循环驱动，不冒称人工鼠标验收。
- 构建缓存/失败日志/历史代保留，不自动清理。若进程被强制终止留下写锁，后续
  构建明确报告锁文件与恢复提示；确认相关编辑器/构建全部退出后将旧锁移走。
  不由并发等待者自动删除锁，避免误删新写者的锁。
