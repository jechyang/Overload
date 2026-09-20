# F / U7 输入与事件工作包

工作区基线：`870c494538629e8c06e825297a8387cf293f284e`。本文件只记录 F，不修改总矩阵。
参考范围：uGUI 2018.4 / `7ae6166c9b673a966f62905953f4bc6b3153a675`，按行为重写。

## 状态

F 实现及核心原生/CLR/GPU 联合矩阵已通过，并已在 `cf675b5` 接入提交后的 A/B 共享依赖。
B 第四轮标准 Debug 构建通过；F 在该新库上再次完整验证通过，A 最终统一回归也已通过。
editor-camera 的 8 组像素回归通过；隐藏窗口 OS cursor warp 检查明确 SKIP，
导航按钮另由编辑器任务的实际 ImGui event replay 验证。最终逐类型兼容审核仍由 A 负责，
本文件不把尚待审核的总矩阵条目直接改成完成。

## 已实现

- Window 的有序键盘 down/up/repeat、字符、鼠标、滚轮、焦点；InputManager 持有输入采集器。
- Windows IME composition（Unicode scalar cursor）、多指触摸、UTF-8 剪贴板；不包含文字编辑策略。
- `UIInputCollector` 的确定性注入、不可变发布、Main 所有权、有限数和队列载荷校验。
- `BaseEventData`、`AxisEventData`、`PointerEventData`、`RaycastResult`、所有 17 个 handler 接口、
  ExecuteEvents、EventTrigger/TriggerEvent、EventHandle、EventTriggerType。
- EventSystem 选择守卫、更新 selected、导航、捕获清理；Pointer/Standalone/Touch 输入模块。
- session registry + scene 过滤；销毁注销；firstSelected/输入覆盖使用 A 的 ActorRef/ComponentRef。
- GraphicRaycaster 默认使用 B 的 `GraphicHitSource`/op53 正式只读图形命中源。
- PhysicsRaycaster 使用 Bullet 全命中查询；Physics2DRaycaster 使用原生二维 box/circle backend。
- `NativeUIRayProjection` 支持正交/透视相机，默认主相机或 Canvas 指定相机。
- GameView 仅新增 viewport/focus hook；没有修改 AView 的 `request.viewId` 回归保护。

## F → G/H 输入契约

生产类型：`Sources/OvWindowing/include/OvWindowing/Inputs/UIInputSnapshot.h`、
`Sources/OvManaged/Overload.ScriptCore/EventSystems.Input.cs`。

- `position` 为 **Game framebuffer 像素、左下原点**。输入窗口 client logical 坐标先减 dock offset，
  再按 framebufferScale 缩放，Y 使用 `(extent.y - clientY + offset.y) * scale.y`。
  越界坐标保留，不 clamp；拖拽仍使用原捕获目标。
- GameView 的 framebuffer 当前按 `GetSafeSize()` 分配，content→framebuffer 比例是 1，
  不能再乘一次 OS DPI。独立/native 视口可使用任意正比例（测试包含 x2 / y1.5）。
- `time`/`deltaTime` 单位为秒，time 单调不减。sequence 每次发布递增。
- 同帧 down/up、repeat 和字符保留有序事件，不折叠成最终按钮状态。
- 键码使用 GLFW；鼠标 left/right/middle 的 pointerId 是 -1/-2/-3；触摸 ID 非负。
- 字符事件 ID 是合法 Unicode scalar；composition 是 UTF-16 字符串，cursor **按 scalar 计数**。
- 焦点丢失会清 held keys/buttons/composition，托管模块释放 capture/selection/hover。
- `BaseInput.Inject(UIInputSnapshot)` 是托管确定性入口；原生入口是 `InputManager.UIInput().Push(event)`，
  与 Window callback 使用同一采集器。生产消费者从 BaseInput 获取 source，H 不另建采集器。
- 剪贴板通过 `BaseInput.clipboard` 访问，内容不进入渲染线程。

## A 接入契约

专属 `InputBridge.h/cpp`：

```cpp
void QueryInput(OvReflection::Script::State&, uint64_t owner, uint32_t operation,
    uint64_t argument, const std::string& text, uint64_t& output);
```

owner 是安全 `CTransform` 句柄；总分发须先由 Guard 验证 Main/context。不使用裸指针作为 managed session ID。
closing 不允许输入/剪贴板/物理查询，session 关闭只释放托管注册和捕获。

| op | 含义 |
|---|---|
| 80 | 获取本 tick 固定输入 word 数量；InputManager 第一次调用才采集 |
| 81 | argument 为 word 索引，读取同一固定数组，不重新消费事件 |
| 82 | 固定当前剪贴板，返回 UTF-8 字节数 |
| 83 | argument 为 8 字节块索引，返回 little-endian packed word |
| 84 | text 写剪贴板 |
| 85 | text 为 invariant 数字格式二维 ray/shape 请求，返回 double 距离；-1 未命中 |
| 86 | text 为 origin/direction/maxDistance/mask 请求，返回同场景 Bullet 命中数 |
| 87 | argument=index*8+field，读取 GUID、距离、世界点/法线；返回前过滤失效/其他场景 Actor |
| 88 | argument=packed screen point，text=valid/originXY/originZdirectionX/directionYZ/distance |

输入 word 格式在 `UIInputEncoding.h` 与 `NativeUIInputSource.Decode`：首 word magic `0x3149554f`，
后续 sequence、double time/delta、focus、packed float mouse/viewport、events/held/composition 数量、cursor。
每事件固定 5 words 加 composition scalar words；没有 ABI padding 或裸对象。
每帧最多 16384 events、每段 composition 4096 scalars、合计 65536 scalars，托管拒绝超过 200000 words。
InputManager.ClearEvents 清 tick 缓存；即使没有 UI 消费者也排空原生队列。

## B 接入契约

`GraphicHitSource` 是 B 拥有的正式适配器。F 不持有第二套 Frame。
`IUIGraphicHitSource.GetHits` 返回排序/深度/方向/世界点；`IsRaycastLocationValid` 调用 Graphic/filter。
输入点为 bottom-left，与 B op53 一致。首帧需生产 ExtractFrame 已形成快照，无快照不猜测几何。
已验证 Camera Canvas 的原生物理阻挡/关闭恢复和 multiple Canvas sorting 与真实像素一致。

## 独立验证入口

```text
node Tests/UI/run-input-validation.mjs
node Tests/UI/run-input-validation.mjs --compile-production
node Tests/UI/run-input-validation.mjs --runtime
```

第一条使用生产 C# 源码、测试用 INativeAccess scene model 验证事件状态机；不冒充真实原生场景。
第二条额外逐个编译 F 生产 C++ 文件，使用正式编译器 include contract，输出独立 H 盘目录。
第三条构建正式 ManagedHost、项目自定义 InputModule/Raycaster/handler，链接 Debug 库，
从 Window 事件经原生 bridge 到真实 CLR；包含 default/render-thread、重载、切场景和回收。
只有共享库已同步最新源码时才能运行第三条。

已通过 evidence：

- `Build/UIValidation/F-Debug-FGeYRM`：完整独立 runner，原生/C# 编码、事件状态机、六个生产 C++ 文件、
  正式 ManagedHost 和项目 fixture；真实 Window→InputManager→bridge→CLR click/drag/focus、多指、hot reload、
  失活、场景销毁、ALC 回收；Bullet/2D 真实 Actor 事件；三种 Canvas 的 GPU 像素及正式 GraphicRaycaster；
  RectMask/stencil 内外、CanvasGroup、多个 Canvas 排序、physics blocking。
  default 和 render-thread 两个执行均 exit 0。
- `Build/UIValidation/F-Debug-4D6Gra`：最后补测回调内禁用 EventSystem，中止本帧且无残留捕获。
- `Build/UIValidation/F-Debug-STwOVS`：B 第四轮新库上重跑全部 --runtime 用例，包含最终托管修改，全通过。
- `Build/UIValidation/Debug-5PI0zb`：标准 UI --managed --scripting-regression 全过（含 s3 default/render-thread）。
- B 标准构建日志 `Build/UGUI-B/debug-build-3.log` exit 0。
- B 第四轮构建日志 `Build/UGUI-B/debug-build-4.log` exit 0。
- 两份 OvEditor.exe 在第三轮构建后 size=43059200，mtime=2026-09-20T17:34:38.899Z，
  SHA256=`eec1bfae9540b6042faac9a0d4bd3518bbec9cf28aaa01e5fe6d034e70b85cd3`，完全一致。
- F 示例 host generation 成功：`424d2095-b0b4-4529-b18a-54108ef7aa56`，receipt 位于
  `Examples/ManagedUI/.overload/csharp/build.xml`。后续第四轮统一 generation 由 A 负责。
- A 最终第四轮 host generation：`3edf3675-0c70-424c-bd9c-8755eb9b6964`，
  evidence `Build/UIValidation/u1-final-example-generation.json`。
- A 最终冻结后的完整集成回归：`Build/UIValidation/Debug-GN2U0m/result.json`，success=true。
- 最终示例 generation 更新为 `0c94c9a2-b47a-442d-b68f-30d86ce401af`；上述最终 generation receipt 已更新。
- 最终双 EXE：size=43059712，mtime=2026-09-20T17:45:07.193Z，
  SHA256=`35d4d5a8bc2c2da8729688c5259c5fcb841b5bb854c42cd833414dc64b745fc9`，
  证据 `Build/UIValidation/u1-final-editor-binaries.json`。
- editor-camera：`Build/JobsValidation/Skinning/Debug-editor-camera-editor/result.json`。
  8 组像素验证通过；OS cursor warp 跳过原因及 ImGui 回放替代证据见 `U1-PROGRESS.zh-CN.md`。

每个 F 运行目录保存命令、编译器、commit 和退出码。

## 尚待验收/已知适配限制

- 统一 Debug、UI/managed/scripting、双 EXE 及示例 generation 已收口；editor-camera 保留上述明确的系统鼠标跳过项。
- 当前核心真实 CLR/GPU 联合矩阵已过；嵌套遮罩更深组合由 B 的 U5 矩阵与主线完整集成继续覆盖。
- 桌面真实 WM_IME/WM_TOUCH 注入与剪贴板轮回（目前是生产源编译和确定性事件载荷验证）。
- 二维 backend 是 query-only oriented box/circle，不承担完整 2D rigid-body simulation。
- 87 当前逐字段重复只读 Bullet 查询，安全但仍可改成 session-owned 批量结果租约以降低 bridge 次数。
- 一旦完整验收完成，交 A 更新总矩阵；在此之前不能称 U7 完成。

## F 文件清单与提交边界

- 新增：ScriptCore `EventSystems.*.cs`、`UI.GraphicRaycaster.cs`。
- 新增：`OvCore/Scripting/CSharp/InputBridge.h/cpp`、`OvCore/UI/Input/Physics2DQuery.h`。
- 新增：`OvWindowing/Inputs/UIInputSnapshot.h`、`UIInputEncoding.h`、`UIPlatformInput.h/cpp`。
- 修改：Window.h/cpp、InputManager.h/cpp、PhysicsEngine.h/cpp、GameView.h/cpp。
- 测试：`run-input-validation.mjs`、`input_snapshot_validation.cpp`、`event_system_validation.cpp`、
  `fixtures/InputEvents.cs`、`fixtures/NativeInputContracts.cs`、本进度文件。
- A 持有且已接入：GameplayBridge 总分发 80–88、Actor.IsAlive、UIBehaviour 非 RectTransform guard。
- B 持有且已接入：GraphicHitSource/op53/Frame 正式命中。
- 集成顶点为 `cf675b5`，已包含 F `aa6467e`、B `d8acb1f` 与 A 共享接口；单独 F commit 不应脱离上游使用。
- 未改总 runner/总矩阵、AView、SceneView、CameraController 或 Button/InputField 控件。
