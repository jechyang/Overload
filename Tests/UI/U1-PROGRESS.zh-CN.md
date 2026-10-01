# U1 基础协议进度

日期：2026-09-21。U0 清单及回归结果见 `U0-VALIDATION.zh-CN.md`。
A 主线已验证公共集合、运行时事件、会话注册工具，以及 UI 标量与组件引用字段。
数组/List 的托管协议与字段编解码已验收；矩阵中 U1 相关类型为 implemented/adapted。
B/D/E/F 的后续阶段见各自进度文件与总矩阵。

## 已实现并验证

- `SetPropertyUtility`：值、可空引用、颜色的修改检测。
- `IndexedSet<T>`：唯一性、随机访问、交换删除、索引替换、排序、枚举与批量移除。
  修复参考辅助实现中重复插入/索引替换可能破坏集合状态的问题；比较器抛异常时保持原集合。
- `ObjectPool<T>`：引用对象池、借出计数、实例归属校验、重复释放拒绝、容量上限、
  初始化/重置失败的状态恢复、清理与 Dispose。
- `ListPool<T>`：实例持有，归还时清空元素；没有静态泛型池持有项目对象。
  使用 `using` 或 Behaviour 的 `TrackSubscription` 绑定所有者生命周期。
- `UIEvent` / `UIEvent<T>`：运行时监听、可取消订阅、重复监听去重、调用期间添加/移除、
  主线程约束。带 Behaviour 参数的构造函数绑定事件源生命周期；直接 Behaviour 方法
  的监听器也会在接收者销毁时注销。没有持久监听器/裸地址序列化。
- `UISessionRegistry<T>.For(Actor)`：会话持有类型表、弱引用元素、与 Behaviour 生命周期
  绑定的注册回执、主线程和会话检查；session 关闭时清空类型和注册记录。
- `Behaviour.ReleaseSubscriptions` 先取快照再清空，允许注销回调移除同一订阅列表，
  不在清理中失效枚举器。

## 真实验收

```text
node Tests/UI/run-validation.mjs Debug --u0 --managed --foundations --scripting-regression --core-library=<当前头文件对应的独立 OvCore.lib 绝对路径>
```

本轮完整成功 evidence：`Build/UIValidation/Debug-bkNFra/result.json`。
该调用除基础像素/布局/宿主/示例回归外，还通过了：

- 3000 次确定性随机集合变更、索引一致性、池归属和回调异常恢复。
- 事件回调期间修改监听器、错误线程取消后仍可正确取消、销毁事件源时停止分发。
- 真实 Actor/Behaviour 移除接收者和事件源后自动解除监听。
- 事件源仍存活时，被移除的接收者可由 GC 回收。
- 弱注册表即时注销、连续三次项目热重载无重复监听。
- 场景卸载与 session 关闭后，4 个项目程序集上下文全部可回收。
- 默认和独立 RenderThread 的现有 C#/Lua 生命周期回归。

`--foundations` 只是 U1 的已实现子集，不会将 `--u1` 标为通过。矩阵中的相关类型
继续保持 partial。列表的原生 Inspector/XML、ColorBlock/SpriteState/Navigation、
Actor sibling 的运行时验收和安全克隆仍需实现或完成验收。

## 并行任务与构建边界

验证过程中其他任务修改了 `ManagedField` 的 C++ 布局（新增 displayName）和字段显示
元数据。新头文件与旧 `Bin/Debug/OvCore/OvCore.lib` 混用曾导致原生访问异常；未回滚或
覆盖这些源码，而是在 `Build/UIValidation/core-isolated-1789921277890` 编译独立库。
相同用例使用一致的库后通过。该目录的 `manifest.json` 保存布局头文件哈希和归档来源。

独立编译的目标文件已完成；初次归档遇到磁盘不足，仅压缩本任务生成的中间文件和已完成
evidence 后，使用 HostX64 的 lib.exe 在非压缩输出目录成功归档。未删除其他任务产物。

后续修改生产 C++ 后，需要重新构建与头文件一致的库；不要复用旧 Bin 库。MSBuild
独立项目构建应显式提供 SolutionDir，并使用独立 IntDir/OutDir，避免共享产物竞争。

## 提交状态

U0 已独立提交为 `2c44e0e`（`test(ui): add complete uGUI runtime compatibility matrix`），
只包含八个 U0 文件；U1 扩展继续保留在工作区，未混入该提交。提交前后校验了工作文件
SHA-256，确认暂存 U0 快照没有覆盖 U1 内容，其他任务的改动也未暂存。

Git 写入通道已恢复：显式使用 `C:\Windows\System32\cmd.exe`、`login: false` 和
`require_escalated`，提交消息通过 `--file` 传入。PowerShell 的 CET 错误和普通 Node
子进程的 `.git` 写入限制仍不能代表该提交通道不可用。后续完成一个阶段即单独提交。

用户已清理 H 盘；后续构建、测试、中间文件和 evidence 继续存放在 H 盘。之前独立
OvCore.lib 已被清理，保留的成功日志仅作为历史验收记录；下一轮运行前必须重新构建
与当时头文件一致的库，不能把旧记录当作当前产物的验证。

## 2026-09-21 A 集成验收更新

- 使用 B 第三轮标准 Debug 新库（含已成功构建的 OvEditor），在
  `Build/UIValidation/Debug-UPCqoj` 编译了当前原生/CLR 验收程序。
- 独立 `foundations` 模式成功：`Build/UIValidation/u1-isolated-foundations.log`。
  真实销毁接收者/源、GC、连续三次热重载、关闭后全部项目 ALC 回收，无 ERROR。
- 独立 `fields` 模式成功：`Build/UIValidation/u1-isolated-fields.log`。
  Vector2/Vector4/Rect/RectOffset/Color 经过类型化 Inspector、XML、三次热重载；
  ComponentRef 经过 native/managed 类型解析、删除失效、场景相对查找及新代重绑。
  该模式故意提交 13 个非法字段写入；26 条重复 Console/Logger ERROR 均为预期拒绝，
  不是可忽略的生命周期错误。
- `node Tests/UI/run-field-codec-validation.mjs`：
  `Build/UIValidation/field-codec-ttxu0k/result.json`，584 断言通过。覆盖全部标量元素、
  数组/List、空/空引用、Unicode/NUL、嵌套、大小限制、非有限数、枚举、类型和 XML 实体拒绝。
  此结果只证明生产托管 codec，不证明列表的原生 Inspector/XML 或场景热重载。
- 总回归 `Build/UIValidation/Debug-UPCqoj/result.json` 为失败：新加静态帧不重复布局
  断言暴露布局通知重复排队，退出时还暴露 driven 失效句柄清理错误。D 已接手修复重测。
- `editor-camera` 编译/链接成功，但运行失败于并行编辑器的导航区域断言，
  证据 `Build/JobsValidation/Skinning/Debug-editor-camera-editor/current/run.log`。
  已通知编辑器任务核对；不把旧的相机通过记录当作当前构建结果。
- ManagedUI 示例新 host/project generation 已生成：
  `Examples/ManagedUI/.overload/csharp/build.xml`，generation
  `5d6f08a6-7705-4226-8ee7-070da003331d`；输出 `Build/UIValidation/u1-example-generation.log`。
  后续托管修复仍须再次生成。

### 提交边界

本次先提交已验证的 U1 公共工具里程碑：UI.Collections、UI.Events、UI.Registry、
Behaviour 的订阅清理、两份基础 fixture 和本进度文件。它不表示 U1 全部完成。
UI.Rebuild 的 session registry 交接已随 D 的 `223ae46` 提交，A 不再重复暂存。
标量/组件引用、集合 codec、Actor API 和跨 B/D/E/F 桥接继续保留在工作区，
后续按验收完成的里程碑提交；不混入并行编辑器/反射改动。

随后 D 已修复上述两项布局/清理问题，标准总回归通过：
`Build/UIValidation/Debug-9ajrk6/result.json`；独立完整 U4 验收通过：
`Build/UGUI-D/layout-QOxghO/result.json`。示例 generation 尚需包含这些最终修复。

### 第 4 轮最终集成记录

- B 标准 Debug 构建：`Build/UGUI-B/debug-build-4.log` 成功。
- A 完整总回归：`Build/UIValidation/Debug-VSMChf/result.json` 成功，
  包括 U0、native UI、managed UI、公共集合、事件生命周期、UI 标量/组件引用字段、
  示例，以及 S3 普通/RenderThread。此前 Debug-UPCqoj 的失败已由 D 修复。
- 当前字段 codec + 宿主异常清理共 587 项断言：
  `Build/UIValidation/field-codec-As11WK/result.json`。即使 OnDestroy 和 native UI
  release 同时抛异常，也释放订阅并保持销毁幂等。
- 最终示例 generation：`3edf3675-0c70-424c-bd9c-8755eb9b6964`；
  三份程序集大小/时间/SHA256 在 `Build/UIValidation/u1-final-example-generation.json`。
- D 最新第 4 轮验收：`Build/UGUI-D/layout-osZabR/result.json`，9145 断言和
  26 帧普通/RenderThread 完全一致。
- A 公共工具里程碑已提交 `f6ac031`。字段/引用/共享桥接仍与后续工作按归属分批提交。

- 新增真实 Actor 层级专项：`Build/UIValidation/u1-hierarchy-7xstZl/result.json`，
  子节点/根节点排序、索引边界、深度优先组件枚举、inactive 分支/parent 查询、
  三次重载间激活状态、循环拒绝、pending destroy 与帧末删除通过。
  `Actor.Active` 延用引擎现有 dispatch 结束后生效契约；最初测试在同一回调立即读取
  新值而失败，已改为跨回调验证，未修改生命周期重入约束。XML sibling 顺序仍待专项验收。
- editor-camera 最终成功：`Build/JobsValidation/Skinning/Debug-editor-camera-editor/result.json`。
  8 组相机/OverlayUI 像素回归通过；隐藏窗口 OS cursor warp 检查明确 SKIP，
  实际 ImGui 导航点击普通/RenderThread 已由编辑器任务单独验证。
- 第 4 轮两份 OvEditor 大小、mtime、SHA256 一致：
  `Build/UIValidation/u1-final-editor-binaries.json`（SHA256 `35d4d5a8bc2c2da8729688c5259c5fcb841b5bb854c42cd833414dc64b745fc9`）。

集合字段暂不由生产字段发现器暴露：tag25 codec 已实现并测试，但原生 XML/Inspector
尚未接入，当前仍按 UnsupportedField 保留原始记录，避免发布不能完整往返的编辑入口。
待原生集合编辑/重映射验收完成后移除这一临时发现边界。

### 本轮最终源码冻结记录

- B `d8acb1f` 已提交图形/遮罩里程碑，F `aa6467e` 已提交输入/事件里程碑。
- codec/销毁清理/集合发现边界最新为 588 断言，`Build/UIValidation/field-codec-7kNael/result.json`。
- 示例已再次串行刷新至 `0c94c9a2-b47a-442d-b68f-30d86ce401af`，包含 B 最后 Mask/Modifier
  编辑模式删除清理，以及 E 最后默认字体与小于 1 行距修复；
  `Build/UIValidation/u1-final-example-generation.json` 指向此 generation。
- Actor 层级专项已加入基础字段正式 fixture，新增 Scene XML 对子/根 sibling 顺序、
  inactive 继承与 parent 链的断言，不再把 XML sibling 列为未验收。
- 最终集成将共同的类型化 Inspector、InspectorName 和字段声明顺序作为共享字段实现纳入，
  保留编辑器任务其余 SceneView/Gizmo/CameraController 等未提交工作区改动。

**最新完整 A 集成验收**：`Build/UIValidation/Debug-GN2U0m/result.json` 成功。
该轮包含最新 Actor sibling/parent/active 的 Scene XML 验收、三次重载跨帧生命周期，
集合字段发现边界，以及 B/D/E/F 冻结后的全部基础 UI/示例/S3 回归。

### H 盘运行配置复验

六个 UGUI runner 已在启动子进程前统一 TEMP、TMP 与 OV_SCRIPTING_SHADOW_ROOT 到
各自 H 盘随机验收目录。最新完整回归 `Build/UIValidation/Debug-G3aK9G/result.json`
全部通过；已核对该次 `temp` 与 `managed-shadow` 实际存在，路径记录见
`Build/UIValidation/u1-h-drive-paths.json`。它是当前最终总回归证据。
