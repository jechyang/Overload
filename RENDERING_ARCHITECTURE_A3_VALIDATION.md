# 渲染架构 A3：显式功能绑定与并行命令块

日期：2026-09-10。状态：A3 已完成实现与验收；Debug/Release 完整构建、新前端精确对照及既有辅助路径回归均通过。A4 未开始。

## 数据与线程归属

- SceneRenderRequest 接管主视图网格请求；StaticOpaqueRequest 仅保留 A1 限定输入适配器。MeshInput/PreparedMeshDraw/MeshCommandRecorder 不再以 Static 命名。
- Extract 在主线程稳定期捕获模型、用户矩阵、材质 program/属性版本、有效 poseVersion 与已转置 palette、蒙皮 boundsScale、灯光矩阵/位置/作用范围。动画评估与 pose 发布时序不变；后续阶段没有 Scene/Actor/Component/源 Material 借用。
- FrameBindings 保存时间；ViewBindings 保存相机上传值和筛选后的灯光矩阵；每个绘制项持有已编码的材质快照，源网格输入持有 palette 与反射常量。DrawPacketBuilder 组合 EngineConstants，录制只读取发布数据。
- ShadowReceiverBindings/ReflectionReceiverBindings 接收拥有型纹理及值。材质覆盖是独立 PropertyOverrides，源材质不接受逐 Draw 的阴影/反射 SetProperty。移除覆盖时恢复源属性，不能复用此前的覆盖缓存。
- 材质 singleUse 兼容消费留在 Extract 主线程：先捕获不消费的输入，按统一 Stage 排序与可见性解析源签名缓存，只消费本次成功快照实际上传的属性。连续签名命中沿用原值，切换材质/变体按原规则重新读取；Encode/Record 失败不回滚已消费值。失败不转回旧 Renderer 重画。

## 实际任务依赖

Extract（Main，包含有 singleUse 请求的消费顺序解析）→ BuildView（Main）→ 候选范围 Prepare（串行或 Worker）→ 汇合/全局 Stage 排序（Main）→ 绑定范围 Prepare（串行或 Worker，其中首范围准备灯光/相机绑定）→ 汇合/只读发布（Main）→ 私有命令块 Record（串行或 Worker）→ 按预定槽位汇合/Seal（Main）→ 既有 RenderFrame/GL owner。

RenderTaskCoordinator 统一拥有三批任务的提交与汇合。每批提交前固定输出/handle 存储；全部已提交任务退出后检查错误和成功标记。绑定阶段完成前 PreparedFrameData 的 builder 不对外发布；录制失败不返回部分命令。串行与 Worker 调用同一范围函数，Worker 不嵌套提交或调用并行 Seal，也不调用 ResourceExecutor/GL。

录制器在主线程生成视图控制命令，各范围使用独立 RenderCommandList。块入口恢复视图灯光内容，每 Draw 显式恢复目标、viewport、Engine/Reflection UBO、灯光和所需 palette 绑定及材质/几何状态；GPU 写入只在命令执行发生。没有共享的 CPU 当前材质/相机缓存。协调器不实现 GPU 清除、绑定或绘制逻辑。当前每块恢复灯光、每 Draw 上传完整绑定；本阶段验证正确性与实际并行，没有以专项时长宣称整帧性能收益。

## 职责迁移

EngineBuffer/Lighting/Skinning 的新前端输入由 Extract、DrawPacketBuilder 和 MeshCommandRecorder 接管。源 Material 的绑定和功能覆盖不再依赖 CompositeRenderer::OnBeforeDraw 链。原功能类仍服务尚未迁移的产品/辅助视图，A4–A6 继续退出。

CReflectionProbe 的 UniformData 和 UBO 分配补齐 std140 尾部四字节：128 字节，原有字段偏移不变，新增尾部清零。新接收结构用显式 32 位布尔值并检查大小/偏移。Extract 对 EngineUBO、ReflectionUBO、LightSSBO、SkinningSSBO 的绑定点与布局做 owner 侧验证。

驱动可能列出未被着色阶段使用的 SkinningSSBO；palette 需求依据实际顶点阶段引用及请求 FeatureSet。无蒙皮分支保留 no-op，使用 SKINNING 但缺失有效 palette 时显式绑定 identity。

## 验证入口与覆盖

- A3：node Tests/Skinning/run-validation.mjs Debug --bindings --vs <VS>；Release 同理；追加 --frontend-immediate 使用旧即时路径对照。
- A2：--stages / --stages --frontend-immediate。
- A1：--frontend / --frontend-immediate。
- 既有辅助视图/材质失败语义：--command-list、--parallel-recording。这两种隐藏窗口验证复用前端用例的测试 AppData 隔离，日志写入各自输出目录，避免沙箱账户未注册 Windows Known Folder；生产 SystemCalls 不变。

A3 延续独立冷启动 clean/poison、有/无 staged trace、旧命令或即时/新串行/两 Worker 的精确比较；各组 10 进程、80 帧；每进程 8 帧、96 Draw。staged 记录全部活跃 Engine/Reflection UBO、实际灯光及有效 palette 字节，以及既有材质/管线/绘制项。未被顶点阶段引用的 palette 不作为有效绘制输入采集，同时用活跃 bone attribute 交叉验证该判断；identity 与真实 pose 仍采集完整有效范围，不放宽图像或有效参数容差。

场景覆盖共享材质、三 Stage、静态与蒙皮混合、identity、变化 pose、singleUse、缺失变体、透视/正交、实例数、等 key 和跨范围透明排序，以及无限作用域定向光与被裁掉的远处点光。独立两视图用例修改相机、pose、singleUse 和接收纹理，销毁源输入后逆序录制/回放，检查每视图图像、材质版本和源属性不污染。

绑定/录制各自用有超时的两 Worker 闩锁验证实际重叠、正反完成顺序、私有输出失败、取消、第二次提交失败、全部任务汇合、恢复和单 Worker。成功的强制顺序命令实际回放，与串行图像逐字节比较。录制失败后检查独占已发布输入在汇合后释放。首轮 Debug 即时对照暴露了测试观察时序错误：第二次提交失败后仅有的首任务退出，输入可以合法释放；修正为在首任务仍阻塞时检查存活，仅在确有第二个在途任务时继续检查存活，汇合后仍要求释放。该修正不改生产生命周期或比较容差。探针仅注入测试编译副本，生产接口不接收任意 Worker 回调。

最终验收结果：

| 检查 | Debug | Release |
| --- | --- | --- |
| 全量解决方案，含 Game/Editor | 通过，exit 0 | 通过，exit 0 |
| A3：旧命令/旧即时各对照新串行与两 Worker，clean/poison | 两组通过，20 进程、160 帧 | 两组通过，20 进程、160 帧 |
| 绑定与录制实际重叠、正反完成顺序回放、失败/取消/部分提交、恢复与单 Worker | 通过 | 通过 |
| 两视图共享材质、pose/singleUse、接收纹理、源销毁及逆序回放隔离 | 通过 | 通过 |
| A2 三 Stage 回归 | 两组通过，20 进程、160 帧 | 两组通过，20 进程、160 帧 |
| A1 限定前端及无效绑定负例回归 | 两组通过，20 进程、160 帧 | 两组通过，20 进程、160 帧 |
| 既有 --command-list / --parallel-recording | 两项通过 | 两项通过 |

最终前端矩阵共 120 个独立冷启动进程、960 帧：A3、A2、A1 各 40 进程/320 帧，不计诊断或失败重试。每组 10 份 RGBA 与 5 份非空 staged trace 精确一致，包含所有首帧、不预热、不放宽容差。每份 RGBA 196608 字节；A3 staged trace 204288 字节（96 Draw），A2 为 179712 字节（96 Draw），A1 为 59904 字节（32 Draw）。

既有回归另外验证 24 帧真实 Mannequin、24 组动态阴影/反射/mipmap/曝光/Bloom/Tonemapping/FXAA 图像对照，以及取消、部分执行失败和渐进历史恢复。并行编码用例每配置检查 1024 份源销毁材质输入、8 个 GPU 代际和 12 帧 × 3 模式 × 4 Pass 的 27648 次有序绘制。它们验证共享改动下既有辅助路径的行为；新前端的辅助 View 迁移仍属 A4。

最终两配置完整构建后刷新六份编辑器/Game 导出模板 EXE，逐字节等于对应 Bin 产物；哈希见 Build/a3-product-build-evidence.json。最终汇总见 Build/a3-final-validation-evidence.json；Release A3 采用生命周期观察点修正后的 Build/a3-release-bindings-final.json，Debug 使用重跑后的 Build/a3-debug-suite.json。原失败证据保留为 Build/a3-debug-initial-lifetime-assertion.log。

复现环境：Visual Studio 2022 Community / MSVC 14.42.34433，x64 Debug 与 Release。先运行 Dependencies/premake5/bin/premake5.exe vs2022，再以 Hostx64 编译器运行 Overload.sln Build；最终构建使用 /m:1、/p:CL_MPCount=1、/p:ForceRebuild=true、/p:TrackFileAccess=false、/p:VcpkgApplocalDeps=false。该环境的 x86 编译器及并发完整构建曾触发内存/PDB 失败，最终验收以串行完整构建为准。

本地证据：Build/a3-sequential-build-evidence.json 保存两配置构建参数、时间及退出码；Build/a3-debug-suite.json 和 Build/a3-release-suite.json 保存每组回归参数与日志。各测试完整清单、冷启动捕获和逐进程日志位于 Build/JobsValidation/Skinning/<配置及模式>/result.json 及其 current 目录。Build 为生成目录，不纳入提交。旧 SceneRenderer、ABaseRenderer、CompositeRenderer、EngineBuffer/Reflection/Skinning/Shadow Feature 和 Standard 着色器与 A0 固定提交一致，见 Build/a3-oracle-source-evidence.json；共享 Material::PrepareSnapshot 的可选覆盖/消费参数及 CReflectionProbe 的 128 字节修复属于本阶段明确变更。

## 尚留适配器与 A4 边界

StaticOpaqueRequest/Extract 继续服务 A1 限定入口并在消费前拒绝其不支持的能力，A6 清理。SceneRenderRequest 的主视图仍独占整个网格视图，不与旧路径重复提交。

活动反射探针、后处理、未声明的辅助 Stage 仍明确拒绝；活动阴影生产没有导入资源时拒绝。A3 的导入接收纹理只保证资源持有及绑定隔离，不宣称完成 A4 的生产者、内容有效性和历史校验。Game/GameView/SceneView/AssetView 的完整入口仍由 A4–A5 迁移。A4 接通 ShadowCaster、ReflectionCapture 和显式资源计划。

## 提交收尾

实现与验收已完成，Git 提交尚未生成：当前沙箱无法创建 .git/index.lock；正式 require_escalated 入口因 Windows CET 启动错误退出（并非自动审批拒绝）。Build/a3-commit.mjs 保存本阶段精确文件清单、内容哈希与预期 HEAD 检查，可在具备仓库写权限的终端执行 node Build/a3-commit.mjs。脚本只暂存 A3 文件并创建本地提交，不推送。

补充（2026-09-10）：A3 已提交为 `2678dcf`（feat(rendering): complete A3 explicit bindings and parallel recording）。上述内容保留当时的提交阻断记录；当前会话已在该提交基础上推进 A4。
