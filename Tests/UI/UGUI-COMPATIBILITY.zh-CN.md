# uGUI 分层与兼容范围

实现方向是参考官方 uGUI，把布局、图形生成和控件策略放在可继承的 C# 类中，C++ 仅提供原生组件、资源解析、不可变帧快照、网格上传和渲染提交。当前完成了基础布局及 Graphic 网格链路，但不是完整 uGUI 兼容层。

## 源码基准

官方仓库为 <https://github.com/Unity-Technologies/uGUI>，分支 `2018.4`，固定提交 `7ae6166c9b673a966f62905953f4bc6b3153a675`。

本地参考源码位于 `Build/Reference/uGUI`，保留原 LICENSE。`node Tools/UI/reference-ugui.mjs --check` 可校验版本和工作区，逐文件索引位于 `Build/Reference/ugui-files.json`。参考许可证为 Unity Companion License。

## U0 完整运行时清单

机器可读清单位于 `Tests/UI/ugui-runtime-matrix.json`，覆盖参考提交 `UI` 和
`EventSystem` 下的全部 84 个运行时文件、178 个类型声明（其中 148 个公开可访问）。
清单包含嵌套事件、枚举、委托、obsolete 接口和内部辅助类型；不将 Properties 下的
程序集属性文件计为运行时类型文件。每项记录对应 Overload 类型、实施阶段、状态、
测试编号及差异。现有基础能力保持 `partial`，尚未开始的实现使用 `pending`，均不计入完成。

`reference-ugui.mjs --check` 同时验证固定提交、工作区、文件 SHA-256 和声明覆盖率。
`node Tests/UI/run-validation.mjs Debug --matrix-only` 验证漏项、重复项、未知状态、
缺测试的 implemented/adapted、错误声明和不完整 blocker 均会被拒绝。
该命令只证明清单完整性，不代表运行时兼容验收通过。

固定版本的 `IClipRegion.cs` 实际声明的是 `IClipper` 和 `IClippable`，没有
`IClipRegion` 类型；`CoroutineTween` 是命名空间/文件，并非独立类型。
`BaseVertexEffect`、`IVertexModifier` 等已过时类型也保留在清单中，避免遗漏。
UnityEngine 外部依赖（如 UIVertex、DrivenRectTransformTracker 和移动软键盘）单列，
后续仍须实现或明确适配，不能借文件范围排除完成条件。
`node Tools/UI/reference-ugui.mjs --check --complete` 会拒绝任何仍为 pending、partial
或 blocked 的类型及外部依赖，作为 U13 的最终完成闸门。

## 当前代码归属

| 功能 | 实现位置 |
| --- | --- |
| 锚点、pivot、矩形、画布尺寸和缩放结果 | C++ `CRectTransform` / `CCanvas` |
| 屏幕适配策略、物理单位、宽高匹配 | C# `CanvasScaler` |
| 最小/期望/伸缩尺寸和优先级 | C# `LayoutElement` / `LayoutUtility` |
| 横向/纵向布局、间距、对齐和自适应 | C# `LayoutGroup` / `HorizontalOrVerticalLayoutGroup` / `ContentSizeFitter` |
| 布局与图形重建队列 | C# `LayoutRebuilder` / `CanvasUpdateRegistry` |
| 顶点生成和基础图形 | C# `Graphic` / `Image` / `RawImage` / `VertexHelper` / `UIVertex` |
| 原生网格、纹理/材质引用和提交代际 | C++ `CCanvasRenderer`，C# `CanvasRenderer` 包装 |
| 三种 Canvas 模式、裁剪、不可变快照和 GPU 提交 | C++ UI Frame / RenderResources / 渲染帧提取器 |
| 原生组件句柄与父子节点访问 | C# `RectTransform` / `Canvas` / `Actor` 包装，C++ Gameplay/UIBridge 校验 |

布局和图形策略不在 C++ 中复制一套。项目脚本可继承 `Graphic` 并重写 `OnPopulateMesh(VertexHelper)`；生成的位置、UV、逐顶点颜色和三角形索引通过 `CanvasRenderer` 上传。纹理和基础材质引用随帧快照固定，渲染线程不访问托管对象或活组件。

## 脚本扩展点

框架位于 `Sources/OvManaged/Overload.ScriptCore/UI.*.cs`，公开命名空间为 `Overload.UI`、`Overload.EventSystems` 和 `Overload.Maths`。

- 可继承 `Graphic`，重写 `OnPopulateMesh` 生成任意三角形或多边形，并使用 `SetVerticesDirty` / `SetMaterialDirty` 请求重建。
- `Image` 提供基础纹理四边形；`RawImage` 额外公开纹理和 `uvRect`。二者仍可继续继承和重写。
- 可继承横向/纵向布局组或直接继承 `LayoutGroup`；普通 `Behaviour` 也可自行实现布局接口。
- `CanvasScaler` 的屏幕尺寸、缩放处理和 DPI 查询可重写。
- `UIBehaviour` 提供尺寸、父节点和画布变化回调；`ICanvasElement` 可进入布局及 PreRender/LatePreRender 阶段。
- `Actor.GetComponent<T>()` / `GetComponents<T>()` 支持原生组件、托管组件和接口查询。

包含 `UIBehaviour` 子类的项目会自动发现 UI 脚本组件；只使用内置组件的项目可加 `[assembly: Overload.UI.EnableUI]`。项目自定义子类位于可卸载的项目程序集，修改并重载脚本无需重编 C++。框架本体位于常驻 ScriptCore，修改框架仍需重建托管宿主。

## 执行与生命周期

渲染前主线程依次更新 RectTransform 视口上下文、运行托管 UI 更新、完成布局和 Graphic 重建，然后提取不可变绘制快照。Worker 和 RenderThread 只消费复制后的顶点、索引和固定资源代际。

首帧托管 UI 更新前，宿主根据类型元数据只准备当前场景中的 UI 类型和 UI 接口实现，恢复序列化字段；不依赖 Inspector 选择，不提前创建普通游戏脚本，也不触发 Awake/Start。类型标记随项目热重载更新。

`Graphic` 在启用、尺寸/层级变化、字段编辑和热重载后重新登记脏标记；禁用、销毁、场景卸载及 session 释放会撤销队列项并清空原生网格。原生组件句柄带 epoch，删除再添加组件不会让旧包装恢复有效。

## 当前边界

- 不是 `UnityEngine.UI` 的二进制或源码兼容层，使用 Overload 的 Actor/Behaviour 生命周期及命名空间。
- `Image` 当前是基础纹理四边形，不包含 Sprite border、fill、preserveAspect 或 set-native-size 语义；`RawImage` 支持基础 `uvRect`。
- 自定义材质路径可提交，但材质 shader 需自行遵守当前 UI 顶点和 uniform 约定；尚无 uGUI 的 `IMaterialModifier` 链。
- 尚未实现 `MaskableGraphic`、Text、完整 Mask、EventSystem 和具体交互控件。
- RectTransform 仍是画布平面内二维轴对齐布局，未覆盖 Unity 完整三维旋转/缩放编辑语义。
- `GridLayoutGroup`、`AspectRatioFitter`、`DrivenRectTransformTracker` 和完整 CanvasGroup 语义仍未补齐。
- 当前每帧仍会在 C# 侧调度布局根并去重，尚未完成所有 Inspector/原生层级变化的纯增量 dirty 优化。

## 验证与示例

完整基础回归：

    node Tests/UI/run-validation.mjs Debug --managed --scripting-regression

验证使用真实 CLR、真实 OpenGL 和像素读回，覆盖：项目 C# 自定义 `Graphic` 三角形、逐顶点颜色、UV/纹理、基础 Image/RawImage、仅网格 UI pass、三种 Canvas 模式、裁剪、默认及独立 RenderThread 的一致像素、首帧编辑态初始化、脏重建、热重载、失效句柄、销毁/场景卸载和资源快照生命周期。

`Examples/ManagedUI` 提供可运行项目，场景中的红色三角形由项目 C# 重写 `OnPopulateMesh` 生成，绿色卡片由项目 C# `Image` 子类生成。没有启用 Unity 双端差分认证，因此不声称所有 uGUI 行为一致。
