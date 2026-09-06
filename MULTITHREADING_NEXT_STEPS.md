# Overload 多线程改造：后续实施与新对话交接

更新：2026-09-06。工作目录：H:/overload/Overload。

## 从这里开始

**阶段 0、阶段 1 已验收提交。阶段 2 已建立可拥有资源并延后消费的 SceneRenderer 帧命令序列，覆盖目标、上传、draw、反射/曝光 mipmap 顺序；源 renderer/场景销毁后及真实动态 pass 对拍通过。动态反射/曝光历史已加入取消/失败失效传播及下一帧重建；Editor debug/picking/readback 已接入 owning 命令与成功完成后的主线程解析；ImGui 拥有型帧快照、编辑器纹理入口迁移与完整/逐面板 UI 录制已验收，统一 RenderFrame 提交、GPU fence 回收与 Editor/Game 整帧捕获入口已验收。产品循环接入与逐帧输入/拾取仍待收尾，尚无独立 Render 线程。阶段 2、3 均未全部完成，最新验收见 MULTITHREADING_RENDER_FRAME_VALIDATION.md。**

本文件是新对话的首要入口。后续阶段按先后依赖推进，每次完成一个可独立验收的增量。下文的阶段编号用于细化原计划第 12 节；阶段 1 当前实现见 3.4/3.5/3.6 节及对应三份验收文档，其余设计建议不是已实现接口。

总体路线：CPU 任务系统 → 蒙皮评估并行 → 可交接的渲染命令 → Game/Render 双线程（A 形态）→ 多 Pass/View 并行命令录制 → RHI/Vulkan → 视瓶颈演进到 fiber/job graph。

## 1. 当前可依赖的基线

- C++20、Premake、Windows / VS2022 v143。核心模块为静态库，保持编译警告视为错误。
- JobSystem 已有依赖计数、Submit、Wait/WaitAll、ParallelFor、工作窃取、MainThread 亲和队列、异常回调；Chase-Lev 和 MutexDeque 双实现已做对照。
- Editor/Game 的 Context 已创建并注册 JobSystem。
- Scene::Update 新增接收 JobSystem 的重载，Editor/Game 已使用；连续、无脚本且仅含已核实组件的 Actor 可进行蒙皮批次评估，在下一处任意回调前等待并发布。原串行重载保留。
- ParseScene 与 FilterDrawables 已按连续块并行、按块序合并，返回前等待所有任务，包括异常展开路径。
- Parse 串行门槛 max(128, 2×worker)，Filter 为 max(256, 2×worker)；大批次目标约 4×worker 个块。不要未经测量重新调整。
- **当前 GL 操作仍在主线程；尚无独立 Render 线程，也没有跨帧渲染数据交接。** 当前并行处理依赖场景更新完成后数据稳定。
- 每个编辑器视图有独立 renderer。Filter 也会在 DrawFrame 内的反射 pass 调用，不能假设它只在 BeginFrame 运行。

### 阶段 0 的验收结论

| 项目 | 结果 |
|---|---|
| 双 deque 冒烟、依赖图和压力回归 | 通过 |
| ASan：停机共享后继、捕获/回调、异常及唤醒等边界 | 通过 |
| 实际引擎 Debug/Release 对拍 | 每配置 580 组，结果一致 |
| 隐藏 OpenGL 离屏图像 | RGBA 逐像素一致，37,341 个非背景像素 |
| Tracy 工作线程 | JobWorker-0 至 JobWorker-10；主线程参与；649 次 Parse 和 649 次 Filter，各 44 任务均在阶段结束前完成 |
| Tracy Wait 回归 | 1000 次约 0.8 ms 工作，Wait 中位数 0.803 ms，最大 1.026 ms；没有复现约 15 ms 停顿 |
| Tracy 本地队列唤醒 | 20 次均由其他 worker 完成，入队到子任务开始最大约 0.318 ms |
| 最终完整解决方案增量构建 | Debug/x64、Release/x64 均退出 0，0 警告、0 错误 |
| 构建后资源与程序复制 | 两配置编辑器/游戏输出及代表性模型、着色器与源文件一致 |

这是阶段 0 范围的验收，不代表完整编辑器项目场景、多视图交互、所有平台或未来跨帧并发都已验证。Tracy 区间重叠也不等于形式化无竞争证明。

## 2. 必须保持的约束

1. **明确读写所有权。** 只读输入需要有可证明的稳定期；每个 job 独占自己的输出。const 裸指针本身不保证资源存活或其他线程不修改。
2. **结果发布晚于计算完成。** 暂存输出全部成功后再发布 pose/版本等状态；失败不能把半成品作为新姿态或新帧暴露。
3. **保持可观察顺序。** multimap 相同 key 的插入顺序、透明物体顺序、动画时间/停止/手动姿态行为不能悄悄变化。
4. **Wait 是等待终态，不是成功保证。** Failed/Retired 也视为完成；失败后继目前继续执行。调用方需要明确错误观测及结果有效性策略。
5. **避免等待闭环。** 不得等待自己、后继或依赖被阻塞主线程的任务。现有断言不能检测所有传递依赖环。
6. **MainThread 指线程池的构造线程。** 它不能直接充当未来 RenderThread 的亲和性标记；需要独立提交队列/亲和定义。
7. **停机先停止生产，再结束消费者。** 现有 JobSystem 析构不能与外部 Submit/Wait/Pump 并发。未来持久任务/Render 线程必须显式 drain/join，不能只依靠 Context 成员析构顺序。
8. **GL 上下文单一所有者。** 阶段 1、2 的 worker 不调用 GL、窗口 API 或 UI；Render 线程引入时再统一调整上下文、GPU 资源创建和销毁。
9. ServiceLocator 的 Provide/注册不是并行接口；不要让 worker 修改服务注册表。
10. 每阶段保留串行/旧路径作对照，先保证等价，再衡量整体收益。不要把测试里的固定 sleep 间隔当作帧率或调度耗时。

## 3. 阶段 1：蒙皮姿态评估任务化（已完成约定范围验收）

### 3.1 先核实调用与消费顺序

主要入口：

- Sources/OvCore/include/OvCore/ECS/Components/CSkinnedMeshRenderer.h
- Sources/OvCore/src/OvCore/ECS/Components/CSkinnedMeshRenderer.cpp
- Sources/OvCore/src/OvCore/SceneSystem/Scene.cpp
- Sources/OvCore/src/OvCore/ECS/Actor.cpp
- Sources/OvEditor/src/OvEditor/Core/Editor.cpp
- Sources/OvGame/src/OvGame/Core/Game.cpp
- Sources/OvCore/include/OvCore/Rendering/SkinningDrawableDescriptor.h
- Sources/OvCore/src/OvCore/Rendering/SkinningRenderFeature.cpp
- Sources/OvRendering/include/OvRendering/Animation/SkeletalData.h

已核实：Scene::Update 逐 Actor 调用 OnUpdate；CSkinnedMeshRenderer::OnUpdate 包含 SyncWithModel、UpdatePlayback、评估频率限制以及 EvaluatePose。EvaluatePose 会写 local pose、global pose、bone matrices、转置缓存等状态；不能直接把整个现有 OnUpdate 丢给线程池。

调用顺序已核实：Actor 按组件顺序更新，随后立即执行该 Actor 的 Lua behaviours；脚本暴露所有骨骼查询/编辑和播放控制 API，也能访问其他 Actor。不能把姿态统一延迟到 Scene::Update 末尾，也不能提前推进所有 Actor 的播放状态后再执行原顺序脚本。Game 与 Editor 的 Play/逐帧模式均在 Scene::Update 后执行 LateUpdate，再渲染；Edit 模式不推进 Scene 动画。AssetView 当前没有 CSkinnedMeshRenderer，也没有独立动画预览评估。Inspector 的时间修改、模型通知、动画切换、Stop 和手动姿态继续同步完成。

### 3.2 建议实现顺序

1. **分离计算与发布，先保持串行。** 抽出只读评估输入与独立输出：模型/骨架/动画引用及生存期、采样时间、循环参数、节点映射；输出包含姿态和骨骼矩阵。保持原来的采样、矩阵乘法、转置和版本递增语义。
2. **建立稳定的调度边界。** 主线程准备输入、决定是否需要评估、处理播放状态和资源变更；在第一个姿态消费者之前完成等待与发布。生命周期策略必须覆盖模型替换、组件删除、场景切换与暂停。
3. **按组件或组件批次并行。** 首版优先在多个骨骼实例之间并行，每个实例内部保持父子骨骼计算顺序；不要按每根骨骼拆成大量微任务。少量实例走串行快速路径，阈值经实测确定。
4. **发布一致结果。** poseVersion、矩阵数组/count、boundsScale 等对渲染形成一致视图。异步期间禁止重分配消费者正在读的数组；必要时使用独占暂存缓冲及 generation 校验，具体方案根据实际消费边界确定。
5. **接入 Editor/Game 并排查预览路径。** 保留外部直接 OnUpdate/SetTime 等同步调用的合理行为；避免动画时间每帧推进两次或多个视图重复评估同一帧姿态。
6. **接入错误处理与埋点。** 计算失败保留上一份有效姿态或显式失败状态；记录 Prepare/Evaluate/Wait/Publish 区间。错误回调可能在 worker 执行，不得直接操作编辑器 UI。

### 3.3 阶段 1 验收门槛

- 固定时间序列逐帧对拍 local/global pose、骨骼矩阵及其转置、版本和播放状态。浮点比较按原运算顺序决定精确比较或明确容差，不可宽泛放过误差。
- 覆盖循环/非循环、正负速度、暂停/停止/拖动时间、零时长/缺失动画、不同动画源及不兼容映射、手动姿态、评估频率限制、非激活对象。
- 覆盖多个实例共享模型但不同播放时间；输入资源不得被某实例写坏。
- 验证模型更换、组件/Actor 删除、场景切换和退出时没有悬空任务、数据竞争或等待闭环。
- 保留阶段 0 的 Parse/Filter 结果与像素回归；增加连续多帧蒙皮图像比较，检查上一帧/当前帧混用。
- Debug/Release 全解决方案构建通过；必要的 ASan/压力测试通过；Tracy 显示真实并行，并报告准备、等待、发布开销和小批次表现。
- 完成后更新本文件的阶段状态，不自动跳到 Render 线程大改。

### 3.4 已完成的第一个独立增量：同步计算/发布分离

以下记录首增量验收时的状态；第二增量对调度/输入稳定期的扩展见 3.5。

- CSkinnedMeshRenderer 内新增私有 PoseEvaluationInput / PoseEvaluationOutput，以及 PreparePoseEvaluation、静态 ComputePose / ComputeBoneMatrices、无异常 PublishPose。采样和矩阵运算顺序保持不变。
- 计算只读取输入、写独占暂存数组；成功后交换四组数组并发布 manualPoseOverride / poseVersion。双份存储复用容量，代价是额外一份姿态内存。手动姿态编辑同样先计算后发布。
- 普通评估失败保留上次有效姿态；模型重建时旧模型姿态无效，先清空已发布数组，新姿态成功前不暴露占位矩阵。异常仍沿现有同步调用链向上传播，尚未增加异步错误回调。播放时间和播放状态仍在原调用点更新，不是整个组件的事务回滚。
- 输入引用目前仅在同步调用内有效，**不能直接跨帧或交给后台任务**。没有新增 JobSystem 提交，也没有改变 Scene/Actor/Editor/Game 时序，不能据此声称已有蒙皮并行。
- 已验收：Debug/Release 各 11,616 份状态快照与旧源码逐字节一致；各 24 帧真实蒙皮 RGBA 完全一致；矩阵分配失败、非法层级、发布隔离和模型重建恢复检查通过；针对组件的 Release ASan 通过。阶段 0 Parse/Filter 各配置 580 组及原像素回归通过。最终完整 Debug/Release 构建各 0 警告、0 错误，10 项程序/资源复制核对一致。
- 已采集同步路径的 Tracy Prepare/Evaluate/Publish。性能与限制见 MULTITHREADING_SKINNING_VALIDATION.md；尚无 Wait 区间和真实蒙皮并行证据。
- 后续已在第二增量完成稳定批次边界与跨实例调度，没有把整个 OnUpdate 放进线程池。

### 3.5 已完成的第二个独立增量：有界批次并行

- Scene::Update 按原顺序合并连续、激活、无 behaviour 且组件精确类型仅为 CTransform / CModelRenderer / CMaterialRenderer / CSkinnedMeshRenderer 的 Actor。未知/派生组件、未同步模型等形成边界；每批结束并发布后才执行下一处组件/Lua 回调。后序 Actor 时间不会提前推进。
- 主线程准备输入，worker 只读共享骨架/动画并独占实例暂存姿态。借用输入稳定期扩展到本批全部任务结束，仍不能跨帧或允许其他线程并发修改资源。新增 WaitAllPassive 不执行任意队列回调，主线程显式计算自己的一个块；提交失败也先 drain。
- 门槛：至少 2 个 worker、8 个实际评估实例、512 个总节点；最多 2×worker+1 个连续块。小批次串行。普通 OnUpdate、模型变化、Stop、SetTime、手动姿态继续同步，Editor Play/逐帧与 Game 已接入，Edit/AssetView 不增加重复评估。
- 成功实例按原序发布；失败实例保留旧姿态，全部收尾后在主线程重抛首个异常，不从 worker 调用 UI 错误回调。提交失败整批不发布。播放时间/状态不回滚；并行失败时成功同伴仍发布，与串行首错即停的失败语义不同，详见验收文档。
- 两个 Update 重载增加 Actor 销毁快照失效处理，覆盖回调删除后序 Actor 后立即新建的地址复用情况；未扩展成 FixedUpdate/LateUpdate 或销毁当前 Scene 的通用重入支持。
- 自动化验收：Debug/Release 各 9,600 份串/并行精确状态，单 worker 另 1,024 份；实际 Lua 与自定义观察者、组件/模型/Actor 替换、排队主线程删除、计算/部分提交失败及 40 次 Scene 销毁通过；各 24 帧 32 实例真实蒙皮图像精确一致。Release MutexDeque 与针对组件/Scene/JobSystem 的 ASan 通过。原同步旧源码及阶段 0 渲染回归、完整 Debug/Release 构建和复制核对通过。
- 独占 Release、4 worker、真实 Mannequin 测量：16 实例 201.4→93.5 μs，256 实例 3341.3→1182.2 μs。1/4/8 实例仍串行，新增扫描约 0.5～2.5 μs。Tracy 522 个完整批次均有真实并行、最多主线程加 4 worker 重叠，全部先结束任务再发布。
- **第二增量当时的待办已在第三增量处理：** Debug 热区、默认策略、代表性新建项目命中率及真实编辑器交互验收见 3.6。微基准仍不等于整个编辑器帧率收益。

### 3.6 已完成的第三个独立增量：Debug 热区与编辑器项目收尾

- 动画采样/轨道遍历改为只读 span，避免 MSVC 调试迭代器全局注册锁；直接使用与 FTransform 相同运算顺序的 TRS 矩阵表达式，避免每轨道通知器容器和无用 world 分解。没有关闭 Debug 检查，没有改采样/发布/异常/回调边界。
- Debug/Release 保留现有默认批次路径和阈值。新增可选 SkinningUpdateStats 参数，按本次 Update 统计已审查候选批次的命中情况；异常时不是成功保证，不覆盖任意脚本内部的同步姿态操作。
- 独占真实 Mannequin、256 实例：4 worker Debug 串行 16615.1→批次 5740.7 μs，Release 2031.1→673.7 μs；默认 11 worker 分别为 16306.4→2942.9 μs、2005.4→394.1 μs。不是整帧收益。
- 按用户授权新建 SkinningProject，包含 crowd/fragmented/rate_limited 三个生产序列化场景。默认 11 worker、96 次 Update：连续 64 实例全部命中；每四个实例含一个 Lua 组件的碎片批次不命中；15 Hz 场景只有四分之一更新评估。Debug/Release 统计一致。
- 真实 Editor/EditorActions 的 Edit、Play、Pause、单步、恢复、Stop 与串行 Scene 精确一致；真实 Scene/Game/Asset View 的 24 组同视图消费顺序图像一致，11 次动画变化；8 轮延迟切场景/尺寸变化/停止恢复通过。隐藏窗口与独立测试 AppData，未操作桌面输入。
- 旧源码 11616 份状态/24 帧、批次 9600+1024 份状态/24 帧、异常和生命周期、Release ASan/MutexDeque、阶段 0 各 580 组及图像回归均通过。完整 Debug/Release 0 警告 0 错误、10 项产物复制一致。
- Tracy 648 个完整批次均真实并行，所有块和 Wait 均结束后才发布；准备/等待/发布数据、限制和日志见 MULTITHREADING_SKINNING_STAGE1_COMPLETION.md。
- **阶段 1 在本机及上述代表性项目/交互范围内完成验收。** 不是所有平台、任意用户脚本/重入、跨帧资源并发的保证。接下来按阶段 2 分增量实施。

## 4. 阶段 2：渲染命令与帧数据所有权

**目标：先在同一线程运行新的命令消费路径，得到可安全交接的帧数据。**

- 核心入口：Sources/OvCore/src/OvCore/Rendering/SceneRenderer.cpp、Sources/OvRendering/src/OvRendering/Core/ABaseRenderer.cpp，以及 RenderFeature/RenderPass、SkinningRenderFeature。
- 把 DrawEntity 等直接消费场景并调用 GL 的路径分离成命令生成与命令执行，逐类迁移。
- 定义相机/视图/pass 常量、draw 状态、mesh/material 引用、蒙皮数据与资源版本的所有权。不要把持有 Actor 引用或指向组件 vector 的现有 Drawable 浅拷贝后跨帧使用。
- 处理透明排序、多视图、反射/阴影、fallback/override、UI、调试和 picking 等各自的顺序及数据需求。
- GPU 资源生命周期和命令数据释放需要明确消费完成标记；首次迁移可仍同步消费。

**验收：** 单线程新旧路径命令顺序/状态对照、图像对拍；跨视图共享资源无状态污染；完成前资源保持有效；所有既有功能继续工作。

### 4.1 首增量：同线程几何提交边界

- ABaseRenderer 新增受保护的 GeometryDrawCommand、PrepareGeometryDraw、ExecuteGeometryDraw；命令按值持有不可变 PSO/primitive/instances，同次 DrawEntity 内立即消费借用的 IMesh。
- 保持 feature 前处理、材质 Bind/Upload、几何提交、Unbind、feature 后处理顺序；执行几何命令不再读取 Drawable/Material。未提前查询 mesh count，以保持 Bind/查询顺序。
- 现有命令不包含 shader、texture、uniform、palette、viewport、target，不能跨 draw 或跨帧保存。SceneDrawableDescriptor 的 Actor 引用与 SkinningDrawableDescriptor 的矩阵指针尚未迁移；详细所有权表见新报告。
- 新增 `Debug/Release --geometry`，保留阶段 1 ABaseRenderer.cpp 作旧路径；对照实际 GL 顺序/状态、四种几何提交分支和图像。真实编辑器项目回归继续使用 `--editor`。最终结果与日志见 MULTITHREADING_RENDER_COMMAND_VALIDATION.md。
- 下一增量从材质 CPU 参数/变体选择与 GL 上传拆分开始，固定单次属性、signature 和资源持有语义。然后逐步迁移 transform/palette/view/pass/target；阶段 2 整体尚未完成。

### 4.2 第二增量：材质绑定与逐 uniform 参数快照

- Material 的变体选择/签名准备与 GL program 绑定分离；uniform 的 CPU 复制、类型检查、矩阵转置、纹理解析与 GL 上传分离。原 Bind/UploadProperties 包装已接入，Material 对象布局不变。
- 按原顺序逐属性立即执行，之后恢复单次属性默认值；缺失 uniform 不消费，筛选和异常前缀消费语义不变。修复 signature 哈希 seed 未初始化，保证缓存结果确定。
- 数值参数由命令按值拥有；名称借用 shader 元数据，program/纹理仍借用且必须保持稳定。命令不保留源 Material，但尚不支持跨帧保留资源或完整 draw 批量录制。
- `Debug/Release --material` 使用 `50194bf` 的旧材质/几何源码对照；仅归零旧材质副本的未初始化 hash seed。42 组材质/GPU 状态与 GL 调用序列、几何及蒙皮图像对拍，另检查源对象销毁和 CPU 准备；最终集成/构建/测量见 MULTITHREADING_MATERIAL_COMMAND_VALIDATION.md。
- 下一增量：聚合完整 draw 参数快照、资源版本持有和单次属性批量消费边界，再逐类迁移 transform/palette/view/pass/target。仍不引入 Render 线程。

### 4.3 第三增量：材质快照与 shader/texture 代际持有

- 新增可移动的 Material::Snapshot：CPU 录制捕获选定 program、全部支持的活动 uniform 值和纹理 owning 句柄；Execute 不访问源 Material 或资产包装对象。默认 renderer 仍使用第二增量即时接口，此包尚未聚合完整 draw。
- Shader::RetainVariant 持有整组 variant 代际；新代际及 pass/feature 元数据全部准备后交换发布。Texture::RetainTexture 持有实际 GPU 纹理；loader 重载创建新对象。成功替换递增资产局部版本，旧资源活到最后一份持有释放。
- 成功录制后一次性恢复已用 singleUse 属性，缺失 uniform 不消费；类型、所有权、分配失败均不消费。活动 sampler 必须有纹理或 owning fallback，unsupported uniform 类型拒绝；裸纹理必须提供匹配的 owning 句柄。原即时路径的异常前缀消费不变。
- 获取、录制、回放和最终释放均限定 GL owner 且早于上下文销毁；shared_ptr 不提供并发资源修改安全或 GPU fence。快照回放后返回即时 renderer 须使签名缓存失效；不能直接夹入保留旧缓存的 DrawEntity 序列。
- Debug/Release --snapshots：以 8cc2f8c 的即时 Material/ABaseRenderer 对拍，资源表示使用当前源码；原 42 组材质、256 组几何及 24 帧蒙皮精确一致。新 API 另有 32 组全类型 GL 调用/数值对照、8 轮重载/销毁/回收、64 帧旧新代际交替回放、录制回滚边界。真实 Editor、完整构建与 10 项复制通过，详见 MULTITHREADING_MATERIAL_SNAPSHOT_VALIDATION.md。
- 下一增量：mesh/GPU geometry 持有、transform 值快照、独占 palette，并接入真实 DrawEntity 同线程消费。随后迁移 view/pass/target/feature buffers，闭合帧完成及缓存协议。当前仍无 Render 线程或跨帧队列。

### 4.4 第四增量：几何持有与模型/蒙皮上传快照

- Mesh::RetainGeometry 持有独立 VAO/VBO/EBO 及 bounds。真实 PrepareGeometryDraw 对标准 Mesh 捕获这份持有；自定义 IMesh/派生 Mesh 仍借用，保持虚函数行为，只能同次同步消费。Mesh 内部布局变化，需完整重建。
- EngineBufferRenderFeature 的模型/用户矩阵准备与 GL 上传分离，真实 OnBeforeDraw 已接入；model 转置、user 原样以及两个 UBO 区域的上传顺序不变，执行不再读 Drawable。
- SkinningRenderFeature 新增独占 PaletteSnapshot。即时路径只在原缓存 miss 时复制并执行载荷，命中不复制；独立回放清除旧借用键缓存后上传/绑定，以免之后即时调用误判。未启用 skinning 保持 no-op，缺失 palette 保持 identity。
- Debug/Release --draw-inputs 用 b1128a7 的 renderer/material/两类 feature 源码作即时对照，资源内部表示一致；24 帧/72 draws 的 UBO/SSBO 调用序列与像素精确相同。8 轮源对象/数组销毁后回放 32 帧一致，最终 VAO/VBO/EBO 回收，材质及蒙皮旧回归、完整构建、Editor 和产物复制通过。性能成本及范围见 MULTITHREADING_DRAW_INPUT_VALIDATION.md。
- 下一增量：聚合生产 draw packet，并处理反射/阴影前处理产生的纹理/UBO 所有权，再迁移 view/pass/target/lighting。当前仅上述部分接入独立载荷，不具备完整帧或任意 feature 的延迟消费能力。

### 4.5 第五增量：同次 draw packet 与 feature 资源持有

- CompositeRenderer 增加默认关闭、帧外切换的录制模式。真实 DrawEntity 先完成 CPU feature 操作/材质/几何录制，再按原序同步执行；内置 feature 显式 opt-in，派生/未知 feature 和自定义 Mesh 不被隐式许可。
- Material::RecordCachedSnapshot 按原签名分组捕获或复用上次 GPU 值，保留 singleUse 恢复 CPU 默认值但 GPU 缓存继续命中的语义。执行全量绑定/上传，帧边界及模式切换清缓存。录制失败不执行 feature GL，pass 异常后可 EndFrame 并恢复下一帧。
- Reflection 复制 124 字节 probe 常量并持有 cubemap，Shadow 持有深度纹理；Framebuffer 增加 owning 附件接口。Blit、自动曝光、Bloom 的主/额外输入纹理均已接入。动态纹理只保证对象存活，不冻结内容或尺寸。
- Debug/Release 各 12 组反射/阴影/蒙皮/缓存图像与实际 draw 状态对照、24 帧真实 Mannequin 即时/录制对拍、拒绝及恢复/回收检查通过。真实 Editor 三视图全部开启录制，模式/切场景/resize 通过；旧 draw-inputs/material 对照、完整构建和 10 项复制通过，详见 MULTITHREADING_DRAW_PACKET_VALIDATION.md。
- 当前仍是同次 draw 的同步执行，feature 消费者及 view/pass/target/lighting 必须存活稳定，OnAfterDraw 仍同步。未新增 Render 线程或跨帧队列，没有本阶段整帧收益结论。下一增量先迁移相机/视图/灯光常量及 pass/viewport/target，再闭合动态资源生产顺序、UI/debug/picking 和帧完成。

### 4.6 第六增量：相机/时间与灯光输入

- EngineBufferRenderFeature 增加 140 字节相机与 144 字节帧常量载荷，CPU 准备不保留 Camera/Transform；真实 BeginFrame、SetCamera 已接入。录制 draw 捕获当前 camera/time 并恢复 UBO，反射/阴影的 SetCamera 保持时间不变，EndFrame 清除选择。
- LightingRenderFeature 将剔除/矩阵打包与 GPU 上传分离；载荷独占矩阵。录制捕获当前灯光 buffer/binding point 的持有，保留每帧一次上传。BindOverride/Bind 显式选择覆盖/场景灯光，两处编辑器调试覆盖已迁移。
- Debug/Release 各 32 份销毁相机后的 UBO 字节/范围对照、灯光 frustum/空集合/失败/销毁后回放/最终回收、24 组相机切换与 GPU 污染恢复图像通过。原 packet/Mannequin、真实 Editor 三视图、旧 draw-inputs 对拍、完整构建和 10 项复制通过。详见 MULTITHREADING_FRAME_INPUT_VALIDATION.md。
- 灯光 binding 只持有 GPU 对象，不冻结下一帧重写的内容；LightingDescriptor 仍被 shadow pass 借用，FrameDescriptor 仍借用 camera/target。下一增量迁移 pass/viewport/target/clear，再闭合动态资源版本和帧完成；仍无 Render 线程或跨帧队列。

### 4.7 第七增量：目标配置、viewport 与 clear

- Framebuffer::RecordTarget 捕获附件持有、层、尺寸及读写选择，GL owner 首次消费时实现独立 FBO。Resize 创建并验证新附件后发布，源 FBO ID 不变；保留 cubemap/depth-stencil 共用关系，旧快照持有旧尺寸，零尺寸拒绝。纹理格式、采样与 border color 保留。
- ABaseRenderer 增加 PassStateCommand / ClearCommand，录制包执行前恢复目标和 viewport。Begin/End、各 pass 初始目标、Blit、反射/阴影/拾取已接入 BindTarget，反射逐面重新捕获；AView 在 resize 后刷新 UI 纹理 ID。
- Debug/Release 各 16 轮销毁/resize 后回放、无 GL 上下文捕获、cubemap 选择与回收、16 组目标图像对拍通过；原帧输入/packet/Mannequin、真实 Editor 三视图与旧 draw-inputs、完整构建和复制通过，详见 MULTITHREADING_TARGET_STATE_VALIDATION.md。
- 配置持有不冻结像素内容。生产仍同次 draw 消费；下一步将动态上传与 pass 写入/读取汇入完整命令序列、明确完成边界并消除执行端借用，再迁移 UI/debug/picking。阶段 2 和阶段 3 尚未全部完成。

### 4.8 第八增量：拥有资源的帧命令序列

- RenderCommandList 支持按序录制、封口、转移、单次消费、失败/取消终态和前缀计数。ABaseRenderer 收集目标/viewport/clear，CompositeRenderer 将 owning draw 包加入序列；启用的 feature/pass 必须显式 opt-in，录制失败不能封口。
- Engine/Skinning/Reflection 录制不再捕获 feature this；CPU 值与 GPU buffer 由操作持有。帧上传/灯光覆盖与恢复、palette、反射和自动曝光 mipmap 按生产顺序提交。
- 每配置 8 组销毁所有源对象后的回放、GPU 副作用拦截、取消/异常/分配失败、普通 24 帧 Mannequin 及真实阴影/六面反射/曝光/Bloom/Tonemapping/FXAA 的 24 帧即时/packet/延迟对拍通过。动态测试发现并修复 SetAutoExposureSettings 写错 Effect 键，保留严格逐字节对拍。详见 MULTITHREADING_COMMAND_LIST_VALIDATION.md。
- 当前完成状态仅表示 CPU 提交结束。生产资源创建与部分元数据/埋点仍依赖 GL owner；本增量当时尚未闭合动态历史恢复，后续由 4.9 的失效重建协议处理。下一步闭合 Editor debug/picking/readback、ImGui 和帧完成，再进入固定 Render 线程与有界队列。旧 Editor 仅同次 packet 回归通过。

### 4.9 第九增量：动态历史失效与重建

- RenderCommandList::History 记录历史代次和前序完成标记；取消、录制/执行失败、未消费析构会失效相关历史。已准备后继在任何 GPU 操作前拒绝；下一次生产要求重建，同列表重复使用不重复登记，移动不误取消。
- ReflectionRenderPass 在推进捕获面前登记；失败后重置双缓冲和有效标记，并强制完整六面重建，包含 ON_DEMAND 请求。AutoExposureEffect 在选择历史缓冲前登记；失败后重置 ping-pong、计时和首帧标记。
- Debug/Release 命令状态测试、24 帧渐变曝光/按需逐面反射的取消、部分执行失败、析构放弃、后继失效恢复，均与即时基准精确对拍通过。完整构建、旧路径与编辑器回归结果见 MULTITHREADING_TEMPORAL_HISTORY_VALIDATION.md。
- 这不是 Material singleUse 或场景更新的通用回滚，也不是 GPU fence/跨线程提交队列。下一步继续 Editor debug/picking/readback、ImGui 稳定数据和帧完成，再实现阶段 3 固定 Render owner、有界队列与真实交叠。

### 4.10 第十增量：编辑器调试与拾取命令

- Grid、调试相机/反射/灯光/Actor、描边/Gizmo 与 Picking 已接入整帧命令。调试反射拥有常量 buffer 和 cubemap，调试假灯光 fallback 也纳入提交顺序。
- Picking 录制独立 RGBA 图像、Actor ID/GUID、场景运行身份与 Gizmo 上下文，消费只持有目标与结果存储；列表成功完成后主线程才解析。取消/失败/源对象销毁、切场景、删除 Actor、越界、尺寸和选择变化均有拒绝边界。
- AView::RecordRenderCommands 返回拥有资源的视图命令，SceneView::CompleteRender 在成功消费后执行主线程交互；默认产品仍即时运行。Framebuffer 快照读回隔离并恢复 PBO、pack 布局和读目标。
- 使用 --editor --command-list 验收真实调试/picking 源对象销毁回放，以及 Scene/Game/Asset 即时与延迟视图对拍。完整结果和成本边界见 MULTITHREADING_EDITOR_COMMAND_VALIDATION.md。
- 拾取当前下载完整图像且同步读回，尚未使用 PBO；没有独立 Render 线程。下一步闭合 ImGui 稳定 draw data/纹理与正式帧完成，再完成阶段 3 资源归属、有界队列、真实交叠和交互/窗口/退出/性能验收。

### 4.11 第十一增量：ImGui 拥有型帧快照基础设施

- DrawDataRecorder 复制顶点/索引/命令/视口参数，绑定 owning 纹理；执行不访问源 ImGui context/draw data/widget。后端 GPU 设备代际独立持有，字体像素按代际复制并在消费端上传；未知回调和无所有者纹理拒绝。
- UIManager 增加命令录制入口，Canvas 分离 CPU 构建与 GL 初始化；Image/ButtonImage 可携带纹理所有者，模式切回即时前清理字体逻辑 ID。原产品默认即时路径保持。
- Debug/Release 各 8 组大网格/裁剪/高 DPI/销毁后精确对拍、8 帧字体更新与重建的逆序回放、unpack 状态恢复、失败/取消/回收及真实 UIManager 控件通过。完整构建、10 项复制与两配置实际 Editor 回归通过，详见 MULTITHREADING_IMGUI_SNAPSHOT_VALIDATION.md。
- **整个编辑器 UI 尚未完成迁移。** 下一步逐个迁移 AView、Toolbar、AssetBrowser、搜索/对象字段、树节点、HeaderAction 和 TextureDebugger 的裸 ID，验证全 UI 命令模式；再闭合正式 frame completion 和固定 Render owner/有界队列。资源创建/resize/最终释放仍有 GL owner 约束，无阶段 3 并行或性能结论。

### 4.12 第十二增量：编辑器 UI 纹理入口迁移

- 所有已定位的编辑器 Image/ButtonImage、搜索图标、对象字段/纹理预览、TreeNode、HeaderAction 和 ItemPicker 接入 owning TextureID；AView 更新时保留当前附件。TextureDebugger 通过 baregl::Texture::Snapshot 保留独立 GL 存储，源包装对象删除后仍可回放。
- 全局 Actor 图标在 Editor 析构时释放，Console 取消静态 Logger 订阅，修复延迟 GPU 资源释放日志写入已销毁面板的问题。裸 ID 的旧默认选择改用实际纹理键。
- 新增 --editor --command-list --editor-ui，按实际 Main 启动资源跟踪；完整 Canvas、15 个实际面板、Picker 和 TextureDebugger 销毁后回放共 18 组精确图像对拍。报告见 MULTITHREADING_EDITOR_UI_VALIDATION.md。
- UI 所有权入口已迁移，仍需闭合正式 frame completion 与统一交接。ResourceTracking/Logger/资源创建/resize/最后释放等仍有主线程与 GL owner 约束；阶段 3 固定 Render owner、有界队列、真实交叠及性能/输入延迟验收尚未完成。

### 4.13 第十三增量：统一帧提交与 GPU 回收

- RenderCommandList 合并 sealed 子列表并延后发布各子 CPU 结果，嵌套/父子历史依赖保持顺序。RenderFrame 拥有全帧序列，Submit 包含可选呈现和 fence；Poll 非阻塞，Wait/析构用于排空。成功及失败前缀捕获均保留到 GPU 回收，错误使历史失效。
- Editor 增加 Asset/Game/Scene+UI 聚合入口和成功最新帧的拾取完成入口；Game 增加场景+持有目标的最终 blit+调试 UI 入口。默认产品循环保留原路径。
- Debug/Release 各三组实际 Editor 三视图+UI、六组实际 Game 有/无 UI 图像精确一致；帧失败/取消/轮询/回收/历史、源场景/目标/UI 销毁、原 Editor/动态命令/UI 回归通过。完整协议及证据见 MULTITHREADING_RENDER_FRAME_VALIDATION.md。
- 下一增量接入可切换的完整产品循环并明确逐帧输入/拾取，再落实固定 Render owner、资源操作与通知归属、有界队列和真实交叠。当前无独立 Render 线程，也无新增性能收益结论；阶段 2、3 尚未全部完成。

## 5. 阶段 3：Game/Render 双线程（A 形态）

**目标：让固定 Render 线程消费稳定帧数据，Game/Main 线程负责应用更新与必要的平台/UI 事件。**

- 在阶段 2 所有权明确之后，引入有界帧队列和帧完成通知。首版建议限制在途帧数，不追求无限排队；具体缓冲数、背压及输入延迟策略需在实施时量化。
- 固定 Render 线程拥有 GL 上下文，统一 GPU 创建、上传、绘制、读回和销毁路径。核查 Sources/OvWindowing/src/OvWindowing/Window.cpp 与 Device/Driver 初始化及退出。
- 建立 GPU 资源创建/更新/延迟销毁队列；CPU 帧消费完不自动等于 GPU 已完成使用，涉及 GPU 资源回收时需对应完成保证。
- 梳理 ImGui/UI、编辑器多视图、窗口 resize/最小化、反射/picking/readback 的跨线程流程。当前全局绘制状态与 renderer 内部可变数据不能默认并发安全。
- 明确 Render 线程与 JobSystem 的关系，避免主线程等 Render、Render 等 MainThread 任务形成闭环。
- 退出顺序：停止帧提交 → 处理/取消在途工作 → Render 收尾与资源销毁 → 结束相关线程 → 销毁窗口/上下文和依赖资源。需要失败/取消路径。

**验收：** GL 线程归属检查、多帧稳定性、反复切换场景/视图/窗口尺寸、退出压力测试、图像及输入行为对照；Tracy 显示 Game/Render 交叠，统计整帧 CPU/GPU 耗时、队列长度及延迟。

## 6. 阶段 4：多 Pass/View 并行命令录制

- 使用阶段 0 的任务依赖描述 view/pass 之间的真实数据依赖；各 job 使用私有命令缓冲或独占 arena。
- 资源读取稳定，录制端不调用 GL；按 pass/view 既定顺序合并。透明及等值 key 顺序继续保持确定性。
- 共享 renderer 缓存、临时缓冲、蒙皮上传缓存必须拆分或明确定义同步，不能只在最外层并行调用旧函数。
- 优先避免重复 Parse/姿态评估，按实测考虑帧级复用；不要在资源可变时盲目共享缓存。

**验收：** 多视图/多 pass 对拍、依赖正确性、共享资源压力测试；Tracy 确认录制收益大于调度/合并开销。

## 7. 阶段 5：RHI 与 Vulkan 后端

- 基于已经稳定的命令与资源所有权边界定义 RHI，保留 OpenGL 作为对照路径；不要机械地一对一包装所有 GL 调用。
- 明确资源句柄、pipeline、绑定布局、同步/barrier、命令列表、上传/暂存、交换链与 GPU 完成机制。
- 优先新增 Vulkan 后端；梳理 .ovfx 变体、反射、材质参数和 SPIR-V 生成/缓存的对应关系。
- 先完成最小静态场景，再补蒙皮、阴影/反射、UI、多视图和后处理；不要一次替换全部后端。

**验收：** 后端对照、验证层无错误、资源寿命/同步正确、resize/交换链重建及多帧压力测试，再评估性能。

## 8. 阶段 6：fiber / 更完整 job graph（后期按证据推进）

- 只有当 Tracy 表明阻塞等待、深层嵌套或调度开销成为实际瓶颈时，再升级挂起/恢复机制；不将其作为前几阶段的前置条件。
- 保留现有依赖、异常、取消、线程亲和及停机语义。特别审查 TLS、GL 上下文归属、任务恢复线程和 worker 饥饿。
- 引入 fiber stack 生命周期、栈溢出保护、唤醒竞态与恢复唯一性测试，保持旧调度路径作对照。

## 9. 已知关注项（带到下一阶段，不扩成无关大改）

- **错误可观测性：** 生产 Context 未安装通用错误回调；本次蒙皮独立捕获计算异常、检查实例完成标志，收尾后从主线程重抛。未来其他异步组件仍需明确日志和结果失败策略。不能把 Wait 返回解释成成功。
- **Filter 收尾成本：** Tracy 中最后一个任务完成至 Filter 返回中位数约 0.96 ms；含唤醒、multimap 合并和析构。先细分埋点，再评估节点转移或 vector + stable_sort，保持排序和分配器约束。
- **资源耗尽：** 当前提交失败路径已加强，但调度器会分配内存，不是全局 OOM 可恢复系统。
- **阈值可移植性：** 当前阈值来自本机样本，需要在新负载上复测。
- **构建脚本容错：** 现有 post-build 会尝试复制未构建的 Publish 游戏，并最终 EXIT /B 0；阶段 0 已额外核对 Debug/Release 的必要产物。若后续改发布流程，不应只依赖该退出码。

## 10. 工作区与验证速查

### 工作区

- 原基准为 73ae710a57889bfde94184c3c8f073228e998144；工作分支 main。
- 按用户要求分阶段提交：阶段 0 为 30eb8fe，阶段 1 为 4f8da08，阶段 2 几何首增量为 50194bf，材质第二增量为 8cc2f8c，材质快照第三增量为 b1128a7，几何/模型/palette 第四增量为 abf6b13，同次 draw packet 第五增量为 2648d43，相机/灯光第六增量为 24cabec，目标/viewport/clear 第七增量为 c82d4fa，帧命令序列第八增量为本文件所属的新提交。后续每个独立可验收增量完成检查后单独提交。
- 仅提交多线程实现、测试与验收文档。Editor.cpp 的既有注释修改通过按 hunk 暂存排除；ItemPicker、SkeletalData、OvUI、.zcode 等其他用户改动仍留在工作树。
- 历史验收是在包含这些既有用户改动的工作区中完成，不声称逐个检出了补提交来复现历史构建。Tests/Skinning 的旧源码基准仍固定为原基准，不因补提交而变化。
- 未创建额外任务或推送远程。阶段 2 已建立可选同次 draw packet，继续按独立增量推进。

### 构建（已实际验证的入口）

使用 cmd.exe、login=false；PowerShell 入口在本机不可靠。完整构建通过单次批准的沙箱外 MSBuild 执行；这与操作 VS2022 界面是不同操作。

~~~bat
node Build\JobsValidation\build-solution.mjs Debug verified
node Build\JobsValidation\build-solution.mjs Release verified
~~~

上述本地辅助脚本调用 64 位 MSBuild，参数为 /t:Build /m:2 /nr:false、Configuration、Platform=x64、PreferredToolArchitecture=x64、CL_MPCount=2、VcpkgApplocalDeps=false，保留项目 /WX。最后一项仅关闭本机全局 vcpkg 注入的 DLL 收集步骤，未改工程或全局设置；当前程序的导入 DLL 为系统/MSVC 运行库。

MSBuild 路径：C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/amd64/MSBuild.exe。若换机器按实际安装发现路径。新增 .cpp/.h 后再用 gen_proj.bat vs2022 重新生成项目；本轮项目已包含 JobSystem，所以没有无谓重生成。

### 本地测试与 Tracy

- Build/JobsValidation：现有测试源码、编译工具 tools.mjs、日志、.tracy、CSV、分析脚本；该目录被 Git 忽略，跨机器时不会自动存在。
- scene_validation.cpp：真正的 Parse/Filter 对拍与离屏比较；tools.mjs 的 compileEngine 用于独立引擎链接，不能代替全解决方案构建。
- stress.cpp / regression.cpp 和双 deque 冒烟日志：针对已知线程边界。
- Build/Tools/Tracy-0.11.1/tracy-capture.exe 与 tracy-csvexport.exe 已下载并可用，无需重新下载或使用桌面。
- run-tracy-capture.mjs、export-tracy.mjs、analyze-tracy.mjs：已有自动采集流程；分析脚本依赖本轮固定用例/事件名，新增负载需要相应扩展。Zstd 读取使用 Codex 附带的 Node。
- 新增可跟踪回归入口：`node Tests\Skinning\run-validation.mjs Debug`、`Release`、`Release --asan`；详情见 Tests/Skinning/README.md。数据位于 Build/JobsValidation/Skinning/。Tracy 最终有效采集是 `tracy-skinning-serial-release-final`；早先不带 release-final 的采集误连到同时运行的 Debug 测试，不用于性能结论。
- 批次回归增加 `Debug --parallel`、`Release --parallel`、`Release --parallel --asan`、`Release --parallel --mutex-deque`。并行 Tracy 为 `tracy-skinning-parallel-release-final`，源码路径已核实为 Release-parallel/current；最终构建日志后缀 `skinning-batch-final`，复制结果 `skinning-parallel-build-artifacts.json`。
- Debug/Release 场景证据和新阶段的性能数据要区分是否连接 Tracy、对象构成与 worker 数，不能混用来计算加速比。

- 第三增量最新报告：`MULTITHREADING_SKINNING_STAGE1_COMPLETION.md`；新增 `Debug/Release --editor`。可打开项目：`Build/JobsValidation/Skinning/Release-editor/current/SkinningProject/SkinningProject.ovproject`。测试会重新生成该专用项目。
- 最新正式构建为 `msbuild-debug-skinning-stage1-retry.log`、`msbuild-release-skinning-stage1-final.log`，复制为 `skinning-stage1-build-artifacts.json`；最终独占性能 `skinning-stage1-perf-*`，Tracy 为 `tracy-skinning-stage1-release*`。
- 阶段 2 首增量使用 `Debug/Release --geometry`，输出 `Build/JobsValidation/Skinning/<配置>-geometry/`；详情见 `Tests/RenderingCommands/README.md`。新构建日志后缀为 `geometry-stage2`，编辑器回归日志为 `geometry-editor-{debug,release}.log`，不将并发构建/验收期间的耗时作为性能基线。
- 阶段 2 第二增量使用 `Debug/Release --material`，输出 `<配置>-material/`；最终构建日志后缀为 `material-final`，最终编辑器日志为 `material-editor-{debug,release}-final.log`，复制核对为 `material-final-build-artifacts.json`。微基准见 `material-stage2-perf-payload.json`；首稿 `material-stage2` 构建及性能日志不作为最终产物证据。该增量状态见 MULTITHREADING_MATERIAL_COMMAND_VALIDATION.md。
- 阶段 2 第三增量使用 Debug/Release --snapshots，输出 <配置>-snapshots/；最终构建日志后缀 snapshot-final，快照/编辑器日志为 snapshot-{debug,release}-final.log 和 snapshot-editor-{debug,release}-final.log，复制核对为 snapshot-final-build-artifacts.json。该增量范围见 MULTITHREADING_MATERIAL_SNAPSHOT_VALIDATION.md。
- 阶段 2 第四增量使用 Debug/Release --draw-inputs，输出 <配置>-draw-inputs/；构建日志后缀 draw-inputs-stage2，最终对拍 draw-inputs-{debug,release}-final.log，Editor 日志 draw-inputs-editor-{debug,release}.log，复制 draw-inputs-build-artifacts.json，独占微基准 draw-inputs-palette-perf.json。该增量历史范围见 MULTITHREADING_DRAW_INPUT_VALIDATION.md。

- 阶段 2 第五增量使用 Debug/Release --draw-packets 和 --editor --draw-packets；输出 <配置>-draw-packets/、<配置>-draw-packets-editor/。最终构建后缀 draw-packets-final，验收 packet-{debug,release}.log、packet-editor-{debug,release}.log，复制 draw-packets-build-artifacts.json；第五增量历史协议见 MULTITHREADING_DRAW_PACKET_VALIDATION.md。

- 阶段 2 第六增量使用 Debug/Release --frame-inputs 和 --editor --frame-inputs，输出 <配置>-frame-inputs/、<配置>-frame-inputs-editor/。构建后缀 frame-inputs-final，验证 frame-inputs-{debug,release}.log / frame-inputs-editor-{debug,release}.log，复制 frame-inputs-build-artifacts.json；当前协议见 MULTITHREADING_FRAME_INPUT_VALIDATION.md。

- 阶段 2 第七增量使用 Debug/Release --target-state 及 --editor --target-state，输出 <配置>-target-state/ 与 <配置>-target-state-editor/。构建为 msbuild-debug-target-state-final.log、msbuild-release-target-state.log，专项/编辑器/旧回归日志为 target-state-*.log，复制 target-state-build-artifacts.json。

- 阶段 2 第八增量使用 Debug/Release --command-list，输出 <配置>-command-list/。最终构建日志 command-list-final，专项、旧回归、Editor 原 packet 日志分别为 command-list-{debug,release}.log、command-list-regression-*.log、command-list-editor-*.log；复制 command-list-build-artifacts.json。

### 只在需要时阅读的证据

- MULTITHREADING_PLAN.md：阶段 0 协议与完整 checklist。
- MULTITHREADING_VALIDATION.md：已修复的问题、测试范围与性能基线。
- MULTITHREADING_TRACY_VALIDATION.md：本次时序数据与分析限制。
- Build/JobsValidation/msbuild-{debug,release}-verified.log：最终 0 警告/0 错误构建。
- Build/JobsValidation/stage0-build-artifacts.json：产物/资源复制核对。

### 用户操作偏好

后台命令行、隐藏测试窗口优先；操作 VS2022 界面之前必须询问用户。不要为了验收占用桌面鼠标键盘。无需重读旧对话中的浏览器/联网诊断史；需要联网时使用正常工具及必要的单次审批。

## 11. 新对话建议起始消息

> 阅读 MULTITHREADING_NEXT_STEPS.md 和 MULTITHREADING_RENDER_FRAME_VALIDATION.md，继续完成阶段 2、3：接入完整产品循环与逐帧输入/拾取，收尾录制期间的 GL 操作归属；再实现固定 Render owner、有界队列、资源归属和真实 Game/Render 交叠。保留源对象销毁后回放、即时/packet/延迟动态图像严格对拍，按可验收增量提交 Git，不包含用户已有修改，后台验证。
