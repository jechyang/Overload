# C# 接入 S3 实施与验收记录

日期：2026-09-14。起始 HEAD：`c3ae12d`；已核对 S2 提交 `3c9f9fa` 为祖先。
本文件随 S3 实现独立提交，提交号可用 `git log -1 -- CS_SCRIPTING_S3_VALIDATION.md`
查询。不 push，不实施 S4–S7。

S3 固定源码完整验收已通过；标准构建 CET 失败与显式兼容 MSBuild 成功分别记录。

## 基线与工作区保留

实施前阅读计划、S0/S1/S2、MSBuild 兼容验收、Host ABI、bootstrap 服务和 S1/S2
验收器。HEAD 为 `c3ae12d`，S2 无需重复实现。保存已有 587 个文件的 SHA-256，
最后逐个复核；不 reset/clean，不改动已有诊断脚本、Docs、PerformanceLab 或渲染文档。

先行运行：

```text
node Scripts/Windows/InstallCSharpToolchain.mjs --verify-only
node Tests/Scripting/Integration/verify_s2.mjs --compat-build --python C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe
```

SDK 校验 exit 0。正确兼容基线为 `Build/Tests/Scripting/s2-sPbXEs/verification.json`：
S1/S2 默认、RenderThread、Null、身份/XML/Lua/Prefab、原生无修改构建通过，完整
MSBuild 兼容重建成功；**标准 dotnet build 仍 exit 1，CET，未通过**。

先前 `s2-VO7xEc` 遇到沙箱 FileTracker E_ACCESSDENIED；自动审批允许的沙箱外执行
解决该访问问题。`s2-q57Bam` 使用了验收器不识别的 `--compat`，实际走原直接 csc.dll
分支，虽运行断言通过，但不把它记为兼容构建证据；随后完整补跑了正确入口。

## 实现范围

- 保留成功加载的项目 session/type tokens；编辑态仅 TypeReady，不执行用户构造。
  首次 Awake 经 Host ABI 创建实例，检查 Context/Owner GUID，执行字段初始化器，
  再注入 Owner。原始存档字段保持 opaque，未实现 S4 的托管字段恢复。
- ScriptCore 提供虚生命周期、Owner、Transform、GetComponent<Transform/Box>、
  Actor GUID/激活/查找/创建/销毁/增删 Behaviour/延迟场景加载，以及同步日志。
  不提供任意组件、裸地址或后台任务调度。异步生命周期在类型发现时拒绝。
- 固定 Host ABI 184 字节及 R6 ABI/签名保持不变。独立 ConfigureGameplay 适配器
  用 R6 Context/Guard/Get 校验 Actor 必有且不可独立删除的 Transform 句柄，再取得
  native Owner。Box 使用自己的组件 epoch；位置与尺寸读写仍走现有生成绑定。
  详细操作表见 `BOOTSTRAP_SERVICES.md`。
- 所有实例查询校验 instance ID + session generation。Awake/Start 一次，Enable/
  Disable 为状态变化；更新和碰撞要求 Started+Enabled。构造/回调异常只 Fault 当前
  实例，日志包含类型、Actor、回调与堆栈；Destroy 最多一次，即使抛异常仍清理引用。
  Behaviour 显示 TypeReady、InstanceReady 或 Faulted，不再把类型挂载描述为已执行。
- Actor 按挂载顺序对 Behaviour 的 key/weak epoch 和组件快照分派。回调删除自身、
  后面的 Behaviour、Actor，不使迭代器/引用跨删除继续使用；待删对象跳过剩余回调。
  Actor 在场景阶段/管理器安全点回收。新 Behaviour 的初始化排队，下一次更新前
  完成 Awake/Enable/Start。既有原生 CreateActor 返回引用，允许立即分配 native
  存储，但初始化延迟且新 Actor 不进入当前快照。
- PhysicsEngine 在整个 Bullet Update 持有同一回调屏障；结构修改和激活变化不会
  在 Bullet 回调内同步重入。Scene 的 Play、Update（普通/JobSystem）、Fixed/Late
  采用安全遍历。换场景先停止旧场景分派，再在现有 SceneManager::Update 替换。
  内存场景加载复制文档后延迟执行；文件加载失败恢复旧场景分派。
- Destroy 时 Owner/组件句柄仍有效；随后才失效 native epoch 和删除存储。关闭
  session 后缓存的托管门面拒绝访问，即使 native Actor 仍活着。构造期间 native
  API、工作线程与 RenderThread 访问均拒绝。销毁/停止中的 Owner 拒绝结构修改。
- Editor/Game 复用生产 Scene/Actor 及现有 EditorActions 模式，没有另建托管循环。
  真实应用运行发现 C# 配置路径被按 ACP 解析，已将 runtime/host/project assembly
  三项路径改为 UTF-8 到 C++20 path 的显式转换。

## 示例与完整验收入口

`Examples/CSharpGameplay/MoveAndResize.cs` 的真实 OnUpdate 将 local X 增加
`2 * deltaTime`、Box.Size.X 增加 `deltaTime`。独立 GameScripts.csproj、依赖锁和
无远端源 NuGet.Config 已入库；最小挂载方式见该目录 README。

```text
node Scripts/Windows/BuildCSharp.mjs --compat Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj --rebuild
node Scripts/Windows/BuildCSharp.mjs --compat Examples/CSharpGameplay/GameScripts.csproj --rebuild
node Tests/Scripting/Integration/verify_s3.mjs --python C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe
```

S3 入口强制使用 `--compat-build`，保留完整 S1/S2 回归，再重新编译包含同一示例源码
的 S3 项目和原生夹具。原生 EXE 启动前确认 CLR 未加载，由真实宿主启动 CLR；直接
读取 native Transform/Box 和 Lua 字段，并比较实际回调生成的事件文件。不是字符串
打印、静态扫描或旧 DLL 验收。每个子进程都有超时。

实际应用夹具链接生产 Context、Editor、EditorActions、Game、Scene/Actor 对象，
唯一复制改动是 SystemCalls 的 AppData 查询，指向忽略目录下的测试 profile，以免
修改用户的布局/设置。没有替换生命周期、游戏循环或使用测试版调度。资源通过指向
仓库 Resources 的目录联接加载；窗口创建后隐藏。应用运行不等同人工操作菜单验收。

## 最终结果

完整入口 exit 0；收据：`Build/Tests/Scripting/s3-MX7SMl/verification.json`，
UTC `2026-09-14T12:07:26.154Z`，`stage=S3`、`success=true`。
源码/生成文件前后哈希检查、托管确定性重建、原生无修改构建通过。各命令、工作目录、
退出码、源码与产物 SHA-256 均在收据内；原始日志及事件文件保存在同目录。

| 验收项 | 真实结果与证据 |
|---|---|
| 原版标准 dotnet build | exit 1；`dotnet-build.log` 明确报告 Windows 不完全支持 CET，**未通过** |
| 显式兼容完整 MSBuild | production、acceptance、确定性重复重建、S2 各项目、独立示例、S3 项目全部 exit 0；`compatibility_msbuild_passed=true` |
| S1/S2 回归 | 原生启动、默认/RenderThread、Null、身份/XML/Lua/Prefab 断言通过；六项缺依赖/配置/架构负例按预期 exit 1 |
| S3 真实场景，两种模式 | `s3-default` / `s3-render-thread` exit 0；字段初始化器与 Owner 先于 Awake，Awake/Enable/Start 顺序及去重，Fixed/Update/Late，Actor 父子激活、销毁均经事件和原生状态断言 |
| 动态修改与异常 | 自删 Behaviour、删后续 Behaviour、增添 Behaviour/Actor、删除 Actor、Awake 自删、更新内禁用、碰撞回调内销毁、换场景停止旧分派通过；构造及 Update 异常隔离，Destroy 抛异常仍只清理一次 |
| 真实 API 与 Lua 共存 | 同一示例源码实际移动 Transform、修改 Box.Size；读取 native 数值和 Lua ticks；父子世界矩阵、Bullet 射线命中、碰撞/触发 enter/stay/exit 均通过 |
| 有效期及线程 | 错误 Context、旧 instance generation、构造期/worker/RenderThread API 拒绝；删除/换场景后的缓存门面拒绝；`s3-closed-facades` exit 0，Box 删除再新增不复活旧 epoch，session 关闭后 native 对象仍存活时缓存 Actor/Box 也拒绝 |
| 实际 Editor，两种模式 | `s3-OvEditor-default` / `s3-OvEditor-render-thread` exit 0；编辑态不执行、Play 更新、Pause 不前进、单步恰好一帧且自动暂停、Stop 恢复原生快照及挂载、第二次 Play 创建新实例 |
| 实际 Game，两种模式 | `s3-OvGame-default` / `s3-OvGame-render-thread` exit 0；生产 PreUpdate/Update/PostUpdate 驱动 C#，Lua/C# 同场景执行 |
| 原有工作区 | `node Build/s3-preserve.cjs --check` exit 0，587 个原有文件内容全部保持 |

本轮新编译、实际运行产物的 SHA-256：

```text
s3.exe                  84bfc47b7d9e44ad431333d464e8ab1042da4f2e6a356d77d1c13b651dfa0965
project 空格/S3.dll     99c161dbe477e485192691300141942ba92fbd7599e82839a5f23b0833ee350b
example/GameScripts.dll 4b996da5d971d808707d58139d402dcdeb47bc95c5298dc9932c9cc9cc34d45e
apps/OvEditor-s3.exe     80c2427986bd6d38240f57e57dae654945985f362cffdeb3ae1c7da45394bdbd
apps/OvGame-s3.exe       3f5d7551f8a9ce3d47ffd29d9ae0e244016bd61893e1929d3d32a227e558da23
```

独立示例项目已兼容重建；应用夹具加载的是包含相同 MoveAndResize 源码与额外断言
Behaviour 的新 S3.dll。acceptance 专用缓存门面探针不进入生产 ManagedHost，生产
宿主检查已验证这些入口不存在。预期故障脚本的异常日志不是整个验收失败。

## 开发过程中的失败与修正

- `s2-RH6U9o` 的原生/S1/S2 运行断言通过，但期间修改源码，末尾哈希收据拒绝；
  不作为最终通过证据。
- `s3-6YVors` 原生夹具因 `_wgetenv` /WX 失败，改用 Windows 环境变量 API。
  同目录 `dev-*` 为显式开发重编/运行日志，没有最终验收收据。
- 首轮托管编译发现用户 Vector3 与生成绑定的未限定名称冲突，改为
  `Overload.Maths.Vector3`，保留 R6 原类型和布局。
- 应用夹具补齐 Material 头与生产 Application 对象；隔离 SystemCalls 按原工具
  模块使用非 Unicode Win32 宏编译，未改变生产模块选项。
- 真实 Editor 暴露中文程序集配置路径 ACP 问题，已修复；另外夹具误以为 Box 默认
  尺寸为 1（实际 0.5），改为显式初始化，不改引擎默认值。
- 物理射线用模拟后的对象位置，避免假设动态对象仍位于初始位置。
- `s3-gmKM6p` 的碰撞内销毁夹具重用已运动物体，未再次形成接触而失败；改用新的
  重叠碰撞对后重新编译运行，最终整轮 `s3-MX7SMl` 两种模式均通过。

## 阶段边界与限制

- SDK 10.0.401、runtime 10.0.12、net10.0 未变。标准构建 CET 失败未解除；没有
  Windows 更新、关闭 CET、SDK 二进制修改、全局 PATH 修改或降级。
- S4–S7 未实现：托管字段 Inspector/编辑保存/引用重映射、脚本项目 UI/类型选择器、
  build/reload 工作流、shadow-copy/热重载迁移/ALC 回收证明、发布与性能矩阵均不在本轮。
  现有 C# 原始 XML 数据保留不等同字段已恢复；Editor Stop 仅验证原生场景快照恢复。
- 碰撞只暴露可校验的对方 Actor GUID；不提供完整接触点/法线数据。最小组件集只含
  Transform/Box；没有单独 Behaviour.enabled 属性，启停遵循既有 Actor 层级激活。
- 本机 Debug、真实 RTX 3070 Ti/OpenGL 4.5；不声称完整 R0–R6、Release/Publish、
  无 SDK 主机、远端 CI、性能基准或任意脚本后台任务均已通过。
- 没有 push；提交只包含本轮 S3 代码、示例、验收和协议/计划说明。
