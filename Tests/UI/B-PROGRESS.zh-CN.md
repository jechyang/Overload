# B 工作包交接：U2 图形通路与 U5 遮罩

2026-09-21。B 自有实现与独立验收已完成；整体阶段状态由 A 汇总共享集成和总矩阵后确认。U2/U5 共用 renderer/快照状态，本次作为一个 B 里程碑提交。

## 实现

- 完整 UIVertex/VertexHelper 流、版本化 88 字节 ABI、独立 UI Geometry、多材质/纹理槽和 pop 绘制。
- Graphic dirty/rebuild、注册、材质与 modifier 链、颜色 tween；Shadow、Outline、PositionAsUV1。Image/RawImage 已提取，完整 Image 模式归 C。
- RectMask2D、CanvasGroup、MaskableGraphic、8-bit 嵌套 Mask、cull 通知、画家顺序 stencil hit testing；F 的 GraphicHitSource 消费生产帧。
- Frame 固定顶点/索引/纹理/字体页；RenderResources 在主线程固定 GPU 几何、材质状态和纹理，交付不可变渲染命令。
- 修复 Camera Canvas 非中心内容水平镜像、失效 Actor 遮罩清理、编辑模式尚未 Awake 的 Mask/modifier 移除后重建。

完整 API、坐标、限制与桥接见 [B-GRAPHICS-CONTRACT.zh-CN.md](B-GRAPHICS-CONTRACT.zh-CN.md)。

## 验证证据

独立命令：`node Tests/UI/run-graphics-validation.mjs --runtime`。原生构建：`Build.bat Debug -skip-project-generation`。

- Debug 构建：H:/overload/Overload/Build/UGUI-B/debug-build-4.log，退出 0。
- 最新 B 全部通过：H:/overload/Overload/Build/UGUI-B/graphics-EbQdsI/result.json。40 个 managed contracts；生产 GPU 顶点回读、多材质、三种 Canvas、嵌套 stencil/rect clip、负向边界、字体页；真实 CLR modifier 的禁用/启用/删除、Mask 删除、热重载、父节点禁用及 session 关闭回收。
- 两种执行模式完整 framebuffer bytes 完全一致；真实 CLR 拒绝后台线程 CanvasRenderer 访问，关闭后 collectible 项目上下文为零。
- A 通报总回归全部通过：H:/overload/Overload/Build/UIValidation/Debug-VSMChf/result.json；早于最后 Mask/modifier ReleaseNativeComponents 修复，最后两文件由上述 B fresh CLR 回归覆盖。
- F 联合回归：Build/UIValidation/F-Debug-FGeYRM；D：Build/UGUI-D/layout-osZabR/result.json。由对应负责人生成。
- A 确认 editor-camera 最终通过：Build/JobsValidation/Skinning/Debug-editor-camera-editor/result.json；隐藏窗口 OS cursor warp 明确 SKIP，实际 ImGui 导航点击默认/RT 另测通过：Build/EditorUX/Scripting/results.json。
- Bin/Debug/OvEditor/OvEditor.exe 与 Build/Debug/OvEditor.exe：43059712 字节、SHA256 35d4d5a8bc2c2da8729688c5259c5fcb841b5bb854c42cd833414dc64b745fc9。Build/UGUI-B/editor-binary-comparison.json。

## 依赖与共享集成

验收时 HEAD aa6467e（F），已含 A f6ac031、D 223ae46/fee3e11；同时使用共享工作树中的 A 宿主桥接和 E 字体后端。B 提交不能脱离这些共享依赖单独摘取构建。

A 持有 GameplayBridge/Instances/Host ABI/Scene TickUI 和最终示例 generation；B 只提交自有 UIBridge、CUI、UI 快照文件，以及经 A 同意的 PreparedFrameData/ViewPreparer 最小状态覆盖。CUI 包含已交接的 D driven 字段钩子。E 持有 Fonts/Font.h 和字体资源类。

交接时 A 正在刷新最后 managed generation，并收齐 E/共享桥接提交。B 没有待集成的私有补丁，不修改总阶段矩阵；本记录不宣称全部 uGUI 完成。

## 明确适配

- 矩形裁剪使用 UI fragment 精确 discard；stencil 使用每 draw 不可变 PipelineState。不是硬件 scissor，三种 Canvas 保持相同局部裁剪语义。
- StencilMaterial 返回不可变模板状态，不另建 GPU 材质变体缓存。
- 当前引擎无独立 timeScale 后端，A scaled/unscaled tick 相同；API 与测试支持两种 delta。
- 显式上限：65535 顶点、393210 索引、64 材质槽、8 stencil 层。op53 使用最近提取帧，首个输入前需要首帧提取。
- 材质解析须在 RenderResources 主线程准备阶段完成；不得把仍含 materialPath 的 UI Frame 交给 worker 查询资源管理器。性能优化属 U13。

## B 提交文件

- Sources/OvCore/include/OvCore/ECS/Components/CUI.h
- Sources/OvCore/src/OvCore/ECS/Components/CUI.cpp
- Sources/OvCore/src/OvCore/Scripting/CSharp/UIBridge.cpp
- Sources/OvCore/include/OvCore/UI/Frame.h
- Sources/OvCore/src/OvCore/UI/Frame.cpp
- Sources/OvCore/include/OvCore/UI/RenderResources.h
- Sources/OvCore/src/OvCore/UI/RenderResources.cpp
- Sources/OvCore/include/OvCore/UI/Geometry.h
- Sources/OvRendering/include/OvRendering/Data/PreparedFrameData.h
- Sources/OvRendering/src/OvRendering/Core/ViewPreparer.cpp
- Sources/OvManaged/Overload.ScriptCore/UI.Graphic.cs
- Sources/OvManaged/Overload.ScriptCore/UI.Native.cs
- Sources/OvManaged/Overload.ScriptCore/UI.Vertex.cs
- Sources/OvManaged/Overload.ScriptCore/UI.MeshEncoding.cs
- Sources/OvManaged/Overload.ScriptCore/UI.Effects.cs
- Sources/OvManaged/Overload.ScriptCore/UI.GraphicRegistry.cs
- Sources/OvManaged/Overload.ScriptCore/UI.GraphicHitSource.cs
- Sources/OvManaged/Overload.ScriptCore/UI.Image.cs
- Sources/OvManaged/Overload.ScriptCore/UI.Clipping.cs
- Sources/OvManaged/Overload.ScriptCore/UI.Mask.cs
- Sources/OvManaged/Overload.ScriptCore/UI.VertexHelperExtension.cs
- Tests/UI/GraphicContracts.cs
- Tests/UI/graphics_pipeline_validation.cpp
- Tests/UI/graphics_runtime_validation.cpp
- Tests/UI/fixtures/GraphicRuntime.cs
- Tests/UI/run-graphics-validation.mjs
- Tests/UI/B-GRAPHICS-CONTRACT.zh-CN.md
- Tests/UI/B-PROGRESS.zh-CN.md
