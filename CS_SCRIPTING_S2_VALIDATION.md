# C# 接入 S2 实施与验收记录

日期：2026-09-14。基线、开始与验收时 HEAD：`a1c3a1a`（S1）。
本文件与 S2 实现同一独立提交；提交号用
`git log -1 -- CS_SCRIPTING_S2_VALIDATION.md` 查询。本轮不 push。

**S2 下述真实行为验收完成；标准 `dotnet build` 仍因 CET 失败，未通过。**
S2 完成不表示 C# 游戏实例已经可以运行；对象构造、Owner 桥和生命周期属于 S3。

## 基线与先行复验

工作区起点准确为 S1 提交 `a1c3a1a`。保留原有的
`RENDERING_ARCHITECTURE.md`、`RENDERING_ARCHITECTURE_DIAGRAM.zh-CN.md`、
`.zcode/`、`CODEX_CHANGE_LIST_TEST.txt`、`Docs/`、`Examples/PerformanceLab/`，
未修改、暂存或清理这些无关工作。没有 reset/clean、系统更新、关闭 CET 或修改 SDK。

实际执行：

```text
node Scripts/Windows/InstallCSharpToolchain.mjs --verify-only
node Tests/Scripting/Integration/verify_s0.mjs
node Tests/Scripting/Integration/verify_s1.mjs --python C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe
```

- SDK 校验 exit 0。
- S0：`Build/Tests/Scripting/s0-3x5EsO/verification.json`，入口 exit 0；内部
  标准 dotnet build 仍为失败项，不能由验收器 exit 0 推断标准构建通过。
- 未修改 S1 实现前的复验：`Build/Tests/Scripting/s1-UMbndP/verification.json`，
  原生默认/RenderThread、生产宿主、R6 真实组件与失败矩阵通过，标准构建仍因 CET 失败。
- 沙箱内首次 S1 复验 `s1-xR1iKs` 的 MSBuild FileTracker 报
  `E_ACCESSDENIED (0x80070005)`；沙箱外重跑解决。PowerShell 启动本身也报 CET，
  后续编排使用 cmd/Node，没有把启动失败写成成功。

## 实现与阶段边界

- `Script` 为虚接口，现有 Lua/Null 模板实例实现该接口。`ScriptEngine` 为保持
  ServiceLocator 类型入口与资源路径语义的门面，以 pimpl 封装原后端，按身份分发。
  Lua 绑定中的查找/强转明确限制到 Lua，缺失实例不再进入断言式生命周期调用。
- `ScriptIdentity` 区分后端、逻辑程序集、规范类型名和显式 ID。Lua 保留规范化路径
  与旧字符串 API；非 Lua 内部键使用保留前缀及长度编码，XML 使用结构化身份。
  内部键不会写进 XML；不存在文件名、版本化 AssemblyQualifiedName 或运行时 token 主键。
- 新增常驻 `Overload.ScriptCore`，仅含 S2 的 `Behaviour` 类型契约、ScriptId 和
  ScriptAlias 标注。项目 ALC 共享默认上下文中的 ScriptCore/ManagedHost；项目依赖
  通过解析器或显式程序集相邻目录加载。类型发现不执行用户构造函数或 getter。
- Host ABI 1 的版本、布局、表和 JSON 签名未改。实现类型计数、信息、别名和所有权
  释放；排除抽象、开放泛型、非公开、无公开无参构造的候选。重复 ID、重复别名和
  别名/主键冲突为硬失败，不发布会话或替换之前可用的类型表。
- Native discovery 完成后关闭发现会话，只保留持久身份元数据，不保留过期 token。
  CSharp 适配对象是 `TypeReady` 类型挂载，不是假装有效的 managed 实例；其
  `IsValid()` 为 false。托管实例注册/构造、Owner/组件门面与游戏回调仍明确留在 S3。
  ALC 调用 Unload 只表示释放发现会话，不声称已验证回收，更不声称完成 S6 热重载。
- Behaviour 构造不再注册。Actor 先去重/建立记录、恢复原始数据，再显式注册；Lua
  实例取得默认值并恢复字段、注入 Owner，之后才由既有 Actor/Scene 进入 Awake。
  重复挂载不重复事件/顺序项；别名激活若会合并两条已有 Actor 记录则拒绝加载。
- 缺失类型、程序集、后端及无效 Lua 实例保留完整 Behaviour 记录和原始 XML 字段。
  未知属性、节点、CDATA、注释、宽整数原文不会经过 double 或被静默删除。
  重复/无法识别的存档记录作为 opaque XML 保留。解析成功后写当前主身份；之后
  类改名可沿用已解析出的稳定 ID。XML 保证树内容，不保证原文件的空白和引号形式。
- Editor/Game 的可选配置增加项目程序集路径与逻辑名称，在服务就绪后发现类型。
  缺失/类型已解析状态可显示；没有添加 S5 构建 UI、类型选择器或文件监视。
  配置与类型别名编码见 `Tools/OvCSharp/Schema/HOST_ABI.md`、`BOOTSTRAP_SERVICES.md`。

## 最终固定源码验收

```text
node Tests/Scripting/Integration/verify_s2.mjs --python C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe
```

最终证据：`Build/Tests/Scripting/s2-HqR2n6/verification.json`。
记录 stage=S2、baseline=a1c3a1a、39 条命令的 argv/exit、68 个源文件 SHA-256，
并在结束及整理文档时确认源码哈希未变。共享编排器末行沿用 S1 的输出前缀，
阶段依据为上述 JSON 的 S2 和 `s2-*` 子进程；没有把打印文本作为验收断言。

环境为 Windows `10.0.22000` x64，私有 SDK `10.0.401` / runtime `10.0.12`，
MSVC `14.42.34433` / Windows SDK `10.0.22621.0`。
真实 GPU 为 RTX 3070 Ti，OpenGL 4.5，驱动字符串 616.92。

| 验收 | 实际结果与证据 |
|---|---|
| Debug 完整原生 solution（含 Editor/Game） | `native-build` exit 0 |
| 原生无修改再构建 | exit 0；Debug obj 哈希和 mtime 无变化 |
| ManagedHost/ScriptCore locked restore | exit 0，无远端包源 |
| 标准 dotnet build | **exit 1，CET，未通过** |
| 锁定 SDK csc.dll 真实编译 | ScriptCore、生产/验收 ManagedHost、7 种类型程序集及2个依赖程序集均 exit 0 |
| 托管重复编译 | 相同响应文件产物哈希一致 |
| S1 宿主/R6 回归 | 生产启动、默认/RenderThread 真实 Box/Transform、ABI/线程/生命周期失败矩阵通过 |
| S2 原生运行 | `s2-default`、`s2-render-thread` 均 exit 0，每种模式执行115个运行断言 |
| Null 后端 | `s2-null` exit 0，5个运行断言；真实编译 Null 门面并链接同一 Actor/Behaviour 实现 |

S2 两种模式直接启动原生 EXE，再由 nethost/hostfxr 启动 CLR，使用生产 ManagedHost。
类型断言读取真实 Host ABI 返回数据；Lua 断言读取解释器字段与真实 Actor 名称；
存档断言比较实际序列化 XML 和重建后的对象/字段。具体覆盖：

- 一文件多个 Behaviour、同名类不同命名空间、无效构造约束筛选；带抛异常构造的
  合法类型被发现但没有构造。项目内额外 ScriptCore 副本不改变类型相等性。
- 显式 ID 重复、别名重复、别名与默认主身份冲突均返回 DuplicateIdentity；空 ID
  返回 InvalidArgument，损坏程序集/缺依赖返回 ManagedException，失败不发布 session。
- 实编移动后的源文件、不同 DLL 文件名与改名类；验证程序集逻辑身份独立于文件名。
  旧 tuple/default tuple 解析到显式主 ID，UTF-8 字节长度别名跨 ABI 正确解析。
- Actor 的重复 Lua 规范路径和 CSharp 三种身份挂载均只有一个实例记录/事件/顺序项；
  相同文本的 Lua 与 CSharp 挂载同时存在且 Lua 实例有效。
- Lua number/string/bool/ActorRef/Prefab AssetRef 的真实字段恢复和回调；Scene Play/Update
  证明字段与 Owner 在 Awake 前准备完成；真实 Prefab 保存、实例化和字段恢复；Lua Reload。
- 可用程序集缺失类型、禁用后端、未知后端、完整 opaque 数据以及未知 Lua 字段往返。
  实际移走主程序集后保存并在新 Actor 中重新读取记录，再移回程序集，原记录和重开
  记录均恢复类型身份，uint64 原始文本、嵌套未知字段、CDATA 和附加身份属性仍存在。
- 项目依赖 DLL 实际移走/恢复；相应项目程序集失败后可再次成功发现。
- 空 CSharp/Null 实例序列化，重复和畸形存档记录不丢失且不产生重复实例；混合场景
  通过真实 SceneManager 序列化/重建后，CSharp 身份和 Lua 覆盖字段仍能读取。

最终二进制 SHA-256：

```text
s2.exe:                    4866fa1830d56f8bf05a441861f2b0f4921d638542619813ee000216547ce548
s2-null.exe:               4376e5bdada4f072d0b629c4a0c8b812c0c84fc95061b73b11509ba38ec5d164
Overload.ManagedHost.dll:  d24bb17698fe905d6ef883c89accfd0cbd40c0c803efba2d5581b52e999749d1
Overload.ScriptCore.dll:   33b52d017c6d2591182bd4e13b49145e442935ca3d6298e196f08d64faddd2fa
```

开发过程中的失败保留在 Build：`s1-kkkbWU` 为声明/重载适配的编译错误；
`s2-vs44qs` 为夹具把中文路径编码为 ACP 的错误，修正为 UTF-8；
`s2-ioAoMF` 两种模式行为曾通过，但期间源码修改导致无修改构建收据失败，不能作为
最终固定源码验收。没有拼接这些运行结果冒充一次完整通过。

## 外部阻塞与未验证范围

最终 `dotnet-build.log` 在 ScriptCore 编译阶段实际报：

```text
Fatal error.
Your Windows doesn't fully support CET. Please install all available Windows updates.
```

exit 1，`standard_dotnet_build_passed=false`，`external_blockers` 非空。
直接使用同 SDK 的 csc.dll 证明编译/runtime/真实原生宿主行为，**不等同标准项目
构建通过**。没有证据表明系统已完成所需更新。最小恢复：更新 Windows 并重启，
重新校验私有 SDK，重跑 S0/S2；只有标准 dotnet build 实际 exit 0 且 blocker 清空，
才可改记通过，不应修改 SDK 二进制或全局关闭 CET。

没有实现或验收 S3 的 C# Owner/组件访问、managed 实例构造、游戏生命周期和回调重入；
没有实现 S4 的托管字段编辑/对象引用重映射、S5 构建工作流、S6 热重载/WeakReference
回收或 S7 发布/性能矩阵。没有交互运行完整 Editor/Game Play/Stop，也没有重跑完整
R0–R6/Release/Publish/远端 CI。Null 验证是后端独立门面链接真实引擎夹具，未声称
完成整套 Null Editor/Game 配置构建。后续入口为 S3，需要独立执行与验收。
