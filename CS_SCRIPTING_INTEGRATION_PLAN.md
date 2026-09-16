# C# 游戏脚本接入计划 v1

日期：2026-09-13。仓库：H:/overload/Overload。
基线：c59edf3（R0–R6 原生反射与 C# 绑定实现）。
状态：S0 已按 [S0 验收记录](CS_SCRIPTING_S0_VALIDATION.md) 冻结协议并完成
实际编译运行 smoke；S1 已按 [S1 验收记录](CS_SCRIPTING_S1_VALIDATION.md) 完成
原生宿主、常驻引导与真实组件 smoke；S2 已按 [S2 验收记录](CS_SCRIPTING_S2_VALIDATION.md)
完成后端门面、类型身份与缺失记录保留；S3 已按 [S3 验收记录](CS_SCRIPTING_S3_VALIDATION.md)
完成托管实例、受控 API、生命周期和真实 Editor/Game 默认/RenderThread 运行验收。
S4 已实现托管字段、Inspector、场景/Prefab 存档与引用恢复，详见
[S4 验收记录](CS_SCRIPTING_S4_VALIDATION.md)，固定源码完整复验通过。
S5 已完成项目模板、后台构建、类型挂载与停止态应用，详见
[S5 验收记录](CS_SCRIPTING_S5_VALIDATION.md)，S5 与 S1–S4 完整回归通过。S6 已完成可回收项目代、有限字段热重载与固定源码验收，
详见 [S6 验收记录](CS_SCRIPTING_S6_VALIDATION.md)。S7 未实施。
当前 Windows 的标准 `dotnet build` 仍因 CET 失败；显式兼容入口的
完整 MSBuild 重建与新程序集真实宿主运行通过，二者分别记录，不能混称标准构建通过。

## 1. 目标、范围与交付层次

目标：用户在项目内编写 C# Behaviour，构建后挂到 Actor，在编辑器 Play 和独立 OvGame 中按现有场景生命周期执行；字段可以编辑与保存，脚本可安全重载，发布无需 SDK。

首轮保留 Lua，通过统一脚本入口选择后端；不同时进行 Lua 删除或全引擎 API 绑定。沿用 R6 的 C ABI、Main 线程、弱有效期句柄与签名握手，不另建无检查裸指针桥。

- S0–S3：最小运行闭环。程序化挂载 C# Behaviour，在真实场景 Update 中修改 Transform/Box。
- S4–S5：编辑器可用闭环。新建、编译、选择类型、挂载、编辑字段、存档与 Play/Stop。
- S6：编辑态构建重载与运行态有限字段迁移。
- S7：发布、性能及全链路验收。

第一版不包含任意对象图迁移、async/coroutine 调度、用户原生插件热卸载、所有引擎 API、跨平台、NativeAOT 或不可信脚本沙箱。

## 2. 已有基础与必须补齐的断点

| 位置 | 已有事实 | 接入影响 |
|---|---|---|
| Sources/OvReflection 与 Tools/OvReflectGen | Script 标注生成 C ABI/C# 包装，数值/UTF-8/数学类型、异常、线程和有效期校验 | 直接复用；新增接口仍经受控协议发布 |
| Sources/OvCore/include/OvCore/Scripting/ScriptBridge.h | PublishComponent 使用组件 InspectorTicket | 仅覆盖组件；Actor 发布、场景作用域、托管实例身份还需设计 |
| Sources/OvCore/include/OvCore/Scripting/ScriptEngine.h | LUA_SCRIPTING 编译期选择 LuaScript/LuaScriptEngine，否则 Null | 不能只增加 CSharp 类而不改调用入口 |
| Scripting/Common/TScript.h、TScriptEngine.h | 模板后端接口已有生命周期、属性、项目生成入口 | 可以保留 Lua 实现，用后端适配器包裹 |
| ECS/Components/Behaviour.h/.cpp | 拥有唯一 Script；构造注册、析构注销；已有字段 Inspector/XML | 迁移为后端无关实例；处理创建失败、重载和销毁中间状态 |
| ECS/Actor.cpp、SceneSystem/Scene.cpp | 行为挂载按字符串键，生命周期由原生场景触发 | 类型身份不能直接等同 .cs 文件名；防止回调内增删使遍历失效 |
| Scripting/Common/ScriptPropertyValue.h | bool/double/string/AssetRef/ActorRef | C# long/ulong 不能压成 double；需扩展类型描述和值协议 |
| OvEditor/OvGame 的 Core/Context.cpp | 创建 ScriptEngine 并注册 ServiceLocator | 两个应用都要接宿主；服务可用后再初始化用户实例；显式安排销毁顺序 |
| OvEditor/Core/EditorActions.cpp、Panels/Inspector.cpp、OvTools/Utils/PathParser.cpp | 多处硬编码 Lua 路径、模板、过滤器、重载 | 需要资源识别、类型选择和构建体验接入 |

特别注意：当前 Behaviour::SetScript 只保留仍存在且 variant 相同的字段，OnDeserialize 跳过未知字段，OnSerialize 依赖有效脚本。这些行为不能直接作为 C# 丢失类型/编译失败时的存档策略。Actor 的行为显示顺序与实际遍历顺序也要分别核实，不凭 UI 顺序声明生命周期顺序。

## 3. 固定架构决策

### 3.1 工具链与宿主

新接入默认目标 Windows x64、.NET 10 LTS。按 2026-09-13 官方支持政策，.NET 10 支持至 2028 年 11 月；执行 S0 时固定一个实际可用的 SDK 补丁版本及校验值。原生继续用现有 VS2022/MSVC/LLVM；托管项目用独立 dotnet SDK CLI 构建，不要求 VS2022 加载 net10 项目。[官方支持政策](https://dotnet.microsoft.com/en-us/platform/support/policy)

上轮本机只有 CoreCLR 6.0.15，没有 .NET SDK；R6 的 net8 示例未执行 dotnet build。S0 必须重新检测，不能把旧 CLR 测试冒充新目标验证。SDK 优先放在仓库忽略的 Build/Toolchains 下，使用明确路径，不修改全局 PATH。网络或运行环境不可用时记录具体阻塞，不偷偷降级目标。

原生通过 nethost/hostfxr 启动常驻托管引导程序集，取得固定 C 调用入口。进程保持一个 CoreCLR；项目关闭与 Play/Stop 只重置项目/场景会话。hostfxr_close 关闭宿主句柄，不把它当作卸载整个 CLR。[官方宿主示例](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting)

### 3.2 程序集边界与所有权

建议落点（新建前检查现有目录，名称可按仓库规范微调）：

- Sources/OvCore/Scripting/CSharp 对应 include/src：宿主、后端适配、项目会话和入口表。
- Sources/OvManaged/Overload.ManagedHost：常驻引导、会话路由、异常转换。
- Sources/OvManaged/Overload.ScriptCore：Behaviour、Actor/组件门面、字段标注及共享类型。
- 复用 Examples/ReflectionScript 的原生包装项目，调整目标并接入正式构建。
- 项目 GameScripts 程序集：在 collectible AssemblyLoadContext 内加载。
- Tests/Scripting、Examples/CSharpGameplay：行为回归与最小可玩示例。

ScriptCore、生成的 NativeApi 和 ManagedHost 共享在常驻上下文中；用户程序集及其项目依赖由项目 ALC 管理，避免加载第二份 ScriptCore 造成类型身份不相等。常驻 native 仅保存固定宿主函数指针和数值实例 ID；用户对象强引用在对应项目会话内，销毁后清除，不在原生反射注册表缓存用户委托、Type 或 Assembly。

区分三种身份：持久化 ScriptId、托管实例 ID+会话代次、原生 Context/对象句柄。不能把 GCHandle 值、地址或 ALC 实例 ID 写入场景。Actor GUID 也不替代有作用域/有效期的运行时句柄。

### 3.3 后端与用户 API

采用后端无关 ScriptEngine 门面和脚本实例接口，封装现有 Lua/Null 并新增 CSharp 后端；按 ScriptIdentity 分发，Lua 与 C# 可在同一项目共存。先用 Lua 回归证明门面改造不改变现有行为，不全面重写 Lua 绑定。

C# 用户继承 Overload.Behaviour，使用 OnAwake、OnStart、OnEnable、OnDisable、OnUpdate(float)、OnFixedUpdate(float)、OnLateUpdate(float)、OnDestroy 等名称，与原生入口一致。生命周期通过已创建实例的虚方法或缓存调用入口分派，不每帧扫描反射或按字符串查找方法。由现有 Scene/Actor 主线程阶段驱动，不创建另一套独立游戏循环。

Owner、Transform、GetComponent<T> 第一版只覆盖已支持类型。Actor 桥采用场景作用域的受控查找/发布；必要时添加专用生命周期适配，不为绕过 R6 检查暴露 Actor*。组件属性/方法仍调用生成包装。提供日志和 deltaTime 即可支撑首个运动示例；Input/资源/物理复杂接口按实际示例单独扩展。

### 3.4 失败、重入和编辑态

所有 native→managed 入口都捕获托管异常并返回状态；managed→native 继续使用 R6 状态协议。记录脚本类型、Actor、回调和堆栈。第一版运行期回调异常使该实例进入 Faulted，停止其后续更新，其他实例继续；编辑器可通过重新创建/重载恢复。销毁清理最多执行一次，即使用户清理抛异常也必须释放宿主持有的引用。

回调中创建/删除 Actor、增删 Behaviour、切换场景，统一在既有或新增的主线程安全点应用；待删除对象停止后续分派，迭代器和引用不跨删除保留。明确 OnDestroy 与原生 epoch 失效的顺序；不能为允许销毁回调而延长悬空对象的可访问期。

编辑态不执行游戏生命周期。读取字段默认值若需要实例构造，构造阶段不得访问 Owner/native API；不额外构造临时对象来猜默认值，不调用任意属性 getter。第一版不支持用户 async void 生命周期和宿主调度的后台任务。

## 4. 分阶段执行与验收

### S0：冻结宿主协议与可复现工具链

状态：已完成 S0 交付并单独验收；固定值、实测证据、失败项和恢复步骤见
`CS_SCRIPTING_S0_VALIDATION.md`。本状态不授权或暗示 S1 已开始。

任务：

1. 核对基线、工作区、现有 R6 测试与 SDK/OS/x64 条件，固定 SDK/global.json、runtimeconfig、依赖锁定和下载校验。
2. 定义 Host ABI：版本/表大小/签名、创建关闭会话、类型查询、创建销毁实例、生命周期分派、字段读写、错误文本所有权；与 R6 Native API ABI 分开版本化。
3. 固定 ScriptIdentity：后端、项目程序集逻辑名称、规范类型名为默认身份，允许显式稳定 Id/旧身份别名；不使用带版本号的 AssemblyQualifiedName 作为持久主键。Lua 旧路径身份维持可读。
4. 记录生命周期状态机、重入安全点、销毁次序、字段首版支持矩阵和项目会话归属。
5. 增加独立工具链探测/托管构建 smoke，证明选定 SDK 能编译并运行最小项目；普通 Lua 构建不强制下载 C# 工具链。

验收：固定目标真实编译运行；依赖/位数缺失有可定位错误；协议文档与可执行 smoke 入库。
建议提交：docs(scripting): freeze S0 C# hosting and project contracts

### S1：原生进程启动 CoreCLR 与常驻引导

状态：已完成实现与本地原生验收；默认/RenderThread 真实组件 smoke、失败矩阵及
标准构建仍存在的 CET 失败详见 `CS_SCRIPTING_S1_VALIDATION.md`。不代表 S2–S7 完成。

任务：实现显式路径解析的 nethost/hostfxr 宿主、Host ABI 握手、Main 线程回调及日志；启动常驻 ManagedHost，将 R6 入口表和上下文提供给托管代码。接入 Editor/Game 的可选宿主初始化，等 ServiceLocator 和必要服务就绪后才创建项目会话。

验收：由原生测试进程主动启动 CLR，再由托管入口调用 R6 修改真实组件；覆盖 runtimeconfig/DLL 缺失、错误架构、Host ABI/R6 ABI 不匹配、托管异常及重复创建关闭会话。不能继续只用 dotnet 启动测试 EXE 代替宿主验收。
建议提交：feat(scripting): complete S1 CoreCLR host and managed bootstrap

### S2：统一脚本入口与项目类型身份

状态：已完成 S2 实现与真实本地验收，详见 `CS_SCRIPTING_S2_VALIDATION.md`。
CSharp 的 TypeReady 仅表示类型挂载；S2 当时尚未实现的实例构造、Owner 和游戏生命周期
现已在 S3 完成；S4 中类型挂载仍不构造，首次 Inspector/Play 准备实际挂载实例。
标准 dotnet build 的 CET 外部阻塞仍未解除。

任务：

1. 引入 ScriptEngine 门面/实例接口，适配 Lua/Null/CSharp，保留既有服务入口和项目资源路径语义。
2. 加载项目程序集，发现非抽象、非开放泛型且满足构造约束的 Behaviour 子类，校验重复 ScriptId 与别名。程序集逻辑身份/类型不从 .cs 文件名推断。
3. 将 Actor 的 Behaviour 键和存档扩展到 ScriptIdentity；保留旧 Lua API 兼容入口与旧 XML 读取。首版每 Actor/ScriptId 一个实例，重复添加不重复事件/顺序条目。
4. 类型缺失、后端关闭或编译失败时保留完整脚本记录与字段原始数据；显示缺失状态，不能静默抹掉后保存。
5. 调整构造即注册的流程，保证字段恢复、Owner 注入、实例创建与生命周期的先后顺序明确。

验收：原有 Lua 场景/Prefab/属性用例通过；Lua/C# 身份不冲突；同名类不同命名空间、一文件多个类、文件移动、显式 Id 重命名、缺程序集再恢复均符合协议；空实例序列化无崩溃。
建议提交：refactor(scripting): complete S2 backend dispatch and script identities

### S3：C# Behaviour 生命周期与最小引擎 API

状态：已完成本轮实现与独立本地验收，详见 `CS_SCRIPTING_S3_VALIDATION.md`。
固定源码收据 `Build/Tests/Scripting/s3-MX7SMl/verification.json` 为 success=true；
覆盖真实宿主、Editor Play/Pause/单步/Stop、Game 循环、Lua 共存和两种渲染模式。
S3 验收时 Awake 前完成 C# 字段初始化器及 Owner 注入；存档字段恢复由 S4 单独验收。
首版 Actor 存储沿用原生返回引用接口立即创建，初始化排队且不进入当前分派快照；
销毁、Behaviour 修改和场景替换在主线程安全点处理。S3 收据不包含 S4–S7。

任务：实现托管实例注册、Owner/Transform/GetComponent 受控门面，将现有生命周期与碰撞/触发入口接到 C#；碰撞对象先以可校验句柄和最小只读身份提供。建立实例状态与主线程安全点，处理自删、删其他行为、增添行为及换场景。旧组件句柄在场景切换、删除、会话关闭后拒绝访问。

首个示例：程序化挂载 MoveAndResize Behaviour，在 OnUpdate 中根据 deltaTime 移动 Actor，并经生成绑定修改 Box.Size；包含 Lua 与 C# 同场景。

验收：真实 Editor/Game 场景调度，Awake/Enable/Start、固定步与帧更新、暂停/单步、动态增删、禁用/启用、OnDestroy、场景更换有事件轨迹和可观察结果；字段/Owner 已准备后才进入 Awake；异常只隔离故障实例。物理命中和层级矩阵正确，默认渲染及 RenderThread 模式均通过。没有先跑通生命周期就声称全套 C# 完成。
建议提交：feat(scripting): complete S3 C# behaviour lifecycle integration

### S4：托管字段、Inspector 与场景/Prefab 存档

状态：已完成实现与固定源码完整验收，详见 `CS_SCRIPTING_S4_VALIDATION.md`。
收据 `Build/Tests/Scripting/s4-f618Hg/verification.json` 为 success=true，覆盖
字段边界/演变/未知数据、缺程序集、Prefab 引用重映射、Inspector、Awake 前恢复、
真实 Editor Play/Stop 与 Game、默认/RenderThread，并保留完整 S1–S3/Lua 回归。
标准构建 CET 失败、显式兼容完整 MSBuild 成功分别记录。
复用 Host ABI/R6、S2 原始 XML 与 S3 安全点；
Inspector 使用精确字段文本、覆盖开关和引用身份/解析状态，不包含 S5 类型/构建 UI。

任务：

1. 用户 C# 元数据由加载期托管反射提取，仅在装载时运行；缓存属于项目会话。不让 Clang 解析 C#，不强制引入 Roslyn 生成器。
2. 明确 public 实例字段及 SerializeField 私有字段规则、NonSerialized 排除；排除 static/readonly/任意属性。字段 Id、旧名别名、类型标签、默认值/覆盖值分离。
3. 支持 bool、固定宽度整数、float/double、string、enum、Vector3/Quaternion、ActorRef/AssetRef 描述。保留整数精度与枚举底层类型；不支持类型给诊断而非猜测转换。对象引用存 GUID/资源身份，加载后按场景/Prefab 映射重新解析。
4. 复用现有 Inspector 与 XML 能力但补齐缺失、非法值、未知字段保留、类型变化、重置默认值和覆盖解锁语义。Play 中的临时值不得自动写回编辑态存档。
5. 场景/Prefab 先创建对象并恢复字段引用，再进入 Awake；刷新/重载/删除后旧 UI 回调失效。

验收：整数边界、字符串、枚举、数学值、字段改名/删除/类型改变、缺失程序集往返保存不丢数据；旧 Lua 存档兼容；Prefab 克隆内部引用重映射；Inspector 改值实际影响 C#；Play/Stop 恢复编辑态值。
建议提交：feat(scripting): complete S4 managed fields and editor persistence

### S5：脚本项目构建与编辑器工作流

状态：已完成实现与固定源码完整验收，详见 `CS_SCRIPTING_S5_VALIDATION.md`。
最终收据 `Build/Tests/Scripting/s5-MuAls9/verification.json` 为 success=true。
默认/RenderThread、Lua、新项目完整编辑器流程、中文/空格路径、项目内/外缓存、
失败保留旧版、Play/暂停待应用、Main 停止态切换、并发/配置切换/无变化构建均通过。
S5 本身不包含运行态热重载和发布；运行态热重载与字段迁移由后续 S6 独立验收。

任务：提供用户 GameScripts 项目模板和引用，接入新建 C# 脚本、资源识别、类型选择与挂载、编译诊断、明确的 Build/Reload 状态。一文件多个类时由类型清单选择，不能拖入文件后任意选第一个类。更新 Lua 硬编码菜单/过滤器，继续支持 Lua。

构建在后台进程进行，使用绝对可执行路径和参数数组；按项目/配置隔离输出，生成 NativeApi 后再构建托管依赖。成功后将完整程序集/依赖/清单以一代产物发布，失败保持上一代可用；调度只在 Main 安全点切换。首版只在停止播放时应用新程序集，Play 中编译成功可以待应用。文件监测去抖并排除 bin/obj、代际加载缓存，避免重复构建循环。

验收：从新项目创建脚本→编译→选类型→挂 Actor→编辑字段→保存→Play→Stop→重新打开可运行；语法错误源位置可定位，失败产物不会替换正常版本；中文/空格路径、并发请求、配置切换、无修改构建稳定。
建议提交：feat(editor): complete S5 C# project build and attachment workflow

### S6：可卸载项目会话与有限状态热重载

状态：已完成实现与固定源码完整验收，详见 `CS_SCRIPTING_S6_VALIDATION.md`。
收据 `Build/Tests/Scripting/s6-aaF4Qf/verification.json` 为 success=true，包含 S1–S5、
默认/RenderThread 各 20 次构建/重载与 Play/Stop；两模式均 WeakReference alive=0、
collected=25。保留事件根负例明确报告待回收，没有把 Unload 当作回收证明。

任务：项目程序集和项目依赖进入 collectible ALC，固定共享程序集身份，采用代际 shadow-copy 避免覆盖已加载文件。卸载前停止该会话分派，完成在途同步调用，清理实例、事件、缓存、UI 引用；旧代实例 ID 拒绝调用。ALC 卸载是协作式，调用 Unload 不等于完成卸载，需 WeakReference 验证。[官方卸载指南](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability)

运行态重载只迁移显式支持的字段快照；不迁移静态状态、任意对象图、任务或委托。新版本先完成构建、类型/字段兼容性预检；提交安全点后替换实例，使用明确的重载恢复钩子与生命周期标记，避免重复 Awake/Start 副作用。预检失败保留旧代；切换后的用户恢复回调失败使新实例 Faulted，不承诺回滚已发生的 native 副作用。卸载不完成时报告保留原因线索与重启要求，不无限累积新代。

验收：至少 20 次构建/重载及 Play/Stop、字段兼容/不兼容迁移、重载失败、事件解绑、类型缓存清理、旧句柄访问、两种渲染模式；WeakReference 证明正常代可回收。GC 强制回收仅用于验收/诊断，不作为每帧机制；恶意死循环/用户永久线程不属于可强制安全卸载保证。
建议提交：feat(scripting): complete S6 collectible reload and field migration

### S7：发布、回归与性能验收

任务：默认采用应用私有、版本受控的运行时部署，验证实际 hostfxr 查找位置；分发 ManagedHost、ScriptCore、NativeApi、GameScripts、deps/runtimeconfig 和完整依赖/许可清单。禁止只复制一个托管 DLL 就声称发布完成。编辑器可使用 SDK 构建，发布游戏不运行编译器、不要求 SDK，也不依赖开发机绝对路径。

验收：

- Debug/Release/Publish 完整构建与无修改构建，按三配置验证原生/托管 ABI 匹配。
- 无开发 SDK、无全局 dotnet 路径的干净环境运行 OvGame 示例；有条件用独立机器/VM，无法验证时明确未完成项。
- 默认/RenderThread 下从挂载、字段、存档、碰撞、Play/Stop 到场景销毁的端到端回归，Lua 与 R0–R6 不退化。
- 记录 1/100/1000 Behaviour 的无操作调度、真实 Transform 更新、跨 ABI 次数、GC 分配与帧耗时；预热并校验结果，不预先承诺比 Lua 快。
- 接入 CI 能执行的无 GPU 用例；真实 GPU/发布环境单独留证，不把本地通过写成远端通过。

建议提交：feat(scripting): complete S7 C# runtime packaging and acceptance

## 5. 阶段执行纪律与验收记录

每阶段完成代码、相关测试与 CS_SCRIPTING_Sn_VALIDATION.md 后单独创建本地 commit，不 push。记录实际 commit hash、命令、环境、结果、未验证事项和下一阶段入口；不要再次累计到最后才提交。跨阶段问题归入当前阶段的真实依赖，必要时调整计划并说明原因，不空建阶段 commit 或声称不存在的历史快照。

以现有 Tests/Reflection/Integration/verify_r*.py 为基础回归；新脚本测试建议放 Tests/Scripting/Integration/verify_s*.py。每阶段只跑相关验证，最终 S7 才做完整矩阵；不能以静态表/输出字符串检查代替游戏行为。提交前检查差异与产物范围，忽略 SDK、构建结果、shadow-copy 和 Python 缓存。

当前无关未提交工作：RENDERING_ARCHITECTURE.md、RENDERING_ARCHITECTURE_DIAGRAM.zh-CN.md、.zcode/、CODEX_CHANGE_LIST_TEST.txt、Docs/、Examples/PerformanceLab/。执行前重新核对，保留这些内容；不能 git add . 或通过 reset/clean 清空工作区。

## 6. 新会话执行提示词

推荐每个会话完成一个阶段，首个会话直接粘贴：

> 请阅读 H:/overload/Overload/CS_SCRIPTING_INTEGRATION_PLAN.md、CLANG_REFLECTION_R6_VALIDATION.md 和 Tools/OvReflectGen/Schema/SCRIPT_ABI.md。以 c59edf3 的 R0–R6 为基础，执行 C# 接入计划的 S0，完成实际工具链 smoke、协议与验收记录后单独 commit，不 push。保留工作区无关改动。不要停在重复给计划；本会话只完成 S0，不提前实现 S1–S7。遇到无法解决的外部依赖时给出具体证据和最小恢复步骤，不能把未执行的测试标为通过。

后续会话将 S0 改为 S1、S2 等，并先读上一阶段验收记录与提交。要连续执行时，可明确要求按 S0→S7 顺序推进、每阶段验收并 commit；本文件本身不等于已授权后台自动执行。
