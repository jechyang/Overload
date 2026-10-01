# Overload uGUI 运行时完整实现计划

更新日期：2026-09-20
当前基线：`dbbe799a5f7ceaaaa2280f9f5d81464c44c8d756`
参考源码：`Build/Reference/uGUI`
参考版本：Unity-Technologies/uGUI `2018.4`，提交 `7ae6166c9b673a966f62905953f4bc6b3153a675`

## 1. 目标与完成定义

目标是在 Overload 中实现一套参考 uGUI 2018.4 设计、分层和运行时行为的 UI 系统，同时保持 Overload 自己的命名空间、Actor/Behaviour 生命周期、资源系统和渲染架构。

“完成全部 uGUI”在本计划中指：

1. 固定参考提交中 `UnityEngine.UI/UI` 与 `UnityEngine.UI/EventSystem` 下的每个运行时公开类型，都必须在兼容矩阵中标记为以下三种状态之一：
   - `implemented`：在 Overload 中有可工作的对应实现和验收；
   - `adapted`：因引擎模型不同而采用 Overload 等价接口，并有行为验收；
   - `blocked`：确实缺少引擎级依赖，列出依赖、最小实现和解除条件。存在任何 `blocked` 时不得宣称完整。
2. 不能仅创建同名空壳类或只验证接口存在；组件必须在真实场景、真实渲染、真实输入和像素/状态检查中工作。
3. 不追求 `UnityEngine.UI` 二进制兼容，也不直接复制 UnityEngine 对象模型。公开 API 位于 `Overload.UI`、`Overload.EventSystems` 和 `Overload.Maths`。
4. Unity Editor 专用 Inspector、菜单、Prefab 创建器不属于运行时兼容范围；Overload 编辑器需要提供等价的组件添加、字段编辑、Scene/Game 预览和调试能力。
5. 如果实现实质性派生自参考源码，必须遵守并保留 Unity Companion License 要求。优先按行为重写，不机械复制整文件。

## 2. 不可破坏的架构约束

### 2.1 C# 与 C++ 边界

C# 负责：

- 布局算法和布局控制器；
- Graphic 顶点生成和 mesh modifier；
- MaskableGraphic、遮罩关系和裁剪策略；
- EventSystem、射线排序、事件冒泡/分发、导航；
- Selectable 状态机、Button/Toggle/Slider/ScrollRect 等控件行为；
- 文本排版、换行、对齐、caret/selection 几何；
- 可继承、可重写的扩展点和项目脚本热重载。

C++ 仅负责：

- RectTransform、Canvas、CanvasRenderer 等必要原生组件及安全句柄；
- 窗口输入、触摸、字符输入、IME、剪贴板和物理查询的底层采集；
- Texture/Sprite/Material 资源，以及 Font/FreeType 原生动态 SDF 字形缓存、GPU 图集和代际管理；
- 不可变帧快照、网格上传、裁剪/模板状态和渲染命令；
- 主线程/Worker/RenderThread 所有权校验。

禁止把控件状态机、事件分发、布局策略、Image fill、文本排版等高层逻辑放入 C++ 后只提供 C# 包装。

### 2.2 生命周期和线程边界

- 编辑态首次绘制前，仅准备当前场景的 UI 类型和 UI 接口实现；不依赖选择对象或打开 Inspector。
- 不提前实例化普通游戏脚本，不提前调用 Awake/Start。
- 项目自定义 UI 子类热重载不需要重编 C++；字段和可恢复状态按现有 session 迁移。
- Worker/RenderThread 不得访问托管对象、Actor、活组件或资源管理器。
- 输入、UI 几何、纹理/字体图集和材质均须在主线程形成不可变快照后跨线程。
- 禁用、销毁、移除组件、场景切换和 session 关闭必须撤销注册、捕获和焦点，不留下静态强引用。

### 2.3 已知回归保护

- `AView.cpp` 必须继续按主视图 `request.viewId` 查矩阵，绝不能退回 `preparedViews.back()`。
- UI-only Overlay pass、Camera/World Space SceneUI 和普通 Scene 网格必须可同时存在。
- `Build/Debug/OvEditor.exe` 必须确认为最新复制产物，不能只验证 `Bin/Debug/OvEditor/OvEditor.exe`。
- 修改 ScriptCore 或 ManagedHost 后，必须重建示例项目自己的 host generation。

## 3. 当前基线

已实现：

- 原生 `RectTransform`、`Canvas`、`CanvasRenderer`、基础 `CanvasGroup`、基础 `RectMask2D` 和旧 `CUIImage`；
- Overlay、Camera、World Space 三种 Canvas；
- C# `UIBehaviour`、`CanvasScaler`、`LayoutElement`、横/纵布局组、`ContentSizeFitter`；
- `LayoutUtility`、`LayoutRebuilder`、`CanvasUpdateRegistry`；
- C# `Graphic`、基础 `Image`、基础 `RawImage`、`VertexHelper`、基础 `UIVertex`；
- 位置、UV0、颜色、纹理和基础材质的 CanvasRenderer 网格提交；
- 编辑态首帧准备、项目子类热重载、不可变 UI 渲染快照和 RenderThread 验证。

当前事实来源：

- `Tests/UI/UGUI-COMPATIBILITY.zh-CN.md`
- `Tests/UI/README.md`
- `Sources/OvManaged/Overload.ScriptCore/UI.*.cs`
- `Sources/OvCore/include/OvCore/UI` 与 `Sources/OvCore/src/OvCore/UI`

开始新阶段前必须运行基线：

```text
Scripts\Windows\Build.bat Debug -skip-project-generation
node Tests\UI\run-validation.mjs Debug --managed --scripting-regression
node Tests\Skinning\run-validation.mjs Debug --editor-camera
```

## 4. 工作区保护

基线提交后工作区仍包含其他任务的改动。新会话必须先执行 `git status --short`，逐文件保护，不得 reset、checkout、clean 或覆盖。

截至本文创建时，至少以下内容不属于本计划，不得顺手提交：

- `Examples/CSharpGameplay/**`
- `Docs/MultithreadingTutorial/**`
- `Examples/PerformanceLab/**`
- `RENDERING_ARCHITECTURE.md`
- `RENDERING_ARCHITECTURE_DIAGRAM.zh-CN.md`
- `Sources/OvCore/src/OvCore/SceneSystem/SceneManager.cpp`
- `Tests/Scripting/Integration/native_example_scene_validation.cpp`
- `Tests/Scripting/Integration/verify_example_scene.mjs`
- `.zcode/**`、`Sources/OvEditor/Build/**` 和其他本机生成物

如果这些文件的状态发生变化，以实际 `git diff` 为准，不以此列表替代检查。

## 5. 目标运行时数据流

```text
Window/InputManager
    -> C++ UIInputSnapshot（鼠标/触摸/键盘/字符/IME，主线程）
    -> C# InputModule / EventSystem
    -> C# RaycasterManager / GraphicRaycaster / PhysicsRaycaster
    -> C# ExecuteEvents / Selectable / Controls
    -> C# dirty/layout/mesh/text rebuild
    -> C++ CanvasRenderer CPU sink
    -> immutable UI Frame（网格、裁剪、材质、纹理/动态 SDF 图集页代际）
    -> Worker prepare/record
    -> RenderThread submit
```

所有全局注册表必须是 managed session 或 scene 所有，不能使用跨 session 持有项目对象的静态集合。

## 6. 分阶段实施

每阶段都应独立构建、验证、更新兼容矩阵并单独提交。除非阶段明确要求，不要跨阶段提前实现控件。

### U0：建立完整兼容矩阵和确定性测试入口

目标：先让“全部”可计算，防止遗漏文件或用空壳冒充。

工作：

- 新建 `Tests/UI/ugui-runtime-matrix.json`，逐项列出固定参考提交中的运行时 `.cs` 文件、公开类型、Overload 对应类型、状态、测试和差异说明。
- 扩展 `Tools/UI/reference-ugui.mjs --check`：验证提交、文件清单和矩阵覆盖率。
- 将现有组件标记为 `partial`，不能直接标记为 `implemented`。
- 为每个后续阶段在 `Tests/UI/run-validation.mjs` 预留独立 flag 和 evidence 条目。
- 记录可接受差异：命名空间、Actor 生命周期、AssetRef、无 UnityEngine.Object、无 MonoBehaviour coroutine。

验收：

- 参考运行时文件覆盖率 100%；无未分类类型。
- 矩阵脚本能拒绝漏项、重复项、未知状态和没有测试编号的 `implemented`。

建议提交：`test(ui): add complete uGUI runtime compatibility matrix`

### U1：基础协议、字段系统与公共工具

目标：为后续所有组件补齐共同能力，避免每个控件自造桥接。

C#：

- 完善 `SetPropertyUtility`、`IndexedSet<T>`、`ObjectPool<T>`、`ListPool<T>`。
- 增加可序列化的组件引用、Actor 引用、枚举、Vector2/Vector3/Vector4、Rect、RectOffset、Color/ColorBlock、SpriteState、Navigation 和列表字段。
- 定义 `UIEvent` / `UIEvent<T>`：支持运行时监听器；持久监听器若进入本阶段，必须通过类型/方法 token 安全恢复，不能保存对象地址。
- 补齐 Actor 的 sibling index、active-in-hierarchy、组件枚举、延迟销毁和安全克隆 API；Dropdown/DefaultControls 后续会依赖。
- 补齐 session-owned registry、弱引用和销毁时自动注销工具。

C++/宿主：

- 只扩展通用字段 ABI、安全句柄和批量只读查询。
- 为数组/结构体设置长度和总字节上限，拒绝非有限浮点、非法枚举和跨场景引用。

验收：

- 新字段经过 Inspector 编辑、XML 往返、热重载迁移、失效引用和场景切换测试。
- session 关闭后所有 registry、event listener 和组件引用可回收。

建议提交：`feat(ui): add shared serialization and registry foundations`

### U2：Graphic、UIVertex、CanvasRenderer 和 modifier 完整化

目标：完成所有图形组件共同的扩展链。

C#：

- `UIVertex` 增加 normal、tangent、uv1、uv2、uv3；颜色继续使用 Color32。
- 补齐 `VertexHelper` 的 stream、triangle stream、fill mesh、读写顶点等参考 API。
- 完善 `Graphic`：canvas/depth、default material、materialForRendering、pixelsPerUnit、Raycast、CrossFadeColor/Alpha、注册/注销和 rebuild 回调。
- 实现 `GraphicRegistry`、`GraphicRebuildTracker`、`IGraphicEnabledDisabled`。
- 实现 `IMaterialModifier`、`IMeshModifier`、`BaseMeshEffect`、`Shadow`、`Outline`、`PositionAsUV1`。
- modifier 链顺序由 C# 决定，项目自定义 modifier 可继承和热重载。

C++：

- 扩展 `CCanvasRenderer` 顶点格式、多个材质/纹理槽、pop material、颜色/alpha、裁剪/cull 和代际。
- Frame 快照复制所有流和材质槽；GPU Geometry 与纹理代际在命令执行完成前存活。
- 不在 C++ 实现 modifier 或 Graphic 状态机。

验收：

- 项目 C# modifier 修改位置、UV、颜色、normal/tangent，真实像素与顶点快照均验证。
- 多材质、禁用 modifier、modifier 销毁、热重载、RenderThread 完全一致。
- 非法索引、超限网格、NaN/Inf 和资源切换有负向测试。

建议提交：`feat(ui): complete Graphic vertex and modifier pipeline`

### U3：Sprite、Image 和 RawImage 完整化

目标：完成 uGUI Image 的主要生成模式，几何仍全部由 C# 生成。

资源层：

- 定义 `SpriteRef`/Sprite 资源元数据：texture、rect、border、pivot、pixelsPerUnit、packing/rotation。
- C++ 只解析纹理和 Sprite 元数据、固定 GPU 纹理代际；不得生成 Image 网格。

C#：

- `Image.Type`：Simple、Sliced、Tiled、Filled。
- `FillMethod`：Horizontal、Vertical、Radial90、Radial180、Radial360；支持 fillOrigin、fillClockwise、fillAmount。
- preserveAspect、useSpriteMesh、pixelsPerUnitMultiplier、alpha hit threshold、SetNativeSize。
- RawImage 完成 SetNativeSize、UV 边界、材质和 raycast 行为。
- 对 packed/rotated sprite 作明确适配并写入矩阵。

验收：

- 每种 Image type/fill origin/clockwise 至少一组参考像素；边界值 0、0.5、1。
- 九宫格角、边、中心的 UV 和像素不串色；Tiled 在裁剪后保持 UV 连续。
- 资源热换、纹理卸载、场景切换和 RenderThread 快照稳定。

建议提交：`feat(ui): implement sprite-backed Image modes`

### U4：布局系统收尾

目标：补齐固定参考版本的所有布局组件和 driven 行为。

- 实现 `GridLayoutGroup`：startCorner、startAxis、cellSize、spacing、constraint、constraintCount。
- 实现 `AspectRatioFitter` 全部 AspectMode。
- 实现 `DrivenRectTransformTracker` 和 driven 属性恢复。
- 对照固定版本补齐 Horizontal/VerticalLayoutGroup 的 child scale、force expand、control、alignment 和 reverse arrangement（仅实现参考版本实际存在项）。
- 完善 ContentSizeFitter 与嵌套 layout group 的循环检测和 warning。
- 原生/Inspector 变化应精确标 dirty，逐步取消不必要的每帧全根扫描。

验收：

- 每个 Grid corner/axis/constraint 组合有几何断言。
- AspectRatioFitter 在 stretch anchor、非中心 pivot 和父节点 resize 下正确。
- 禁用/销毁 controller 后 driven 属性恢复；热重载不留下 tracker。

建议提交：`feat(ui): complete uGUI layout components`

### U5：裁剪、MaskableGraphic 与模板遮罩

目标：实现 uGUI 的矩形裁剪和 stencil mask 语义。

C#：

- 实现 `IClipper`、`IClippable`、`IClipRegion`、`ClipperRegistry`、`Clipping`、`RectangularVertexClipper`。
- 实现 `IMask`、`IMaskable`、`MaskUtilities`、`MaskableGraphic`。
- 提供完整 C# `RectMask2D` 和 `CanvasGroup` 包装，包含 ignoreParentGroups。
- 实现 `Mask`、showMaskGraphic、嵌套 mask depth、材质修改链和 cull 通知。

C++：

- Frame 携带不可变 clip stack/stencil state。
- Overlay 与 SceneUI 都正确提交 scissor/stencil；不能污染 Scene、Picking 或下一帧状态。
- `StencilMaterial` 的 GPU 状态/材质变体可以原生缓存，但 mask 层级和选择逻辑留在 C#。

验收：

- 多层 RectMask2D、Mask、CanvasGroup、禁用父节点、嵌套 Canvas 的真实像素。
- stencil 深度上限、资源回收、跨场景和 RenderThread 测试。
- mask 下 raycast 与实际可见区域一致。

建议提交：`feat(ui): add maskable graphics and stencil clipping`

### U6：原生动态 SDF 字体与 C# Text

目标：提供功能完整的 uGUI legacy Text 等价组件，为后续 InputField/Dropdown 服务；字体后端使用原生动态单通道 SDF，不使用按字号缓存的位图 alpha 字形。

边界约束：

- C++ 负责字体资源、FreeType 字形轮廓、SDF 生成、图集分配与上传、原生/GPU 资源生命周期。
- C# 负责富文本解析、fallback 选择、换行、对齐、best fit、overflow、顶点生成和按图集页分批。
- C++ 不实现文本排版、控件行为或四边形生成，也不向 C# 暴露裸 FreeType 指针。
- U6 默认实现单通道 SDF；MSDF/MTSDF 不作为本阶段验收条件。未来若增加，只作为可替换的字体栅格化后端，不改变 C# `Text` 和 `Graphic` 接口。

C++ 原生动态 SDF 层：

- `Font` 资源持有字体文件数据、FreeType face 和资源代际；字体卸载或热替换后，旧 face 不再被新请求使用，但已提交帧引用的旧图集仍保持有效。
- 提供批量 `EnsureGlyphs` 边界。一次请求传入 Font 引用、去重后的 code point/glyph、样式和 SDF 配置，返回不可变 glyph 记录，避免逐字符跨托管边界调用。
- glyph 记录至少包含 glyph id、advance、bearing、plane bounds、atlas UV、atlas page/material slot、SDF scale/bias/spread 和资源代际。
- 从 FreeType outline 原生生成单通道 signed distance field；采用固定设计/em 尺寸和可配置 spread，图集优先使用线性采样的 R8，质量不足时允许 R16，并为双线性过滤保留明确的 padding。
- cache key 至少包含字体资源代际、face index、glyph id、style/variation、SDF em size 和 spread，确保热替换或质量策略变化不会复用错误字形。
- 单个图集代际内采用 append-only page，已发布 glyph 的 UV 不迁移。只有必须重排或淘汰时才创建新代际；旧 page texture 和 glyph table 由在途帧继续持有，直到渲染线程消费完成。
- 图集可增长为多页；每个 glyph 返回 page/material slot，并复用 U2 的 `CanvasRenderer` 多材质/多纹理槽提交，不依赖一个无限增长的全局 atlas。
- 缺字生成按帧批处理并去重；SDF 生成和 atlas 上传只允许发生在主线程/资源所有者阶段，不允许渲染线程临时生成字形或修改图集。
- 提供 SDF UI shader/material：采样 signed distance，使用导数感知的 `fwidth`/`smoothstep` 计算覆盖率，使同一缓存字形在合理缩放范围内保持清晰；threshold、softness、scale/bias 等渲染参数进入不可变快照。
- SDF 材质必须兼容 Overlay、Camera、World Space、矩形裁剪、Mask/stencil 和现有 OverlayUI/SceneUI pass。

C#：

- 实现 `FontData`、`FontUpdateTracker`、`Text`，接口和序列化语义尽量对齐 uGUI 2018.4。
- 支持 font、fontSize、fontStyle、lineSpacing、alignment、horizontal/vertical overflow、best fit、richText（按 2018.4 范围）。
- C# 完成字符解析、fallback 字体选择、换行、对齐、best-fit 选择、顶点和 UV 生成；批量请求缺失 glyph，并依据返回的 atlas page/material slot 对几何分批。
- `fontSize` 在有效质量范围内主要缩放 glyph plane bounds，不为每个字号重复缓存位图；只有超过预设缩放质量区间时，才允许请求另一档 SDF em size。
- 明确 Unicode、surrogate pair、组合字符、缺字 fallback 和换行规则；至少覆盖 ASCII、中文、代理对和组合字符输入，fallback 顺序必须稳定且可测试。
- Shadow/Outline 优先保持为 C# mesh/material effect；SDF shader 可以暴露边缘和柔化参数，但不得把控件布局或文本排版移入 C++。

验收：

- 验证 atlas texel 表示有符号距离：字形内部、轮廓和外部采样值符合约定，而不是位图 alpha coverage。
- 同一缓存 glyph 在多个字体大小、Canvas scale 和 DPI 下保持清晰，使用真实 GPU 渲染和边缘像素容差比较。
- 一帧出现多个缺失 glyph 时只触发批量生成；相同文本后续帧不重复生成或上传。
- 覆盖 atlas page 增长、多页文本、混合 fallback 字体，以及新代际发布后旧在途帧继续使用旧 texture/glyph table 的情况。
- 覆盖字形指标、kerning、换行、对齐、overflow、best fit、rich text 的几何和真实像素。
- 覆盖字体/字号热换、脚本热重载、禁用、销毁、切场景、资源卸载、Overlay/Camera/World Space 和 RenderThread。
- 明确断言渲染线程不会执行 FreeType、SDF 生成、atlas packing 或资源上传。

建议提交：`feat(ui): add native dynamic SDF fonts and managed Text`

### U7：EventSystem、输入快照与 Raycaster

目标：完成可扩展的事件基础设施，不先写具体控件。

C++：

- 从 Window/InputManager 生成不可变 `UIInputSnapshot`：鼠标位置/按钮/滚轮、键盘按下/释放/重复、字符输入、触摸、焦点、IME composition。
- 输入坐标明确为 Game viewport 坐标，处理编辑器 dock、DPI、viewport offset 和 Y 翻转。
- 暴露批量物理 raycast 结果或安全查询接口；不得在 RenderThread 查询物理世界。

C#：

- 实现 `BaseEventData`、`AxisEventData`、`PointerEventData`、`RaycastResult`、`MoveDirection`。
- 实现全部 EventInterfaces、`ExecuteEvents`、`EventHandle`、`EventTriggerType`、`EventTrigger`。
- 实现 `EventSystem`、firstSelected/currentSelected、selection guard、sendNavigationEvents、drag threshold。
- 实现 `RaycasterManager`、`BaseRaycaster`、`GraphicRaycaster`。
- 实现 `BaseInput`、`BaseInputModule`、`PointerInputModule`、`StandaloneInputModule`、`TouchInputModule`。
- 实现 `PhysicsRaycaster`；`Physics2DRaycaster` 若无 2D 物理后端，必须在本阶段补齐最小 2D query backend，不能留空壳后标完成。

事件时序必须覆盖 pointer enter/exit、down/up/click、initialize potential drag、begin/drag/end drag、drop、scroll、move、submit、cancel、select/deselect、update selected。

验收：

- 使用确定性输入注入，不依赖人工点击。
- Overlay、Camera、World Space、多 Canvas、遮罩、sorting order 和 blocking objects 的 raycast 排序。
- 多指触摸、拖拽越界、对象在回调中禁用/销毁、场景切换、焦点丢失。
- 自定义项目 C# InputModule、Raycaster 和事件接口可热重载。

建议提交：`feat(ui): implement EventSystem and raycast input pipeline`

### U8：Selectable、导航和状态转换

目标：建立所有交互控件共享的状态机。

- 实现 `Navigation`、`ColorBlock`、`SpriteState`、`AnimationTriggers`。
- 实现 `Selectable`：静态/会话注册、interactable、CanvasGroup 继承、navigation、pointer/selection 状态。
- 支持 Transition.None、ColorTint、SpriteSwap、Animation。
- ColorTint 使用 C# tween runner；SpriteSwap 驱动 Image；Animation 通过可替换 `IUIAnimationDriver` 对接项目动画组件。若引擎没有通用 Animator，必须提供可实际触发和测试的默认驱动，不能静默 no-op。
- 实现 `CoroutineTween`、`TweenRunner<T>` 的 session 安全等价实现，不依赖 Unity coroutine。

验收：

- Normal/Highlighted/Pressed/Selected/Disabled 转换像素与状态。
- 自动/显式导航，禁用对象跳过，嵌套 Canvas 和对象销毁安全。
- 热重载期间无重复监听器、无旧 tween 回调。

建议提交：`feat(ui): add Selectable navigation and transitions`

### U9：Button、Toggle、Slider、Scrollbar

目标：完成基础交互控件。

- `Button`：click、submit、transition、interactable。
- `Toggle` / `ToggleGroup`：isOn、allowSwitchOff、组内互斥、通知抑制。
- `Slider`：方向、min/max、wholeNumbers、normalizedValue、handle/fill RectTransform 驱动。
- `Scrollbar`：方向、size、numberOfSteps、drag 和 page step。
- 所有 onValueChanged/onClick 使用 `UIEvent<T>`，项目 C# 可监听、继承和重写。

验收：

- 鼠标、键盘导航、submit/cancel、拖拽和程序赋值。
- set-without-notify、回调中销毁、父 CanvasGroup 禁用、热重载状态迁移。
- 真实像素和 RectTransform 几何，不只检查事件计数。

建议提交：`feat(ui): implement core selectable controls`

### U10：ScrollRect

目标：完成嵌套滚动和惯性行为。

- viewport/content、horizontal/vertical、movementType、elasticity、inertia、decelerationRate、scrollSensitivity。
- scrollbar visibility、spacing、自动隐藏/扩展 viewport。
- drag、wheel、normalizedPosition、velocity、bounds 和 late update。
- 嵌套 ScrollRect 的事件转交规则。

验收：

- Unrestricted/Elastic/Clamped，水平/垂直/双轴，滚轮、拖拽、惯性和 resize。
- 固定 delta 的确定性轨迹；默认与 RenderThread 像素一致。
- content/viewport 销毁、禁用和场景切换无捕获残留。

建议提交：`feat(ui): implement ScrollRect behavior`

### U11：InputField 与桌面文本输入

目标：实现参考版本 InputField 的编辑行为。

底层：

- Window 增加字符输入、key repeat、剪贴板、焦点和 IME composition API；用快照传给 C#。
- 平台代码只提供输入和剪贴板，不实现编辑策略。

C#：

- textComponent、placeholder、caret、selection、blink、scroll offset。
- ContentType、InputType、CharacterValidation、LineType、characterLimit、readOnly、asteriskChar。
- 插入/删除、word/line navigation、Home/End、选择、复制/剪切/粘贴、submit/cancel。
- onValueChanged/onEndEdit、自定义 validator、组合输入。
- 移动软键盘在桌面主机上采用 `TouchScreenKeyboard` 的 character/IME 会话适配
  （`adapted`），无屏幕键盘 UI 面；InputField 可打开会话并注入/同步文本。

验收：

- 确定性键盘/字符/IME 注入；ASCII、中文、surrogate、选择和剪贴板。
- 密码显示不泄露真实文本到渲染 mesh/log。
- focus 丢失、对象销毁、场景切换和热重载安全。

建议提交：`feat(ui): implement InputField and text editing`

### U12：Dropdown 与 DefaultControls

目标：完成动态层级控件和运行时创建工厂。

- 实现 `Dropdown`：OptionData、template、caption、item 创建、列表定位、滚动、blocker、选择和销毁。
- 实现 `DefaultControls`，用 Overload Actor/Behaviour API 创建 Button、Text、Image、Toggle、Slider、Scrollbar、Dropdown、InputField、ScrollView 的标准层级。
- Actor clone/instantiate 必须保留组件引用映射和托管字段，不复制运行时 listener 或失效句柄。
- 实现 `Misc` 中仍被运行时依赖的工具。

验收：

- DefaultControls 创建的每种控件可直接交互并正确序列化。
- Dropdown 多次打开/关闭、场景切换、选项动态变化、模板销毁无泄漏。
- 运行时创建的项目自定义子类可热重载。

建议提交：`feat(ui): add Dropdown and default control factories`

### U13：完整兼容收口与性能硬化

目标：清零矩阵中的 partial/blocked，并形成可持续回归。

- 对照参考目录逐文件复核：Core、Layout、Culling、MaterialModifiers、Utility、VertexModifiers、Animation、EventSystem、EventData、InputModules、Raycasters。
- 所有 `partial` 必须转为 implemented/adapted；所有 `blocked` 必须解除，否则发布说明明确为未完成。
- 增加 UI profiler 指标：layout roots、graphic rebuilds、vertices、indices、draws、SDF glyph hits/misses、SDF atlas pages/updates、raycasts、event dispatch、GC allocations。
- 消除每帧无条件 layout 根扫描；无 dirty 的静态 UI 不应重复上传网格。
- 为大量 Graphic、深层 mask、长文本、Dropdown/ScrollRect 和频繁热重载建立压力测试。
- 更新兼容文档、示例和 API 注释；生成最终矩阵报告。

验收：

- 完整矩阵无遗漏、无空壳、无无测试的 implemented。
- Debug 和 Release 均通过；默认和 RenderThread 结果一致。
- 长时间输入/场景切换/热重载无泄漏、无 use-after-free、无跨线程访问。
- 示例覆盖布局、图形、文字、遮罩、按钮、导航、滚动、输入框和 Dropdown。

建议提交：`test(ui): complete uGUI compatibility and stress matrix`

## 7. 全组件核对清单

以下清单必须进入 U0 的机器可读矩阵。

### Core / Graphics

- Graphic、MaskableGraphic、Image、RawImage、Text
- GraphicRegistry、GraphicRebuildTracker、GraphicRaycaster、IGraphicEnabledDisabled
- Mask、RectMask2D、IMask、IMaskable、MaskUtilities、StencilMaterial
- CanvasUpdateRegistry
- FontData、FontUpdateTracker
- SetPropertyUtility、Misc

### Layout

- CanvasScaler、LayoutElement、LayoutGroup、HorizontalOrVerticalLayoutGroup
- HorizontalLayoutGroup、VerticalLayoutGroup、GridLayoutGroup
- ContentSizeFitter、AspectRatioFitter
- LayoutRebuilder、LayoutUtility、ILayoutElement/Controller/SelfController/Group/Ignorer
- DrivenRectTransformTracker 等 UnityEngine 依赖能力的 Overload 等价实现

### Culling / Material / Mesh Effects

- IClipper、IClippable、IClipRegion、ClipperRegistry、Clipping、RectangularVertexClipper
- IMaterialModifier
- IMeshModifier、BaseMeshEffect、Shadow、Outline、PositionAsUV1

### Controls

- Selectable、Button、Toggle、ToggleGroup、Slider、Scrollbar、ScrollRect、Dropdown、InputField
- Navigation、ColorBlock、SpriteState、AnimationTriggers
- DefaultControls

### EventSystem

- EventSystem、EventHandle、ExecuteEvents、EventTrigger、EventTriggerType
- 全部事件接口
- BaseEventData、AxisEventData、PointerEventData、RaycastResult、MoveDirection
- RaycasterManager、BaseRaycaster、GraphicRaycaster、PhysicsRaycaster、Physics2DRaycaster
- BaseInput、BaseInputModule、PointerInputModule、StandaloneInputModule、TouchInputModule

### Utility / Animation

- VertexHelper、VertexHelperExtension
- IndexedSet、ObjectPool、ListPool、ReflectionMethodsCache 的等价或明确不需要的适配
- CoroutineTween、TweenRunner

## 8. 每阶段统一验收要求

每个阶段至少执行：

```text
Scripts\Windows\Build.bat Debug -skip-project-generation
node Tests\UI\run-validation.mjs Debug --managed --scripting-regression
node Tests\Skinning\run-validation.mjs Debug --editor-camera
```

涉及输入或编辑器产品循环时，还要增加对应的确定性产品测试，不得只调用组件方法模拟结果。

每阶段必须覆盖：

- 首次打开场景，不选择对象、不进入 Play 的可视组件首帧；
- Play 生命周期和编辑态生命周期不混淆；
- 项目 C# 继承、virtual override 和热重载；
- enable/disable、移除组件、销毁 Actor、切换场景、关闭 session；
- Inspector 字段 XML 往返及热重载迁移；
- Overlay、Camera、World Space；
- 默认渲染和独立 RenderThread；
- 真实 GPU 像素/几何/事件时序；
- 非法输入、资源卸载和跨线程拒绝。

构建完成后比较：

```text
Bin/Debug/OvEditor/OvEditor.exe
Build/Debug/OvEditor.exe
```

两者大小、时间和 SHA-256 必须一致。修改 ScriptCore/ManagedHost 后执行：

```text
node Examples\ManagedUI\build.mjs <Python 3 绝对路径>
```

## 9. 测试组织建议

- `Tests/UI/native_ui_validation.cpp`：原生组件、资源代际、Frame、GPU、RenderThread。
- `Tests/UI/managed_ui_validation.cpp`：真实 CLR、项目子类、热重载、生命周期。
- `Tests/UI/event_system_validation.cpp`：确定性输入、raycast 和事件时序。
- `Tests/UI/text_validation.cpp`：glyph metrics、SDF distance/atlas generation、C# 排版和多尺度像素。
- `Tests/UI/controls_validation.cpp`：Selectable 和各控件状态机。
- `Tests/UI/stress_validation.cpp`：规模、场景切换、在途帧和资源回收。
- `Tests/UI/fixtures/`：按阶段拆分项目程序集，避免单一 ManagedContracts 无限膨胀。
- `Examples/CompleteUI/`：最终综合示例；ManagedUI 保持为基础扩展示例。

所有 evidence 写入唯一的 `Build/UIValidation/<config>-<id>`，结果文件记录：commit、命令、编译器、runtime、渲染模式、测试项和退出码。

## 10. 提交纪律

- 一阶段一提交；一个提交必须可构建并通过该阶段全部测试。
- 只暂存本阶段文件，提交前检查 `git diff --cached --check` 和 `git diff --cached --stat`。
- 不提交 `.overload/`、`ManagedUI.local.ovproject`、Build 产物、绝对路径配置和 validation evidence。
- 不把其他任务的脏文件混入提交。
- 每阶段更新兼容矩阵和 `Tests/UI/UGUI-COMPATIBILITY.zh-CN.md`。
- 在 U13 之前不得使用“完整兼容 uGUI”的表述。

## 11. 新会话启动指令

可将下面内容直接作为新会话首条消息：

```text
请阅读 H:/overload/Overload/UGUI_IMPLEMENTATION_PLAN.zh-CN.md、
H:/overload/Overload/Tests/UI/UGUI-COMPATIBILITY.zh-CN.md，并检查当前源码和 git 状态。
以 dbbe799 为 uGUI 基线，但保留工作区其他任务的未提交改动，不要 reset、checkout 或覆盖。
参考 Build/Reference/uGUI 中固定的 2018.4 / 7ae6166c9b673a966f62905953f4bc6b3153a675。
从计划中第一个未完成阶段开始直接实现、构建、真实渲染/输入验证、更新兼容矩阵和示例，并为该阶段单独 commit；不要只给方案。
C# 负责布局、图形生成、事件分发和控件行为，C++ 只负责原生组件、资源、输入快照、网格提交和渲染底层。
任何阶段结束前必须运行 UI 基础回归和 editor-camera 回归，并确认 Build/Debug/OvEditor.exe 已更新。
```
