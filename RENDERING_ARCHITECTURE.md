# 当前渲染架构

当前产品资源图、线程时序和物理资源复用见 [渲染架构图解（2026-10-06）](RENDERING_ARCHITECTURE_DIAGRAM.zh-CN.md)。

A0–A6 的实施契约见 RENDERING_ARCHITECTURE_REFACTOR_PLAN.md，实际验收状态见 RENDERING_ARCHITECTURE_A6_VALIDATION.md。旧源码基线保留于 Git 935423e；当前生产与测试使用同一 RenderFrontend。

## 数据流与模块边界

产品视口/游戏提供请求 → Main 提取场景/工具/UI 计划片段 → ComposeFrames 合成唯一 RenderPlan → Compile 验证访问与存活区间 → RenderTargetPool 实现物理映射 → PrepareFrame / RecordFrame → RenderCommandList → RenderFrame → 固定 GL owner 执行、呈现及 GPU 退役。

| 原 Renderer 职责与状态 | 当前归属及输入输出 |
| --- | --- |
| Begin/Draw/End 生命周期、当前场景与相机 | RenderFrontend 组织单视图或帧的 Extract/Prepare/Record；SceneViewRequest/SceneRenderRequest 显式携带相机、目标尺寸和阶段。产品 RenderFrame 单独管理提交/完成。 |
| ECS 遍历、材质覆盖、single-use 输入 | OvCore 的 SceneRenderExtractor 在稳定 Main 读取期复制模型矩阵、共享 palette、材质参数和资源代际，发布 RenderSceneSnapshot。 |
| 阴影相机、反射面与渐进捕获 | SceneFrameExtractor 提取独立 View 并声明资源读写；`CReflectionProbe` 持有面游标、ping-pong 与 `m_captureHistory`。Light 只接受阴影准备所需的相机位置。 |
| 相机矩阵、裁剪、距离排序 | ViewPreparer / MeshFeatureProcessor 消费快照；各范围独占结果，按源顺序汇合并确定 Stage 顺序。 |
| Pass/Feature 注册、RTTI 查询和共享 Descriptor | `RenderPipeline` / `ScenePipeline` 选择阶段；唯一 `RenderPlan` 声明作用域/代际句柄、附件、子资源访问、Load/Store 与固定顺序依赖。独立操作不要求 View。 |
| DrawPacket、材质/灯光/骨骼绑定 | DrawPacketBuilder 在只读 PreparedFrameData 基础上构建范围绑定；发布前检查每个任务结果。 |
| DrawEntity、模型绘制和工具状态 | ToolDrawCollector 的 CollectMesh/CollectModel 接收显式 ToolDrawInput；立即捕获材质、几何与 palette，延迟阶段仅消费拥有型数据。 |
| 编辑器 Grid/Outline/Gizmo/Picking | 各 Collector 在 Main 提取；产品面板提供请求。PickingReadback 在命令完成后发布，不能从 Worker 回查面板或 Actor。 |
| GPU 动态缓冲、记录缓存、实际 Draw | MeshCommandRecorder 只记录拥有型操作；资源初始化由 ResourceExecutor 的 owner 执行，上传/绑定/绘制在 GL owner 执行。Driver 是执行后端。 |
| Blit、曝光/Bloom/FXAA 与历史 | `PostProcessPipeline::Build` 展开明确节点，scratch 由产品池管理；viewport owner 保持输出与曝光历史，Probe 保持反射历史。History 区分录制、消费成功、失败与取消。 |
| 全局渲染器服务与当前帧状态 | 产品 Context 持有 frontend、资源执行器与提交设施；Worker 不使用渲染器 ServiceLocator，也不维护可变全局相机。 |

OvRendering 的 Prepare/Record 不读取 Scene/Actor/Component 或借用源 Material。几何、纹理、程序与目标快照维持消费资源生命期；快照并不允许后台读取后来被修改的 GPU 动态内容，内容依赖仍由计划和命令顺序保证。

## 产品资源图与临时目标

Editor 保持 Asset → Game → Scene → Camera Preview → ImGui 的 GPU 顺序；Grid、
Outline、Gizmo、Picking 和读回也在图中。Game 包含最终 backbuffer blit、调试 UI
和 Present 边界。两个执行模式消费同一产品图，没有计划后的 opaque 尾插命令。
材质及 UI 的实际纹理采样进入访问声明；跨场景共享导入按存储代际归一化。
片段作用域映射保留原闭包的句柄，拒绝过期代际和缺失生产者，后加入片段不能
凭空提升初始化状态。固定顺序验证 RAW/WAR/WAW、反馈、Clear/Load/Discard、
mip 和 cube face；当前不自动裁剪或重排。

编译后的存活区间允许同帧不同视口复用完全兼容的纹理/FBO，颜色格式、mip、
过滤和 wrap 均须一致。物理资源映射在发布 Worker 输入前冻结，租约保留到 GPU
fence 退役；即时执行同样退役。Texture Debugger/材质导入上一代池资源时也会
pin 相应存储，多个在途消费者独立释放。输出、曝光/反射历史和资产不成为 scratch。
自动曝光的亮度 scratch 使用 R32F、1024×1024 和完整 11 级 mip 链；
保留原采样与 32 位精度，完整链约 5.33 MiB。数值对照与内存实测见
[亮度缓冲验收](FRAMEGRAPH_LUMINANCE_VALIDATION.zh-CN.md)。

产品池默认闲置预算 256 MiB。Main 执行预留单套，RenderThread 产品预留两套，
覆盖现有两帧容量；租用资源不受闲置淘汰影响。预热命中只做 CPU 查询，缺失目标
才批量交给 GL owner 创建。预算不足、resize 或效果变化仍可能产生分配和 owner
等待。关闭效果停止申请对应临时目标，已闲置缓存按预算保留，不承诺立即清空。
当前图需要的闲置预留会更新 LRU 使用时间，防止 resize 后每帧淘汰并重建备用目标；
成功实现的计划会推进未使用帧龄，默认经过 60 个成功计划后自动回收旧尺寸和停用效果
缓存。三视口 1080p→720p→1080p 实测中，60 帧后闲置约从 236.83 MiB 降到 110.83 MiB，
继续关闭效果 60 帧后降至 31.64 MiB，均保持暖池零分配。当前预留目标受每个新计划刷新，
active lease 和外部 pin 不受过期规则影响；关闭
自动过期可保留旧行为，闲置预算淘汰和手动 Trim 保持独立。释放沿用资源 owner 队列，
不会在热路径同步等待 GL。

ImGui 原生/拥有型回放共享设备程序代际，切换时仅重建字体纹理 owner。
已发布字体可在 CPU 使用；首次出现字形会触发字体纹理上传，属于冷资源准备，
仍可能等待 GL owner。暖态无 owner 等待的验证先覆盖实际 FPS 文本所需字形。

`CompiledRenderPlan::Dump` 给出节点、依赖、内容版本和逻辑存活峰值，
`RenderResourceMapping::Dump` 给出实际纹理与物理槽，池统计记录分配、命中、
别名、淘汰和闲置/租用字节。估算峰值与查询纹理字节不能等同全部驱动 VRAM。
开发前测量见 [F0 基线](FRAMEGRAPH_F0_BASELINE.zh-CN.md)，产品组合与最终回归见
[F5](FRAMEGRAPH_F5_VALIDATION.zh-CN.md) 和 [F6](FRAMEGRAPH_F6_VALIDATION.zh-CN.md)。

## 并发、失败与剩余串行步骤

RenderTaskCoordinator 在单视图中分块 Prepare、构建绑定和 Record。多视图模式把各 View 的 mesh/binding/record range 扁平提交到同一 JobSystem，再按 View 固定槽顺序合并，避免嵌套等待并让大主视图与阴影/反射共享 worker。每个 job 拥有独立输出槽；WaitAllPassive 等待终态后仍逐项检查异常/成功状态，部分提交失败同样汇合已提交任务。成功结果按固定槽顺序合并，完成快慢不改变绘制顺序。取消、异常或缺失输出都不能发布成功帧。

以下串行部分是明确的 owner 或顺序要求：

- Main 负责 Scene 更新后的稳定提取、工具/UI 读取和可变 single-use 材质输入消费；提取期必须保留其可观察顺序。材料程序快照首次建立涉及 ResourceExecutor，后续 Worker 不查询源对象。
- 排序合并依赖范围结果，阶段/视图与 GPU 资源生产消费顺序固定；无须让不同 job 写同一列表或记录器缓存。
- postprocess 设置、viewport 目标、曝光和渐进反射历史有单独的主线程 owner；录制结束不等于历史提交成功。
- GPU 缓冲上传、GL 调用、读回、呈现和最终资源释放归同一个执行 owner。多个录制任务可共享资源句柄，但不会在 CPU 录制时写动态 GPU 内容。

render_commands=false 时在 Main 同步执行并退役同一产品图；true 时通过产品 RenderFrame 提交。render_thread=false 时 Main 执行；true 时交给固定 RenderThread，后者要求开启帧提交。parallel_recording 在即时和提交模式中控制同一准备/录制实现的调度方式。A6 的并发与图像验证不构成性能提升结论；阶段 4 的性能记录仅适用于当时样本。

CPU job 依赖、RenderPlan 的 GPU 资源顺序、`RenderCommandList::History` 的跨帧成功关系分别表达。退出时先停止生产，再等待任务和在途帧，最后由正确的资源 owner 退役；不能用对象析构顺序替代 drain/join。

## Stage 与扩展边界

`RenderStage` 枚举与 `DescribeStage` 提供集中映射（technique、排序、反射/阴影接收、`staticSceneSupported`）。当前支持组合已有 Stage（Opaque、Transparent、SceneUI、OverlayUI、ShadowCaster、ReflectionCapture、Picking），不等于已提供可任意注册新 Stage 或脚本 SRP SDK。`PipelineRunner::Run` 是薄的 View/Stage 切片提交助手，显式调用见 stage validation；产品帧由 RenderFrontend/RecordFrame 按 RenderPlan 合并命令。

## 最小管线扩展

Tests/RenderingCommands/inspection_pipeline.h 使用既有 Opaque/Transparent/SceneUI 阶段组合两个视图，通过 SceneFrame.AddView 声明输出附件与实际采样，经 PrepareFrame 编译一次，再调用 RecordFrame。示例不修改场景提取核心、协调器或后端。

ComposeFrame 要求拥有型视图和完整资源契约；错误诊断定位到 Pass/View 或资源编号。缺少生产者、错误阶段/尺寸/附件或无效视图会被拒绝。需要跨帧历史的流程由 ExtractFrame 的相应 owner 构建，不能用手写计划绕过。ComposeFrames 合并未准备的拥有型片段，生产链在 PrepareFrame 中统一编译一次。

编辑器 `Window > Frame Graph` 提供最近一次准备完成的产品图诊断：Pass 访问和依赖、子资源、资源存活区间与物理复用、池缓存统计和闲置预算/清理。资源流图可拆分内容版本，逐访问显示读入/写出版本及 mip/layer 范围；多个版本仍共享物理存储，不保留旧纹理内容。诊断快照只保存值，冻结不会持有 GPU 存储；默认关闭，打开才采集，关闭即停止。冻结只固定计划，池计数保持更新；这是提交前计划，不代表 GPU 已完成。具体边界和验证见 [Frame Graph 诊断面板验收](FRAMEGRAPH_DIAGNOSTICS_VALIDATION.zh-CN.md)。

新增 CPU 功能应先确定快照字段及 owner，再实现局部 Processor/Preparer/Collector，并通过明确阶段/资源契约组合。当前没有可视化流水线编辑器、脚本 SRP SDK 或新的 Renderer 继承体系。运行入口和测试边界见 Tests/RenderingCommands/README.md。
