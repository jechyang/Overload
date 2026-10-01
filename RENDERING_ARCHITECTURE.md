# 当前渲染架构

基于当前源码的架构图、资源数据流、线程时序及组织评估见 [渲染架构图解（2026-09-11）](RENDERING_ARCHITECTURE_DIAGRAM.zh-CN.md)。图解同时标注了本文部分概念名称与当前实现的对应关系。

A0–A6 的实施契约见 RENDERING_ARCHITECTURE_REFACTOR_PLAN.md，实际验收状态见 RENDERING_ARCHITECTURE_A6_VALIDATION.md。旧源码基线保留于 Git 935423e；当前生产与测试使用同一 RenderFrontend。

## 数据流与模块边界

产品视口/游戏提供请求 → ExtractScene / ExtractFrame / ComposeFrame → 只读快照与 RenderPlan → Prepare / PrepareFrame → 只读 PreparedFrameData → Record / RecordFrame → RenderCommandList → RenderFrame → 固定 GL owner 执行、呈现及退役。

| 原 Renderer 职责与状态 | 当前归属及输入输出 |
| --- | --- |
| Begin/Draw/End 生命周期、当前场景与相机 | RenderFrontend 组织单视图或帧的 Extract/Prepare/Record；SceneViewRequest/SceneRenderRequest 显式携带相机、目标尺寸和阶段。产品 RenderFrame 单独管理提交/完成。 |
| ECS 遍历、材质覆盖、single-use 输入 | OvCore 的 SceneRenderExtractor 在稳定 Main 读取期复制模型矩阵、共享 palette、材质参数和资源代际，发布 RenderSceneSnapshot。 |
| 阴影相机、反射面与渐进捕获 | SceneFrameExtractor 提取独立 View 并声明资源读写；`CReflectionProbe` 持有面游标、ping-pong 与 `m_captureHistory`。Light 只接受阴影准备所需的相机位置。 |
| 相机矩阵、裁剪、距离排序 | ViewPreparer / MeshFeatureProcessor 消费快照；各范围独占结果，按源顺序汇合并确定 Stage 顺序。 |
| Pass/Feature 注册、RTTI 查询和共享 Descriptor | `RenderPipeline` / `ScenePipeline` 选择阶段，`RenderPlan` 声明 View、Pass、附件与资源依赖；不再使用可变 Feature 钩子。 |
| DrawPacket、材质/灯光/骨骼绑定 | DrawPacketBuilder 在只读 PreparedFrameData 基础上构建范围绑定；发布前检查每个任务结果。 |
| DrawEntity、模型绘制和工具状态 | ToolDrawCollector 的 CollectMesh/CollectModel 接收显式 ToolDrawInput；立即捕获材质、几何与 palette，延迟阶段仅消费拥有型数据。 |
| 编辑器 Grid/Outline/Gizmo/Picking | 各 Collector 在 Main 提取；产品面板提供请求。PickingReadback 在命令完成后发布，不能从 Worker 回查面板或 Actor。 |
| GPU 动态缓冲、记录缓存、实际 Draw | MeshCommandRecorder 只记录拥有型操作；资源初始化由 ResourceExecutor 的 owner 执行，上传/绑定/绘制在 GL owner 执行。Driver 是执行后端。 |
| Blit、曝光/Bloom/FXAA 与历史 | `PostProcessPipeline` 记录显式命令，viewport owner 保持目标及历史；`RenderCommandList::History` 区分录制、消费成功、失败与取消。 |
| 全局渲染器服务与当前帧状态 | 产品 Context 持有 frontend、资源执行器与提交设施；Worker 不使用渲染器 ServiceLocator，也不维护可变全局相机。 |

OvRendering 的 Prepare/Record 不读取 Scene/Actor/Component 或借用源 Material。几何、纹理、程序与目标快照维持消费资源生命期；快照并不允许后台读取后来被修改的 GPU 动态内容，内容依赖仍由计划和命令顺序保证。

## 并发、失败与剩余串行步骤

RenderTaskCoordinator 在单视图中分块 Prepare、构建绑定和 Record。多视图模式把各 View 的 mesh/binding/record range 扁平提交到同一 JobSystem，再按 View 固定槽顺序合并，避免嵌套等待并让大主视图与阴影/反射共享 worker。每个 job 拥有独立输出槽；WaitAllPassive 等待终态后仍逐项检查异常/成功状态，部分提交失败同样汇合已提交任务。成功结果按固定槽顺序合并，完成快慢不改变绘制顺序。取消、异常或缺失输出都不能发布成功帧。

以下串行部分是明确的 owner 或顺序要求：

- Main 负责 Scene 更新后的稳定提取、工具/UI 读取和可变 single-use 材质输入消费；提取期必须保留其可观察顺序。材料程序快照首次建立涉及 ResourceExecutor，后续 Worker 不查询源对象。
- 排序合并依赖范围结果，阶段/视图与 GPU 资源生产消费顺序固定；无须让不同 job 写同一列表或记录器缓存。
- postprocess 设置、viewport 目标、曝光和渐进反射历史有单独的主线程 owner；录制结束不等于历史提交成功。
- GPU 缓冲上传、GL 调用、读回、呈现和最终资源释放归同一个执行 owner。多个录制任务可共享资源句柄，但不会在 CPU 录制时写动态 GPU 内容。

render_commands=false 时直接执行同一份显式命令；true 时通过产品 RenderFrame 提交。render_thread=false 时 Main 执行；true 时交给固定 RenderThread，后者要求开启帧提交。parallel_recording 控制同一准备/录制实现的调度方式，两个选项默认仍为 false。A6 的并发与图像验证不构成性能提升结论；阶段 4 的性能记录仅适用于当时样本。

CPU job 依赖、RenderPlan 的 GPU 资源顺序、`RenderCommandList::History` 的跨帧成功关系分别表达。退出时先停止生产，再等待任务和在途帧，最后由正确的资源 owner 退役；不能用对象析构顺序替代 drain/join。

## Stage 与扩展边界

`RenderStage` 枚举与 `DescribeStage` 提供集中映射（technique、排序、反射/阴影接收、`staticSceneSupported`）。当前支持组合已有 Stage（Opaque、Transparent、SceneUI、OverlayUI、ShadowCaster、ReflectionCapture、Picking），不等于已提供可任意注册新 Stage 或脚本 SRP SDK。`PipelineRunner::Run` 是薄的 View/Stage 切片提交助手，显式调用见 stage validation；产品帧由 RenderFrontend/RecordFrame 按 RenderPlan 合并命令。

## 最小管线扩展

Tests/RenderingCommands/inspection_pipeline.h 使用既有 Opaque/Transparent/SceneUI 阶段组合两个视图，声明输出目标的多个纹理附件，经 ComposeFrame 冻结并验证计划，再调用既有 PrepareFrame/RecordFrame。示例不修改场景提取核心、协调器或后端。

ComposeFrame 要求拥有型视图和完整资源契约；错误诊断定位到 Pass/View 或资源编号。缺少生产者、错误阶段/尺寸/附件或无效视图会被拒绝。需要跨帧历史或 postProcessAfter 的流程由 ExtractFrame 的相应 owner 构建，不能用手写计划绕过。

新增 CPU 功能应先确定快照字段及 owner，再实现局部 Processor/Preparer/Collector，并通过明确阶段/资源契约组合。当前没有可视化流水线编辑器、脚本 SRP SDK 或新的 Renderer 继承体系。运行入口和测试边界见 Tests/RenderingCommands/README.md。
