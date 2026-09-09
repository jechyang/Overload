# 渲染架构 A1：主相机静态不透明切片

日期：2026-09-10。A1 已完成验收：静态拥有型命令切片、独立冷启动对照及全部活跃 Uniform Block 的无反射绑定契约均已闭合。首帧回读干扰仍使用独立暂存方案；本次 Debug/Release 全量构建与 40 个冷启动进程、320 帧严格对照通过。A2 未开始。

## 数据与线程归属

- RenderFrontend 是独立的薄入口，通过 Extract、Prepare、Record 连接模块；不继承旧 Renderer，不提供 Descriptor/Feature 查询，也不持有 Driver。
- SceneRenderExtractor 在主线程场景稳定期复制相机参数、对象身份、矩阵、可见性与世界包围球，并持有 Mesh 几何和 Material::PreparedSnapshot 的资源版本。ECS、源相机、Material、模型借用均不越过 Extract。
- RenderSceneSnapshot 和 PreparedFrameData 的 builder 为函数局部独占对象；只在成功时转移为 shared_ptr<const>，不公开可写别名。ViewPreparer 根据快照计算矩阵、裁剪、排序、对象常量及管线状态；纯数据实现不创建 GPU 资源、不调用旧 Renderer。
- 按旧源材质地址排序的相对组号只在 Extract 产生；后续不保留可解引用材质地址。等 key 保留原收集顺序，裁剪保留旧 max(scale, 0) 和旋转球心规则。
- StaticOpaqueCommandRecorder 只消费发布结果及显式输出目标，返回现有 RenderCommandList；RenderFrame 仍负责提交、GPU fence 和退役。缓冲区、fallback 纹理由既有 baregl/ResourceExecutor 创建，录制期间无 GL，常量只在 GL owner 执行时上传。

## 实际任务依赖与失败

Extract（Main）→ ViewPreparer（Main 或直接提交的一个 Any Worker）→ 成功检查/发布（Main）→ Record（Main）→ 原提交后端。

串行和 Worker 调用同一个 Prepare 函数。Worker 不查询服务、不嵌套提交、不等待主线程或 GL owner。主线程使用 WaitAllPassive；Result 显式保存值/异常，不能从 JobHandle 的完成状态推断成功。失败和取消抛出前先汇合，未发布部分准备输出；没有准备结果时不能调用 Record。A1 只有一个准备任务，不宣称多个准备任务重叠，也不宣称新 Record 已在 Worker 执行。

## 职责迁移与范围

本次接通主相机静态 Opaque 的独立纵向切片，旧 SceneRenderer 的 BeginFrame 不再是该请求提取/准备的必经入口。新入口支持标准静态 Mesh、稳定材质属性、默认 technique、实例数、透视/正交、相机裁剪及 frustum override；保留 Opaque 原有无条件 stencil 设置。

调用者明确选择整个 StaticOpaqueRequest，不能同一视图再调用旧 SceneRenderer 绘制这些对象。首片拒绝可见 Transparent/SceneUI、活动蒙皮、灯光、探针、后处理以及 shadow/reflection receiver 和 singleUse 属性；不消费失败输入后自动回退。缺材质/无效材质、非活动或不满足 GEOMETRY 可见性的对象沿用跳过行为。材质 override/fallback、其余 Stage 和完整功能绑定属于 A2–A5。

已有 Game、GameView、SceneView、AssetView 仍使用完整旧路径，本次没有自动切换产品默认配置。它们不是通过新前端内部的旧模式分支运行。新入口首片的直接调用及已有后端集成由专项测试验证，完整产品迁移尚未完成。

## 验证

已完成：

| 检查 | Debug | Release |
| --- | --- | --- |
| 完整解决方案构建（含 Game、Editor） | 通过，exit 0 | 通过，exit 0 |
| --frontend 三路严格对照 | 通过 | 通过 |
| Worker/无 GL/失败/取消/恢复 | 通过 | 通过 |
| 源及目标生命周期、空视图、拒绝不支持输入 | 通过 | 通过 |

每种配置、每模式 8 帧，共 196608 字节 RGBA 和 54208 字节绘制 trace；旧拥有型命令、新串行、新 Worker 逐字节一致，不使用容差。专项使用 /W3 /WX；MSVC 14.42.34433、Windows SDK 10.0.22621.0，实机 NVIDIA RTX 3070 Ti / OpenGL 4.5。

构建先执行 Dependencies/premake5/bin/premake5.exe vs2022，再通过 MSBuild/Current/Bin/amd64/MSBuild.exe 构建 Overload.sln，两个配置均使用 /t:Build /p:Platform=x64 /p:ForceRebuild=true /p:PreferredToolArchitecture=x64 /p:CLToolPath=<VS>/VC/Tools/MSVC/14.42.34433/bin/Hostx64/x64/ /p:TrackFileAccess=false /p:VcpkgApplocalDeps=false /m:2 /nr:false /p:CL_MPCount=2。沙箱账号缺 Known Folder，关闭 FileTracker 并强制重新编译；显式 Hostx64 避开旧 HostX86 编译器内存不足。早期失败尝试不作为验收证据，最终两次全量构建与专项顺序执行，没有复用旧生产对象冒充重编译。

本地证据：Build/a1-debug-final.log、Build/a1-release-final.log；Build/JobsValidation/Skinning/{Debug,Release}-frontend-command-list/result.json 及 current/run.log、compile-args.json、frontend-{0,1,2}.{rgba,draws}。构建后的 Game/Editor 未做新前端产品入口切换，不据此宣称三视口运行迁移完成。

新增入口：node Tests/Skinning/run-validation.mjs Debug --frontend（Release 同理，可追加 --vs）。输出独立放在 Build/JobsValidation/Skinning/<配置>-frontend-command-list/current/，不改任何历史 baseline。

最初的专项在同一进程中依次重建三种模式，留下了首次即时帧 98 像素差异。下面的独立冷启动验证替代该测量方法；历史失败不改写为通过。

### 首帧差异排查（2026-09-09）

差异是中央三角形的 98 个像素保持背景色，并非颜色舍入。第 4 次 Draw 没有写入该区域的颜色、深度和模板。临时关闭深度/模板测试、重新绑定引擎 UBO、补充中性反射 UBO 均未消除它。

逐项移除观测操作得到以下结果（Debug 本机诊断）：

| 观测方式 | 旧即时与新路径第 0 帧不同像素 |
| --- | ---: |
| 原 trace：draw 前同步读取活引擎 UBO | 98 |
| 只移除纹理回读 | 98 |
| 只移除活 UBO 回读 | 0 |
| 完全不安装 draw hook | 0 |
| 将完整 trace 挪到 draw 后，仍读取活 UBO | 98 |
| draw 边界复制 UBO 到独立暂存区，全部 draw 后读取暂存区 | 0 |

确认的触发条件是 glGetNamedBufferSubData 同步读取随后仍被逐对象更新的活 UBO。日志伴随 NVIDIA 的 VIDEO → DMA CACHED 缓冲区迁移提示，但本次没有证明驱动内部原因，也不将结论推广到其他 GPU/驱动。不能用丢弃首帧、预热、放宽容差或修改旧渲染实现解决这个观测问题。

修正只涉及测试：每次 Draw 前 glCopyNamedBufferSubData 把实际 GPU 常量复制到互不覆盖的暂存槽，整轮结束才同步读取并填回原 trace 位置。所有 68 个 float 仍来自真实 GPU 数据，继续逐 Draw 检查新路径显式时间；只有旧 Feature 的墙钟字段按原规则归零。draw 参数、矩阵、uniform/sampler 和管线 trace 的比较范围没有缩小。

测试可执行文件保留 live 诊断策略，以复现原同步回读方式；正式 runner 使用 none/staged。每个模式分别启动两个独立进程，各自创建窗口、GL context、Driver、shader、场景与资源，none 完全不安装 hook、不分配 trace GPU 缓冲区。三种模式 × 两种观测共 6 个进程，每进程 8 帧，精确比较全部 RGBA；staged 三份非空 trace 另行精确比较，因此不会让某一路先运行来预热另一条路径，也不会用空 trace 冒充验证。

最终正式验证（本次重新运行）：

| 检查 | Debug | Release |
| --- | --- | --- |
| --frontend：旧拥有型命令 / 新串行 / 新 Worker，none/staged 独立冷启动 | 通过 | 通过 |
| --frontend-immediate：旧即时 / 新串行 / 新 Worker，none/staged 独立冷启动 | 通过 | 通过 |
| staged 三路 GPU 常量/绘制 trace 非空且逐字节相等 | 通过 | 通过 |
| 有/无 trace 的完整 8 帧 RGBA 逐字节相等 | 通过 | 通过 |
| Worker、取消/失败汇合、源/目标生命周期与拒绝输入 | 通过 | 通过 |

共 24 个正式冷启动进程、192 个场景帧（包含所有首帧，无预热）。每进程 RGBA 196608 字节；每份 staged trace 54208 字节。证据位于 Build/JobsValidation/Skinning/{Debug,Release}-{frontend,frontend-immediate}-command-list/result.json 及 current/cold-<模式>-<none|staged>/ 下的原始输出。生产代码/头文件/构建配置未改动；专项按 /W3 /WX 重新编译，复用上一提交已全量构建的生产库，符合计划“无布局变化时只跑受影响回归”的要求。

原故障复现也已完成：node Tests/RenderingCommands/reproduce-frontend-readback.mjs 在 Debug/Release 各自冷启动三模式的 live 探针。两配置旧即时均得到逐帧差异 [98,0,0,0,0,0,0,0]，新串行/Worker 均为 0；六份 live trace 与对应 staged trace 完全一致，none/staged 像素也始终一致。原差异仍能被精确触发，并非测试把它忽略。原始采集与诊断 JSON 位于 Build/JobsValidation/A1FirstFrame/，正式通过结果与该机器相关诊断分开记录。

### A1 上次交接边界（历史，现已由下节闭合）

排查还发现 Standard 默认变体含第二个活跃 Uniform Block（反射常量，binding 1），原 trace 仅覆盖 binding 0。无探针且材质禁用接收时，旧 ReflectionRenderFeature 会提前返回，新静态录制桥也没有显式反射 UBO 绑定。中性反射 UBO 试验没有改变上述 98 像素差异，所以这是独立的绑定完整性问题，不能把本次像素对照通过等同于全部 shader 输入已定义。

OpenGL 要求活跃 Uniform Block 绑定足够大的缓冲区；缺失时 shader 结果可能未定义，见 [OpenGL 4.5 Core 规范，Uniform Variables](https://registry.khronos.org/OpenGL/specs/gl/glspec45.core.pdf)。下一项仍在 A1：核对完整活跃块、补齐明确的无反射绑定契约和污染/冷启动验证；保留 A0 旧源码并明确区分有效基线输入与既有未定义绑定。上次交接时该项尚未实施，未宣布 A1 全项验收。此次收尾见下节，仍不进入 A2。

### 活跃 UBO 完整绑定收尾（2026-09-09–10，已通过）

本次仅处理 A1 的绑定契约。新静态桥每 Draw 上传并绑定完整 128 字节 ReflectionUBO：position/boxCenter/boxHalfExtents 为零、rotation 为单位矩阵、brightness=0、boxProjection/local 为 32 位零，尾部填充也初始化。EngineUBO 继续提供完整 272 字节。资源由既有 ResourceExecutor 创建，常量上传只发生在 GL owner 执行阶段；命令按 shared_ptr 持有反射 buffer，覆盖前端销毁后的回放与 GPU 退役。

Extract 在发布快照前核对所选实际 program 的全部活跃 Uniform Block，每请求按 program 去重，通过 ResourceExecutor 查询 GL 元数据。只接受 A1 已提供的 EngineUBO/binding 0 和 ReflectionUBO/binding 1，检查最小缓冲区尺寸、成员名/类型/偏移、数组/矩阵步长和主序；未知块或不兼容布局明确拒绝，不继承外部 UBO。Prepare/Record 不增加 GL 查询，也没有进入 A2 的 View/Stage 工作。

对照输入现在显式分为有效和无效两类：旧 SceneRenderer/ABaseRenderer/CompositeRenderer、材质、Engine/Reflection Feature、Standard 及反射 shader 与 A0 固定提交保持一致；测试在旧路径绘制前外部绑定 128 字节中性反射数据。这是对 oracle 的有效输入约束，不修改旧实现。旧无绑定和 124 字节范围仅作为负例，不能用其渲染输出定义兼容性。旧 CReflectionProbe 的 124 字节载荷/分配仍是未迁移探针路径的既有问题，本项没有修改或宣称验证有探针功能，后续 A3/A4 必须处理其 std140 完整尺寸。

测试扩展：逐 Draw 枚举所有活跃块，验证实际绑定 buffer、offset/range 和存储足够大；按真实绑定起点复制全部 400 字节 GPU 常量到独立暂存槽，绘制结束后回读。新增反射 128 字节全部与中性值核对（包括 padding），原引擎 68 个 float、时间断言和其他 draw/uniform/sampler/state trace 保持。每进程输出 uniform-blocks.txt，保存驱动返回的成员布局。

每配置、每 oracle 共 10 个独立冷启动进程、80 帧：clean 下旧/新串行/新 Worker × none/staged，加新串行/Worker × none/staged 的 poison。新路径提交前轮换空绑定、16 字节范围、完整脏 buffer；staged 在相邻 draw 间再次污染，必须由下一条命令恢复。所有模式的完整 RGBA、五份非空 staged trace 逐字节一致才通过，不预热、不丢弃首帧、不放宽容差。

负例包括缺失 UBO、124 字节范围、16 字节范围，以及经真实 shader/Extract 的未知块、错误 binding、超大块、错误类型、偏移和 row-major；同时核对 Standard 默认 pass 的全部非 SKINNING 变体。保留 Worker/无 GL/失败取消汇合、源/目标/前端销毁及空视图回归。

最终验证结果：

| 检查 | Debug | Release |
| --- | --- | --- |
| 全量解决方案（含 Game/Editor） | 通过，exit 0 | 通过，exit 0 |
| --frontend：有效旧命令输入 / 新串行 / Worker，clean 与 poison | 10 进程、80 帧通过 | 10 进程、80 帧通过 |
| --frontend-immediate：有效旧即时输入 / 新串行 / Worker，clean 与 poison | 10 进程、80 帧通过 | 10 进程、80 帧通过 |
| 全部活跃 UBO、实际范围及中性数据/填充，16 个静态 Standard 变体 | 通过 | 通过 |
| 无绑定/124 字节/短范围及 6 类 shader 契约负例 | 全部拒绝 | 全部拒绝 |
| Worker、无 GL 准备/录制、失败/取消/恢复、源和资源生命周期 | 通过 | 通过 |

共 40 个正式冷启动进程、320 个场景帧，包括全部首帧；每进程 RGBA 196608 字节，每份 staged trace 59904 字节（32 Draw 的全部活跃 UBO 及原 trace 项）。每组 10 份 RGBA 与 5 份非空 staged trace 精确一致，有/无 trace 和污染前后都不改变像素。未重跑或改写上一轮的 live 首帧诊断。

构建证据：Build/a1-ubo-debug-build.log、Build/a1-ubo-release-build.log。Debug 构建期间补正错误消息保存块名的方式，随后用 ForceRebuild=true、BuildProjectReferences=false 分别重建 OvRendering/OvGame/OvEditor，最终日志为 Build/a1-ubo-debug-final-<模块>.log；再显式提供 SolutionDir 执行资源打包，验证 Build/Debug 的 Game/Editor 与最终 Bin 产物逐字节相等。中间 ForceRebuild=false 的尝试因沙箱 Known Folder/FileTracker 失败，不作为通过证据。所有生产对象与最终头文件匹配；专项使用 /W3 /WX。

运行证据：Build/JobsValidation/Skinning/{Debug,Release}-{frontend,frontend-immediate}-command-list/result.json，以及 current/cold-*/ 下的原始 RGBA、draws、run.log 和 uniform-blocks.txt。汇总及 A0 旧源码 SHA-256 证据为 Build/a1-ubo-evidence.json。实际 GL 返回 EngineUBO=272、ReflectionUBO=128，反射成员 offset 为 0/16/80/96/112/116/120；bool 为 4 字节、矩阵步长为 16，均与新载荷吻合。

至此 A1 全项验收闭合，仅限本文已声明的主相机静态 Opaque 范围。A2–A6 和旧有探针功能仍未迁移；不会因为本项通过而宣称完整产品视口或反射功能已经接入新前端。

测试专用 ViewPreparer 源副本只在入口和首次私有 draw 生成后插入观察/故障钩子；有超时闩锁验证无当前 GL、实际 Worker 身份、失败/取消先退出后释放最后输入。故障发生时已经存在部分准备结果，仍不能发布；随后正常请求必须成功。生产入口不接收任意 Worker 回调。SystemCalls 的测试副本只将日志 AppData 路径指向输出内的 profile，避开测试账号 Known Folder 缺失；不是引擎行为修改。

## 尚留适配器与下一会话

- StaticOpaqueRequest/SceneRenderExtractor 是场景输入适配器，首片外功能继续留在旧产品入口；A2–A5 扩展数据契约并迁移调用者，A6 删除旧继承链及无人使用适配代码。
- StaticOpaqueCommandRecorder 是有限后端桥，复用 RenderCommandList/RenderFrame；A3 将其最小绑定/录制职责并入通用 DrawPacketBuilder/CommandRecorder，不建立第二套提交后端。
- A1 已完成；本会话在验证与提交后结束，A2 未开始。后续工作仍按 A0 冻结契约和总计划推进，不能把此项中性绑定当作有探针反射功能迁移。
