# 渲染架构重构 A0：语义基线与迁移契约

日期：2026-09-09。A0 已完成源码核对；本次仅冻结契约，不改变渲染行为，不宣称新前端或新并发路径已实现。下一项为 A1。

## 1. 固定基线与复放方法

- 架构对照提交：620ef10fb56c287581bbd55b0c8904ef9cca26b7。
- Git tree：496f84d2a048851b265f4f0169de718f374cc003。
- 核对开始时，暂存区及受跟踪工作树相对 HEAD 无差异。未跟踪项为 .zcode/、CODEX_CHANGE_LIST_TEST.txt、Docs/、Examples/ 和 RENDERING_ARCHITECTURE_REFACTOR_PLAN.md；其中只将用户指定计划纳入本次提交，其余不作为构建或测试输入、不纳入提交。
- Build/、Bin/、Bin-Int/ 中的已有产物不作为冻结源码或本次通过证据。必要资产、依赖、测试脚本使用上述提交的受跟踪版本；不能混用旧对象文件与新头文件。
- 历史结果见 [阶段 4 验收](MULTITHREADING_PARALLEL_RECORDING_VALIDATION.md)。此处引用已有验收，不表示 A0 重跑过构建、图像或产品测试。

复现时从上述完整提交建立独立 checkout，并在该 checkout 运行 gen_proj.bat vs2022、完整构建 Overload.sln 的 Debug/x64 和 Release/x64；新实现 checkout 也独立生成并构建。不要把旧 Renderer.cpp 单独链接进改变布局后的新库。保留 Tests/Skinning/run-validation.mjs 中每个历史模式原有 baseline，不替换为本次架构基线。

优先复放命令（在各自 checkout 根目录，按需附加 --vs 安装目录）：

~~~text
node Tests/Skinning/run-validation.mjs Debug --parallel-recording
node Tests/Skinning/run-validation.mjs Release --parallel-recording
node Tests/Skinning/run-validation.mjs Release --command-list
node Tests/Skinning/run-validation.mjs Release --editor-stress
node Tests/Skinning/run-validation.mjs Release --game-thread
~~~

命令组合及输出位置以 [测试 README](Tests/RenderingCommands/README.md) 为准。上面冻结的是可执行旧实现和已有确定性用例，不是新增 A1 的跨版本自动对拍器；A1 应在新切片测试中补齐其串行/Worker/旧路径比较。

复放必须分别新建测试场景、材质、Renderer、探针、曝光历史和输出目录；逐帧施加同一相机、模型变换、时间和请求序列。每轮结束先排空产品帧，再销毁资源或重建模式。不能在同一份可变状态上连续运行两个路径：singleUse、ON_DEMAND 捕获、逐面游标和曝光会被提前消费。图像、逻辑 draw 参数/顺序分别比较，GPU 句柄归一到逻辑资源身份。不比较地址、时钟时间戳或 signature 数值。

旧不透明排序含材质地址：跨进程多材质用例必须先核对并记录逻辑材质组顺序；若顺序不同则不可直接宣称 draw 序列等价。A1 首片使用单一标准材质；多材质排序验证采用稳定资源分配的受控用例，或同进程分别重置后复用同一组源材质身份。不能随意放宽像素容差规避地址分组的既有非确定性。

## 2. 当前调用图与生命周期

~~~text
Game::Render / RecordSceneCommands
  -> SceneRenderer::BeginFrame -> DrawFrame -> EndFrame
Editor::RenderViews / RecordRenderFrame
  -> AssetView -> GameView -> SceneView（按可见/有效视口选择）
  -> AView::Render / RecordRenderCommands
  -> SceneRenderer（Asset/Game）或 DebugSceneRenderer（Scene）
SceneRenderer::BeginFrame
  -> 收集活动灯光/探针 -> CompositeRenderer::BeginFrame
  -> ABaseRenderer::BeginFrame（目标、清除、Camera::CacheMatrices）
  -> Feature / Pass 的 OnBeginFrame -> ParseScene -> FilterDrawables
CompositeRenderer::DrawFrame
  -> 有序 Pass（每 Pass 重置目标/viewport）
  -> DrawEntity -> Feature::OnBeforeDraw -> 材质/几何/拥有型 packet
  -> Feature::OnAfterDraw
EndFrame -> Pass/Feature 收尾 -> 清 Descriptor -> 解除视图绘制状态
拥有型列表 -> RenderFrame 聚合 -> Seal -> Submit / RenderThread::Submit
  -> GL owner 按序执行 -> GPU fence 完成 -> 退役
~~~

单次 BeginFrame/EndFrame 是一个视图录制范围，不能等同产品 RenderFrame。Editor 产品帧按 Asset → Game → Scene → 最终 ImGui 合成；SceneUI 是视图内 UI Pass，位于后处理后、编辑器 Debug 前。Game 聚合场景及需要的 UI。EndFrame、FinishCommandRecording、Seal、入队、GPU 完成、最终资源退役是不同边界；Driver::OnFrameCompleted 或 OnAfterDraw 也不是 GPU fence。

目标命名冻结：RenderFrontend 是唯一产品渲染入口类型；RenderView 是一次相机/尺寸/裁剪及 Stage 能力的数据；PreparedFrameData 是 CPU 准备发布结果；RenderFrame 继续是既有产品提交与退役单元。一个产品帧可容纳多个视口及辅助 View，每个视口拥有独立历史/资源上下文，不使用全局“当前 View”。

## 3. Renderer 方法与成员归属

以下按共同 owner 合并列项，覆盖旧入口及其状态；不是要求逐项创建公共类。

| 旧入口/状态 | 接管职责与作用域 | 接入 / 最迟退出 |
| --- | --- | --- |
| IRenderer::BeginFrame/DrawFrame/EndFrame | RenderFrontend 接受视口请求；PipelineRunner 录制；产品方负责 RenderFrame 提交 | A1 / A6 删除 IRenderer |
| ABaseRenderer::BeginFrame/GetFrameDescriptor、m_frameDescriptor | Extract 捕获值，ViewPreparer 计算矩阵；目标清除由 Pipeline 明确记录 | A1、A4 / A6 |
| EndFrame/IsDrawing、m_isDrawing、s_isDrawing | 前端请求及录制作用域；禁止以全局 bool 代替任务汇合 | A1 / A6 |
| CreatePipelineState、m_basePipelineState | 显式流水线默认状态；Record 不询问活动场景 | A1、A4 / A6 |
| SetViewport/BindTarget/RecordPassState/PrepareClear/Clear、m_passState | RenderPlan 决定目标，CommandRecorder 私有状态和命令载荷 | A1、A4 / A6 |
| ExecutePassState/ExecuteClear/ExecuteGeometryDraw、m_driver | 既有 Driver/baregl 执行工具，固定 GL owner | A1–A4 / A6 |
| IsDrawable/PrepareGeometryDraw/DrawEntity | MeshFeatureProcessor 校验/准备，DrawPacketBuilder 组装；只消费拥有型几何和数值 | A1、A3 / A6 |
| Blit/BlitWithRetainedTextures、m_unitQuad、RetainedTexturesDescriptor | 显式全屏绘制工具；输入、输出、状态、sampler 归属分别传入 | A5 / A6 |
| m_emptyTexture2D/Cube、录制 fallback 纹理 | 资源适配器持有，GL owner 创建；PreparedDraw 持有明确代际 | A1、A3 / A6 |
| m_previousMaterialSignature、m_recordedMaterial、m_preparedMaterial | DrawBindings 构建器 / 录制局部缓存；不能跨任务共享可写别名 | A3 / A6 |
| Begin/Finish/CancelCommandRecording、m_commands、SubmitRenderOperation、GetCommandCompletion | CommandRecorder 输出接现有 RenderCommandList；失败由前端汇合收尾 | A1、A3 / A6 |
| SetCommandRecordingJobs、parallel 开关及成员 | 主线程 RenderTaskCoordinator；保留串行/并行产品选择及现有默认值 | A1–A3 / A6 |
| UseRenderHistory | 管线/历史 owner 生成令牌，复用原成功、失效、退役协议 | A4、A5 / A6 |
| ProfileGpuScope | 命令侧标记及既有 GL owner profiling | A3 / A6 |
| CompositeRenderer::Add/Get/Remove/HasFeature、m_features | 前端配置注册，执行顺序显式冻结；功能拆为 Preparer/Processor/Collector | A2–A5 / A6 |
| Add/GetPass、m_passes、m_currentPass | ScenePipeline / EditorPipeline 配置与 PipelineRunner，不再提供任意服务查询 | A2、A4 / A6 |
| Describable 的 Add/Get/ClearDescriptors | Extract 输入适配 → 类型化不可变数据，撤销场景借用 | A1–A5 / A6 |
| SetDrawRecordingEnabled/RecordDrawOperation/RetainDrawTexture、DrawRecording/DrawPacket、m_recording、ExecuteDrawPacket | DrawPacketBuilder + 私有 CommandRecorder + 原执行后端 | A3 / A6 |
| preDrawEntityEvent/postDrawEntityEvent | 显式观察接口，分别标记录制/提交/完成；迁移 FrameInfo 后删除事件 | A3、A5 / A6 |
| SceneRenderer::ParseScene、Scene/SceneDrawable/SceneDrawablesDescriptor | SceneRenderExtractor；Actor、Component、Transform、Material 借用不出提取边界 | A1–A4 / A6 |
| FilterDrawables、SceneDrawablesFilteringInput、SceneFilteredDrawablesDescriptor、DrawOrder | ViewPreparer / MeshFeatureProcessor；显式 View/Stage 列表与排序 | A1、A2 / A6 |
| DrawModelWithSingleMaterial | 收集模型请求进入相同准备/提交链 | A1–A5 / A6 |
| SceneRenderer 构造与灯光/探针收集 | ScenePipeline 配置 + Extract 的灯光/探针输入 | A1–A4 / A6 删除 SceneRenderer |
| DebugSceneRenderer 构造、DebugSceneDescriptor | EditorPipeline + EditorOverlayCollector，选中/悬停/Gizmo 数据按值或稳定 ID | A5 / A6 删除 DebugSceneRenderer |
| AView 的 framebuffer/resize/展示、SceneView 的 GetPass 拾取控制 | 视口资源适配器通过 ResourceExecutor 管资源；面板发请求并校验/应用结果 | A5 / A6 |

源码入口：[ABaseRenderer.h](Sources/OvRendering/include/OvRendering/Core/ABaseRenderer.h)、[CompositeRenderer.h](Sources/OvRendering/include/OvRendering/Core/CompositeRenderer.h)、[SceneRenderer.h](Sources/OvCore/include/OvCore/Rendering/SceneRenderer.h)、[DebugSceneRenderer.h](Sources/OvEditor/include/OvEditor/Rendering/DebugSceneRenderer.h)。

## 4. 全部内置 Pass、Feature 与主动工具

下表验证代号对应第 8 节。生产注册处：SceneRenderer.cpp、DebugSceneRenderer.cpp、AssetView.cpp、GameView.cpp、Game.cpp。Pass 使用 multimap，按 order 升序，同 order 保留插入次序；Feature 的 unordered_map 遍历没有可移植的注册顺序保证。

| 当前功能 | 当前顺序/策略与目标归属 | 阶段 | 验证 |
| --- | --- | --- | --- |
| ShadowRenderPass（Shadows） | 10000；Shadow View/资源计划 + Mesh 的 ShadowCaster 提交 | A4 | V3、V4 |
| ReflectionRenderPass | 15000；探针捕获计划 + ReflectionCapture 列表，保留逐面捕获 | A4 | V3、V4 |
| OpaqueRenderPass（Opaques） | 20000；静态 Mesh 列表先接入，再统一 Opaque | A1、A2 | V1、V2 |
| TransparentRenderPass（Transparents） | 30000；完整透明列表排序后提交 | A2 | V2、V3 |
| PostProcessRenderPass（Post-Process） | 40000；首个活动 CPostProcessStack → 设置快照；AutoExposure → Bloom → Tonemapping → FXAA，保留 IsApplicable 与 ping-pong | A5 | V3、V5 |
| UIRenderPass（UI） | 50000；SceneUI 列表，保留材质和 stencil 状态 | A2 | V2、V5 |
| GridRenderPass（Grid） | 60000 首项；Scene/Asset 的网格数据与专用 Pass | A5 | V5 |
| DebugCamerasRenderPass | 60000 第二项；相机模型/调试灯光数据收集 | A5 | V5 |
| DebugReflectionProbesRenderPass | 60000 第三项；探针可视化及显式反射绑定 | A5 | V5 |
| DebugLightsRenderPass | 60000 第四项；灯光 billboard/模型收集 | A5 | V5 |
| DebugActorRenderPass | 60000 第五项；选中/悬停 Outline、Gizmo、Actor 调试几何 | A5 | V5 |
| PickingRenderPass（Picking） | 60000 第六项；Picking View + 数据/结果归属，主线程身份校验后应用 | A5 | V5 |
| EngineBufferRenderFeature | ALWAYS；Frame/View 时间和相机、Object 模型/user 矩阵绑定 | A1、A3 | V1、V3 |
| LightingRenderFeature | ALWAYS；Frame/View 灯光准备和显式调试覆盖 | A3 | V3、V5 |
| SkinningRenderFeature | ALWAYS；已发布 pose 转换、palette identity 与任务局部缓存 | A3 | V1、V3 |
| ReflectionRenderFeature | WHITELIST Opaque/Transparent；探针选择、接收常量和 cubemap | A3、A4 | V3、V4 |
| ShadowRenderFeature | WHITELIST Opaque/Transparent/UI；接收矩阵和 shadow sampler | A3、A4 | V3、V4 |
| FrameInfoRenderFeature | Game/GameView/Scene/Asset 注册 ALWAYS；draw 统计改为明确观察接口 | A3、A5 | V2、V5 |
| DebugShapeRenderFeature | Scene 为 FRAME_EVENTS_ONLY，Asset 为 NEVER；显式形状收集器，保留清理时点 | A5 | V5 |
| DebugModelRenderFeature | Scene/Asset 为 NEVER，但主动 DrawModel；显式调试模型服务 | A5 | V5 |
| OutlineRenderFeature | NEVER；主动 stencil/膨胀轮廓步骤，保留 OUTLINE_PASS 与 fallback | A5 | V5 |
| GizmoRenderFeature | NEVER；主动轴/球/高亮绘制，输入快照 | A5 | V5 |
| 最终 ImGui / Canvas | 产品帧最后，继续 UIManager 拥有型快照，不增设 Mesh Stage | A5 接口核对 | V5、V6 |

NEVER 禁用自动钩子，不代表工具没有绘制。ARenderFeature::FRAME_EVENTS_ONLY 仍接收 Begin/End，但不进入逐 draw 钩子。未知派生 Feature/Pass、自定义 IMesh 不能自动获得 Worker 许可；现有拥有型录制的拒绝行为继续有效。

## 5. Stage、technique、排序冻结

Stage 名称冻结为 Opaque、Transparent、ShadowCaster、ReflectionCapture、SceneUI、Picking。Stage 能力、材质 technique（旧 Drawable::pass）与 Shader FeatureSet 分开存储。

| Stage | 参与条件与 technique | 排序/回退 |
| --- | --- | --- |
| Opaque | GEOMETRY 可见；非 UI、非 blend；默认 pass（nullopt/空名） | drawOrder 升序 → 旧材质分组键升序 → 距离近到远 |
| Transparent | GEOMETRY 可见；非 UI、blend；默认 pass | drawOrder 升序 → 距离远到近，不按材质分组 |
| SceneUI | GEOMETRY 可见；IsUserInterface 优先；默认 pass | drawOrder 升序 → 距离远到近 |
| ShadowCaster | 活动对象、SHADOW 可见、有效原材质且 IsShadowCaster；SHADOW_PASS | 原组件/mesh 遍历顺序；缺少该 pass 选 ShadowFallback；强制深度、禁混合/颜色/双面裁剪 |
| ReflectionCapture | REFLECTION 可见，排除 UI，IsCapturedByReflectionProbes；REFLECTION_PASS | Opaque 后 Transparent；使用捕获相机、无主视图 override/fallback |
| Picking | 提取既有 actor/mesh/light/probe/gizmo 的可拾取输入；PICKING_PASS | 保留 PickingRenderPass 原遍历及 PickingFallback/Gizmo 专用规则 |

Outline/Grid/后处理为专用工具或整体 Pass，不新增空对象 Stage。SceneRenderer 中 overrideMaterial 优先于原材质，原材质缺失才选 fallback；非空但无效的材质直接跳过。fallback 身份匹配时豁免 includeUI/includeOpaque/includeTransparent 过滤，之后仍按实际材质分类。蒙皮 feature override 只在该材质支持时构建。

等 key 按原收集序号稳定；并行块按预定范围合并。旧 materialKey 是源材质地址数值：新 Extract 可捕获其相对分组序号（不保留可解引用指针），不得改用快照分配地址或任务结束次序排序。透明必须全局合并排序。裁剪保留 bounds、世界变换、skin boundsScale、相机开关及 frustum override 规则。

Shader::GetVariant 当前规则：指定 pass 不存在时返回默认 pass 的空 FeatureSet program；pass 存在但 FeatureSet 不存在时返回该 pass 的空 FeatureSet program。A2 保留这些已定义回退，映射表显式记录；未知/未声明的 Stage 则跳过或拒绝，不能以 Shader 默认回退冒充 Stage 支持。依据：[Shader.cpp](Sources/OvRendering/src/OvRendering/Resources/Shader.cpp)、[SceneRenderer.cpp](Sources/OvCore/src/OvCore/Rendering/SceneRenderer.cpp)。

## 6. 参数与历史唯一 owner

| 参数/状态 | 现有写入和优先级 | 新 owner / 发布边界 |
| --- | --- | --- |
| 材质数值、sampler、stateMask、featureSet | Filter 先选材质及 stateMask；OnBeforeDraw 后绑定材质；Drawable feature override 高于材质 features | Extract 捕获版本，DrawBindings 合成独立值；A3 |
| 引擎 Frame/View/Object 常量 | EngineBuffer 的时间、相机覆盖、模型/user；辅助 Pass SetCamera 后恢复 | ViewPreparer + Object 输入；任务私有绑定；A1/A3/A4 |
| 灯光 UBO/SSBO 与显式假灯光 | Lighting 在帧开始准备，draw 使用当前明确覆盖/默认灯光 | 灯光 Preparer 输出，录制局部绑定；A3 |
| 蒙皮 palette/identity、上传与绑定缓存 | 动画先评估发布；Skinning 按支持情况选 pose 或 identity | Extract 持有 pose 版本，Prepare 转换；GL 缓存留执行端；A3 |
| _ShadowMap / _LightSpaceMatrix | Shadow Feature 在 draw 前对源 Material 写 singleUse；覆盖此前同名属性 | Shadow 接收绑定为唯一写入者；A3/A4 |
| 反射 sampler/探针 uniform | Reflection Feature 选探针并在 draw 前写材质/记录探针数据 | 探针计划、选择结果、接收绑定分离；A3/A4 |
| _InputTexture、后处理属性 | Blit/Effect 先写材质再经 Feature 链；不是外部材质不可见的操作 | 全屏工具显式输入及 Effect 设置快照；A5 |
| Picking 颜色、_OutlineColor、Gizmo/网格参数 | 主动工具对所选材质写入后绘制 | EditorOverlayCollector + 每 draw 覆盖；A5 |
| 反射 captureHistory、面游标、完成 cubemap | 当前主线程先计划/推进，命令历史负责失败失效；不是立即 GPU 完成 | 每探针/场景 owner，任务只产生候选；A4 |
| 曝光渐变、ping-pong、视口表面 | Effect/视口实例状态，失败后初始化历史；resize 经过资源 owner | 每视口/场景历史及资源适配器；A5 |

当前没有通用 Feature 写入优先级：按 unordered_map 遍历调用 OnBeforeDraw，多个 writer 同名属性时最后写者生效但跨构建不保证顺序。A3 内置语义字段只留一个 writer；扩展冲突要求明确顺序或报错，不虚构现有固定的 Feature 优先级。

singleUse 不是整帧事务。即时 UploadProperties 按当时属性遍历顺序上传并消费成功前缀；完整 snapshot/PrepareSnapshot 在校验完成后消费本次有效上传的属性，随后 Encode 失败不恢复已消费属性。当前变体没有的 uniform、签名缓存复用与失败位置均沿用 material_validation / parallel_recording_validation 行为。兼容入口按旧 draw 顺序解析一次，不能先为全部对象消费后又让旧钩子消费一次。A1 若暂不支持某输入，应在消费前确定归属/拒绝，不能失败后悄悄回退重画。

## 7. 线程、任务依赖与失败

| 当前执行者 | 读写与等待 | 新边界 |
| --- | --- | --- |
| Main 更新/提取 | 更新 ECS、动画、面板；稳定期内 ParseScene/FilterDrawables 可借用场景 | 只有 Extract/受控回调读游戏状态，冻结后才能 Prepare |
| 现有 Parse/Filter Worker | 读取稳定期借用；独占分块输出，主线程按范围合并；阈值 128 个 ModelRenderer / 256 个 Drawable | 复用算法，替换成快照输入；不是本轮新增的并行能力 |
| Main 视图/Pass 准备 | 写 Camera 缓存、Descriptor、Feature 缓存、Material singleUse、历史；同步资源需求等待 GL owner | 暂留旧路径，按 A1–A5 分离，不直接并发调用旧 Renderer |
| 命令编码 Worker | 已捕获值与保留的 program 元数据；独占命令槽，无 GL | A3 扩至绑定/Record 私有输出，不嵌套调用调度入口 |
| GL owner | Main 或固定 RenderThread；资源创建/resize、命令执行、fence/回收 | 继续既有 ResourceExecutor/RenderFrame 协议 |
| Main 汇合/产品 | 等 CPU 任务、队列容量、需要的完成；成功且身份匹配才应用交互 | Worker 不等待 Main/GL 资源服务，不创建并阻塞等待同池子子任务 |

JobSystem 源码契约（[JobSystem.cpp](Sources/OvTools/src/OvTools/Jobs/JobSystem.cpp)、[JobHandle.h](Sources/OvTools/include/OvTools/Jobs/JobHandle.h)）：

- Submit 返回 handle；无效 handle 视为已结束，跨池依赖拒绝。异常被捕获并可交给 error handler；失败同样释放后继依赖。
- IsCompleted 包含 Completed、Failed、Retired，且公开 handle 没有结果/异常查询接口。每个任务必须自带独占 Result（成功标志、异常、输出）；不靠一个 fan-in handle 推断全部成功。
- Wait/WaitAll 会参与执行其他 ready Job，Main 还可能执行 MainThread affinity 回调。录制汇合优先使用 WaitAllPassive：仅构造池的线程可调用，仅接受 Any affinity；传入任务的依赖也不得要求被阻塞的 Main 工作。
- 任务完成通过 release/acquire 状态同步结果可见性；异常回调及捕获释放在正常执行完成状态发布前完成。停机先 join Worker，再 retire 排队依赖闭包；不要拿析构当成功汇合或在仍有提交者时销毁池。
- RenderCommandList::EncodeDeferred 当前仅在池构造线程、至少 2 Worker 且至少 128 deferred draw 时走并行；Main 计算自身块，Worker 独占其余槽，WaitAllPassive 后逐项检查 complete/error。A1 的一个 Prepare Job 要直接 Submit，不能靠这个阈值证明已执行准备任务。

拟迁移任务冻结如下。失败规则 F：捕获异常/显式失败；阻止依赖阶段启动和 Publish；停止新增提交；汇合所有已提交 Job 后才销毁输入/输出；命令帧走原 Fail/Cancel 与历史失效。取消只是请求，不等于任务已退出。

| 任务 / 阶段 | 只读输入与前置条件 | 唯一输出 owner / 成功发布点 | 失败 |
| --- | --- | --- | --- |
| Extract + 管线需求（A1 起，Main） | 更新/pose 完成，场景稳定；视口请求 | 本次 Snapshot builder；去掉可写别名后冻结 | 丢弃未发布 builder；已消费 singleUse 不承诺回滚 |
| View/候选准备（A1/A2，Any） | 冻结 snapshot、View 值、Stage 映射 | 每 View/范围 Result；Main 全部检查并稳定合并 | F，不能发布部分列表 |
| 灯光/pose 转换（A3，Any） | 提取的灯光/pose 版本及必要 View | 各功能私有 Result；依赖者只读成功输出 | F |
| DrawBindings / packet 准备（A3，Any） | 可见列表及其灯光/pose/接收参数全部成功 | 每 draw 范围私有 Result；封口 PreparedFrameData | F；不写共享 Material/cache |
| 辅助 View 计划（A4，Main） | 探针/灯光请求、资源代际、历史令牌有效 | 每 View 计划，资源由 GL owner 受控满足；再启动相同 View 任务 | F；无效生产者明确诊断 |
| Record/Encode（A3 起，Any） | 已发布 PreparedFrameData、冻结 Pass 状态及资源 | 每命令块私有上下文；Main 按计划槽位合并封口 | F；不执行失败帧的 GPU 前缀 |
| Editor 收集/Effect 编排（A5，Main 可串行） | 当前 UI/选中 ID/显示设置、Effect 快照 | 每视口覆盖层输入；消费只读数据 | F；旧交互结果不应用 |
| 产品 Seal/Submit（Main） | 全部视图、UI 成功且 CPU Job 退出 | 既有 RenderFrame；GPU 完成由原 fence 报告 | 复用原失败、历史失效及退役 |

CPU 主/影/反射准备可以独立完成；GPU 仍按 Shadows → Reflections → Opaque → Transparent → PostProcess → SceneUI → Debug → 最终 UI 排序。GPU 内容依赖不能由 shared_ptr 存活或 CPU 完成替代。

## 8. 兼容性、验证与允许的诊断

| 代号 | 必须保持的行为 | 现有验证入口 / 新增要求 |
| --- | --- | --- |
| V1 | 静态/蒙皮、源销毁、资源代际、identity、实例与矩阵 | snapshot_validation.cpp、draw_input_validation.cpp、frame_input_validation.cpp、packet_validation.cpp；A1 新增 Extract 后源变更/销毁及无 GL Prepare |
| V2 | 材质选择/回退、排序、等 key、singleUse、变体 | material_validation.cpp、parallel_recording_validation.cpp；A2 补显式 Stage/全局透明排序，A3 补绑定冲突 |
| V3 | 多 View 常量隔离、阴影/反射/曝光、后处理及失败恢复 | target_state_validation.cpp、command_list_validation.cpp、parallel_recording_validation.cpp；A4 补资源生产/消费校验 |
| V4 | 资源存活、GPU 内容有效性、历史提交/失效、全部已提交 Job 排空 | render_frame_validation.cpp、resource_lifetime_validation.cpp、resource_executor_validation.cpp、parallel_recording_validation.cpp |
| V5 | Game/Scene/Asset、网格/轮廓/Gizmo、拾取/材质拖放、完整 UI、resize/最小化/切场景/播放 | editor_command_validation.inl、editor_frame_validation.inl、editor_thread_validation.cpp、editor_stress_validation.cpp、game_thread_validation.cpp |
| V6 | 主线程/RenderThread 模式、队列/关闭、最终 UI | render_frame_validation.cpp、render_thread_validation.cpp、game_thread_validation.cpp、editor_stress_validation.cpp |

上述文件均位于 Tests/RenderingCommands/。新增并发验收须用有超时的闩锁/事件和线程 ID 证明实际执行；A1 至少一个 Worker 执行相同 Prepare，A2 至少两个独立任务实际执行，A3 证明私有绑定/录制块乱序完成仍确定合并。少量 draw 导致编码串行回退不能算并发验证通过。

允许在后续对应阶段增加的诊断：未声明 Stage、缺失当前帧生产者、无效历史、错误 View/附件面/尺寸、非法附件反馈、重复字段 writer、未批准扩展、任务无成功输出。禁用阴影时显式无阴影或有效导入，缺失反射采用既有 fallback；不能采样存活但内容无效的资源。已定义的 Shader 回退、材质消费前缀、可见对象排序不得混入行为修正。

两个源码疑点单独记录：ShadowRenderPass 声明 kMaxShadowMaps=1，但遍历中的 lightIndex 没有递增，不能把现有行为写成“只处理首个光”；SceneRenderPass::PrepareStencilBuffer 无条件设置 stencil，构造的 m_stencilWrite 未控制该方法。A4/A2 必须先复现并保留实际行为；若要修复，另作显式行为变更，不能在等价重构中悄悄更正。

## 9. A0 完成记录与下一会话

- 数据与线程归属：已冻结上表，特别区分 View、产品帧、CPU 成功与 GPU 完成。
- 实际任务依赖和并发验证：已核对现有实现；本阶段无新增任务，无新并发通过声明。
- 职责迁移完成：本阶段交付迁移清单；生产职责尚未迁移。
- 行为验证通过：本次检查文档路径、Git 差异和源码一致性；无源码/头文件/构建变更，因此未重跑构建或产品测试。
- 尚留适配器：全部旧产品调用仍有效；本次未新增适配器。A1 输入适配服务 Game/三视口，最迟 A5 迁移调用者、A6 清零；命令后端桥只复用现有 RenderCommandList/RenderFrame，不建立第二提交后端。

下一会话只做 A1 的主相机静态不透明切片：薄前端、提取快照、最小 ViewPreparer、只读发布、已有命令后端桥；先旧/新串行对照，再由同一个 Prepare 函数在 Worker 执行。覆盖源变更/销毁、无 GL 准备、失败不发布且先汇合。明确对象/Stage 归属，旧路径暂管未迁移功能，同一 draw 不重复消费 singleUse。完成后更新计划及本记录、验证、独立 commit，再提示开启新会话；不要接着推进 A2。


## 10. A1 实施交接（2026-09-09）

主相机静态不透明的拥有型命令切片已实现，具体入口、所有权、限制和验证见 [A1 记录](RENDERING_ARCHITECTURE_A1_VALIDATION.md)。上面的 A0 固定源码与语义契约保持不变。

Debug/Release 全量构建均成功，两个配置均通过旧拥有型命令、新串行、新 Worker 的精确图像/绘制数据对照及源销毁、无 GL 准备、失败先汇合检查。后续排查已把旧即时入口首帧 98 像素差异定位到测试活 UBO 同步回读，改用 GPU 暂存后读取，并增加每模式独立进程及有/无 trace 对照。A0 固定生产源码未修改，像素容差未放宽。A1 全项仍保留活跃反射 Uniform Block（binding 1）的绑定完整性核对，下一会话继续 A1，不进入 A2；详情见 A1 记录。
