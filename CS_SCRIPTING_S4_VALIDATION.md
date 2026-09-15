# C# 接入 S4 实施与验收记录

日期：2026-09-15。起始 HEAD：`ef3ea8f`，已实际核对 S2 `3c9f9fa`、显式兼容
构建入口 `c3ae12d` 和 S3 `ef3ea8f`。本文件随 S4 单独提交，提交号可用
`git log -1 -- CS_SCRIPTING_S4_VALIDATION.md` 查询。不 push，不实施 S5–S7。

## 基线与工作区

已阅读计划、S0/S1/S2/S3 验收、MSBuild 兼容验收、HOST_ABI、BOOTSTRAP_SERVICES
及 verify_s1/verify_s2/verify_s3；核实 S3 最终收据
`Build/Tests/Scripting/s3-MX7SMl/verification.json` 为 success=true。
实施前运行：

```text
node Scripts/Windows/InstallCSharpToolchain.mjs --verify-only
node Tests/Scripting/Integration/verify_s3.mjs --python C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe
```

SDK 校验 exit 0。新基线 `Build/Tests/Scripting/s3-GiLkCY/verification.json` 完整通过，
包含 S1/S2/S3、Lua、Null、真实 Editor/Game、默认/RenderThread 和原生无修改构建。
标准 dotnet build 仍因 CET exit 1；完整 MSBuild 显式兼容构建 exit 0，分别留证。
首次沙箱基线 `s3-zw4FFf` 因 MSBuild FileTracker E_ACCESSDENIED 失败，自动审批允许
沙箱外运行后解决；未关闭文件跟踪。PowerShell 自身也报 CET，编排使用 CMD/Node。
Git 仅使用单次 `-c safe.directory=H:/overload/Overload`，未修改全局 Git 配置。

开始前保存已有 587 个文件的 SHA-256，使用 `node Build/s4-preserve.cjs --check`
复核。原有渲染文档、`.zcode/`、Docs、PerformanceLab、CODEX_CHANGE_LIST_TEST、
ResolveCSharpBuild 三文件和 CS_SCRIPTING_SDK_COMPATIBILITY_VALIDATION 全部保留。

## 实现

- 复用冻结的 184 字节 Host ABI、已有 R6 组件桥、S2 身份/原始记录保存及 S3
  主线程回调屏障。Host ABI/R6 JSON、签名、SDK 10.0.401、runtime 10.0.12、
  net10.0、global.json 均未改。
- 加载用户程序集时提取字段元数据，缓存由项目 session 持有。包含 public 实例
  字段、显式 SerializeField 的 private 字段（含基类 private）；排除 static、
  literal、readonly、NonSerialized 和所有属性。UnsupportedField 有诊断。
  FieldId、FormerlySerializedAs 和默认字段名共同构成稳定 ID/别名；冲突拒绝项目。
- 实现 Host 字段信息/别名、拥有权释放、实例字段读写。每次访问检查 Main、
  session generation、实例和所属类型字段 token。复合结果校验后释放并清零，
  包括标量/空字符串的独立拥有权收据；错误通过原有 Host 异常边界返回。
- 支持 bool、全部固定宽度有/无符号整数、float/double、string、enum、Vector3、
  Quaternion、ActorRef/AssetRef。整数直接使用对应 ABI union 成员，XML 用十进制，
  全程不经过 double。enum auxiliary 保存底层整数 tag 并检查其实际范围。
  字符串严格 UTF-8，XML 用 UTF-8 字节的 hex 表示，保留 NUL/中文/XML 特殊字符。
  null string 按 ABI 1 明确报 UnsupportedField，不猜测空字符串默认值。
- 类型发现/挂载不构造对象。首次 Inspector 或 Play 准备实际挂载对象，读取其字段
  默认值，之后恢复覆盖值；没有额外临时默认值实例或属性 getter。Editor Play 前
  原有 Lua Reload 保留尚未 Awake 的有效 C# Prepared 对象，首次 Awake 复用它。
  未 Awake 的编辑实例释放时不运行 OnDestroy；已 Awake 的实例保持 S3 销毁规则。
- Inspector 提供字段覆盖开关和精确文本编辑；关覆盖开关恢复捕获的构造默认值。
  ActorRef 显示 GUID/当前解析状态，AssetRef 编辑相对路径和资源 kind，enum 编辑
  底层数值。输入非法时不改变字段/存档并记录诊断。刷新、反序列化、项目移除和
  Behaviour 销毁使旧控件回调失效；控件插件另持有 widget 生命周期 token，切换到
  另一 Actor 并销毁旧控件后，复制保留的输入/覆盖开关事件也拒绝访问旧对象。
- 场景和 Prefab 保存显式编辑覆盖值，不保存构造默认值和任意运行时字段变化。
  成功读取的别名写回当前 ID，保留其扩展属性/子节点；删除、改类型、未知、非法、
  重复歧义字段保留原始 XML。内部快照用 compact XMLPrinter 避免混合节点格式化
  空白反复累积；数字/hex 词法解析允许外围 XML 空白，实际字符串字节不受影响。
- Scene Play 在任意 Awake 前准备全部附件。Prefab 先创建全体对象，构建旧 GUID→
  新 GUID 表，改写内部 ActorRef，外部引用保持；即使程序集缺失也能改写明确标记的
  原始 ActorRef。运行态 Prefab 批次使用 S3 安全快照、待删除/停止分派检查。
- ActorRef.Resolve(owner) 使用 Owner 的 Scene 和 R6 校验锚点按需查询，不缓存
  原生指针；删除后解析为空但持久 GUID 保留。AssetRef.Resolve(owner) 检查项目
  资源路径、类型和文件存在，不加载任意资源对象。Play/Stop 使用生产 EditorActions
  原有场景快照，Stop 后恢复编辑覆盖值，随后为新编辑场景准备新挂载实例。

## 验收入口与最终收据

```text
node Tests/Scripting/Integration/verify_s4.mjs --python C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe
```

S4 入口包含完整 S1–S3 回归，强制 `--compat-build`。每次重新构建生产/验收宿主、
ScriptCore、全部 S2 类型变体、S3 示例和 S4 原版/改名版/冲突版程序集，并真实编译
原生夹具及链接生产 Editor/Game 对象。所有子进程有超时，测试窗口隐藏。
不以打印 PASS 字样作为字段结果判定；断言读取真实 Host 字段、原生 Transform、
解析后的场景/Prefab 及生产 Inspector 控件事件处理结果。

最终入口 exit 0，固定源码收据：
`Build/Tests/Scripting/s4-f618Hg/verification.json`，UTC `2026-09-15T08:28:03.728Z`，
stage=S4、success=true。记录 70 条真实命令及 108 个源文件 SHA-256；编排器结束时
检查源码未变，整理文档时再次复核所有源码、冻结协议/工具链和 R6 生成文件哈希。
OS 为 Windows 10.0.22000 x64；私有 SDK/runtime 为 10.0.401/10.0.12；
原生工具链沿用 MSVC 14.42.34433、Windows SDK 10.0.22621.0。

| 验收 | 最终结果 |
|---|---|
| 原版标准 dotnet build，强制 Rebuild | **exit 1，CET，未通过**；standard_dotnet_build_passed=false |
| 显式兼容完整 MSBuild | 全部 exit 0；compatibility_msbuild_passed=true；生产/验收宿主确定性重建通过 |
| Debug 全原生 solution 与无修改构建 | 各 exit 0；无修改构建的 obj 哈希及 mtime 不变 |
| S1 宿主/R6/错误矩阵 | 生产宿主、默认/RenderThread、六种依赖/配置/架构负例按预期返回 |
| S2/Lua/Null | 默认、RenderThread、Null 全部 exit 0；旧 Lua 字段、场景、Prefab、身份/未知 XML 回归保留 |
| S3 生命周期/安全点/真实应用 | 场景默认/RenderThread、关闭会话旧门面、Editor/Game 两种模式全部 exit 0 |
| S4 原生字段矩阵 | s4-default、s4-render-thread 各 exit 0；全部整数边界/溢出、枚举底层范围、浮点最大/最小正规/次正规/-0、数学值、UTF-8/NUL、null/非法 UTF-8、ABI 类型/代次/线程/拥有权验证通过 |
| S4 存档/引用 | 真实 Scene/Prefab 往返、字段改名/删除/类型变化、未知/非法/歧义及扩展 XML、实际移走/恢复程序集、缺程序集 Prefab 重映射、内部/外部/已删除引用和 Awake 前解析通过 |
| S4 Inspector | 真实输入/覆盖开关事件改变 C#，native Transform 验证影响；默认与覆盖分离/重置、刷新、控件销毁、项目移除、Actor 删除后的旧事件失效通过 |
| S4 实际 Editor | 两种模式均 exit 0；Play 复用编辑 Prepared 对象，Play 中 Inspector 改值影响运行，序列化保留编辑覆盖，Stop 恢复 UInt64/字段/原生场景，第二次 Play 通过 |
| S4 实际 Game | 两种模式均 exit 0；生产 PreUpdate/Update/PostUpdate 在 Awake 前恢复存档值，运行改值不覆盖存档，Lua 共存通过 |
| 工作区与协议 | 587 个原有文件全部保持；Host ABI/R6/SDK 锁及生成文件与本轮基线相同 |

本轮重新编译并实际运行的最终产物 SHA-256：

```text
s4.exe                 4ba040014fbbe2e62bdd37404be7f65ae6e83b25a8b815938fadda2c85dd3525
S4.dll                 f7d18bac42217616a9a14b77177b67cf713d7e60703a7dea86e2222730f0cb34
S4Renamed.dll          da637949f595d14be7caaca9d3e9d8141f4e7f7215c81e236458767031cbcdbe
OvEditor-s4.exe        f8e50d3485e97efd3b00239b787f7bb371729184350e8448242a23ba8b457f14
OvGame-s4.exe          8abe18d747066bf70bdf76e34ec45ed5ffe8eb7481997b59ae4a34a90ce419da
```

实际应用隔离方式沿用 S3：链接生产 Context/Editor/EditorActions/Game/Scene 对象，
仅复制 SystemCalls 的 AppData 查询以使用测试 profile，资源目录联接仓库 Resources。
未替换游戏循环或生命周期。标准 CET 错误和预期 null/字段类型/重复身份负例日志
不是被隐藏的成功；最终收据分别标注其预期退出码与外部阻塞。

`s4-MEaygm` 是控件销毁补强前的完整通过收据，`s4-f618Hg` 才是最终提交源码收据，
没有将旧产物或不同轮次片段拼接为最终通过。

## 已定位的开发失败及修正

- H 盘仅余约 187 MB，原生 Editor 增量链接报 LNK1116/错误 112。仅将本轮新基线
  s3-GiLkCY 复制至允许的 C 盘任务目录，逐文件 SHA-256/链接目标校验后，以原路径
  目录联接保留访问。既有产物和历史收据未删除。验收器新增可选进程环境变量
  OV_SCRIPTING_EVIDENCE_ROOT，后续产物放 C 盘、Build 下保留对应联接，不改全局环境。
- `s4-ccnPFu` 首轮完整验收在 Null 门面 `/W4 /WX` 编译时发现局部 info 重名警告，
  改名后重新编译。该失败目录不是最终通过收据。
- 同目录 `dev-*` 是专项重编/运行诊断。夹具曾将 UTF-8 字节传给既有 SceneManager
  本地编码路径入口，导致真实 Prefab 文件找不到；改为遵循当前入口的 path.string()
  契约。字段 UTF-8 值通过显式 u8 转换测试，不混用 ACP。
- 带扩展子节点的数字字段暴露 tinyxml2 格式化空白问题，修正词法解析和内部快照；
  增加未知数据稳定往返及项目移除后旧 UI 事件无效断言。

## 范围与限制

- 标准 dotnet build 的 CET 失败仍未解除；兼容成功不等于标准构建通过。没有更新
  Windows、关闭 CET、修改 SDK 二进制、修改全局 PATH 或降级框架。
- S4 Inspector 使用实际控件/事件验收，真实 Editor/Game 使用生产循环与场景路径；
  没有把它描述为人工鼠标操作验收。枚举用底层数值编辑，ActorRef 用 GUID 编辑，
  不包含 S5 类型选择器、新建脚本/构建 UI。
- AssetRef 解析提供经校验的资源身份/存在状态；没有新增任意资源对象加载 API。
  缺失引用保留身份并解析为空。null string、数组、任意对象图不在 ABI 1 支持矩阵内。
- 未实施 S5 项目构建/类型选择 UI、S6 shadow-copy/热重载/对象图迁移/ALC 回收证明、
  S7 发布/无 SDK 环境/性能矩阵。不声称 Release/Publish、完整 R0–R6 或远端 CI 通过。
