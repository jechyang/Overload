# 渲染架构 A2：统一静态网格 View/Stage 与并行准备

日期：2026-09-10。状态：A2 已完成验收；Debug/Release 构建、静态三阶段并行准备对照及 A1 回归均通过。A3 未开始。

## 数据与线程归属

- StaticSceneRequest 在主线程稳定期提供场景、相机、显式 ScenePipeline 配置、材质 override/fallback 与类别筛选。ExtractScene 捕获所选材质的实际 program/属性版本、状态、几何、对象身份和收集序号；不向后续阶段传递 Scene/Actor/Component/Material 借用。
- StageDescription 明确 Opaque、Transparent、SceneUI 的默认 technique、排序和接收能力，并记录 ShadowCaster、ReflectionCapture、Picking 的既有 technique/fallback 映射。后三者尚无静态场景 provider，请求时明确拒绝；未知 Stage、重复提交同 Stage 也拒绝。
- PreparedFrameData 按 ViewID/Stage 保存连续绘制列表。ViewPreparer 计算相机值，MeshFeatureProcessor 用只读输入与私有范围结果进行筛选；全部任务成功后按预定范围合并，对每个完整 Stage 列表全局稳定排序，最后封口为 shared_ptr<const>。
- Opaque 保留 drawOrder → 原源材质相对组号 → 近到远；Transparent/SceneUI 保留 drawOrder → 远到近，等值 key 保留收集顺序。不会按局部透明排序结果直接拼接。

## 实际任务依赖与失败

Extract（Main）→ BuildView（Main）→ 独立候选范围 Prepare（串行或 Any Worker）→ WaitAllPassive/显式结果检查（Main）→ 全局排序/发布（Main）→ PipelineRunner/Record（Main）→ 既有 RenderFrame/GL owner。

RenderTaskCoordinator 统一拥有准备任务提交。范围数取候选数与 max(2, Worker 数) 的较小值；空场景使用一个空范围。一个 Worker 也顺序执行相同范围。每个任务读取同一不可变快照，仅写其独占 Result。提交前为 handles/result 分配固定存储；部分提交失败、范围失败和取消均汇合所有已提交任务，失败不发布。没有 Worker 嵌套提交、被动等待或 GL 服务调用。

串行与并行调用同一 PrepareRange 与 Publish。A2 的 Record 仍在主线程，未宣称绑定/录制并行；A3 接通该部分。

## 职责迁移

- ScenePipeline 提供静态 Opaque → Transparent → SceneUI 的显式固定配置；PipelineRunner 按准备结果的 View/Stage 切片调用统一提交入口，不读取场景 Descriptor 或按旧 Pass RTTI 选择 Feature。
- RenderFrontend 仅转交 Extract/Prepare/Record；提交与汇合已经移出前端，交由独立 RenderTaskCoordinator。
- A1 的 StaticOpaqueCommandRecorder 扩展并更名为 StaticMeshCommandRecorder，复用既有命令/资源后端，逐 Draw 显式恢复 Engine/中性 Reflection 常量。全部三个 Stage 保留旧无条件 stencil 设置。
- override 优先于原材质；仅原材质缺失时用 fallback；非空无效原材质仍跳过。fallback 身份豁免 include 类别筛选，但之后按实际材质分类。禁止未声明 Stage 利用 Shader 默认回退冒充支持。

## 验证

已完成的行为与构建检查：

| 检查 | Debug | Release |
| --- | --- | --- |
| 全量解决方案，含 Game/Editor | 通过；最终四模块强制重建 | 通过，exit 0 |
| A2：有效旧命令 / 新串行 / 两 Worker，clean/poison | 10 进程、80 帧通过 | 10 进程、80 帧通过 |
| A2：有效旧即时 / 新串行 / 两 Worker，clean/poison | 10 进程、80 帧通过 | 10 进程、80 帧通过 |
| 两 Worker 实际重叠、反向完成、失败/取消/部分提交汇合与恢复、单 Worker | 通过 | 通过 |
| 旧筛选器逐列表对照、Stage/排序/材质选择/配置拒绝 | 通过 | 通过 |
| A1 --frontend 与 --frontend-immediate 回归 | 两组通过，共 20 进程、160 帧 | 两组通过，共 20 进程、160 帧 |

最终验收矩阵包含 80 个独立冷启动进程、640 帧：A2 40 进程/320 帧，A1 回归 40 进程/320 帧；包含全部首帧，不预热、不放宽容差。每份 RGBA 为 196608 字节；A2 每份 staged trace 为 179712 字节（96 Draw），A1 为 59904 字节（32 Draw）。每组全部 10 份图像与 5 份非空 staged trace 必须精确一致，污染与有/无观测也不能改变输出。每 Draw 检查全部活跃 EngineUBO 272 字节、ReflectionUBO 128 字节及原有绘制 trace 项。

旧渲染器仍从测试外部导入有效中性反射 UBO，A0 固定生产源码未改动。A2 新增检查覆盖三个 Stage；缺失 UBO、124 字节范围、错误 shader 布局等独立负例由本次重跑的 A1 专项负责，A2 manifest 不重复声称运行这些负例。首轮 Debug A2 两组行为已通过，随后仅修正此项覆盖元数据并重新采集；最终矩阵采用最后结果，原始图像/trace 比较和断言未放宽。

A2 场景用同一 Actor 的三个静态网格参与 Opaque/Transparent/SceneUI；跨候选范围放置等 key 与透明对象。源材质存于连续数组，固定冷启动各进程中的原地址相对顺序，避免把分配器差异当成排序语义。每帧切换透视/正交、实例数与 drawOrder，并在第 6 帧使用不存在的 FeatureSet 验证默认变体回退。三个主 Stage 显式选择默认 technique；缺失命名 technique 通过同一 Shader 入口核对既有回退，未实现的 Stage 仍拒绝。

构建先执行 premake5 vs2022。MSVC 14.42.34433、SDK 10.0.22621.0；MSBuild 使用 amd64、ForceRebuild=true、Hostx64 编译器、TrackFileAccess=false、VcpkgApplocalDeps=false、/m:2、CL_MPCount=2。Debug 首次全量构建后收敛了调度入口/头文件，再以 BuildProjectReferences=false 分别强制重建 OvRendering/OvCore/OvGame/OvEditor；Release 全量构建采用最终源码。专项均为 /W3 /WX。最后刷新 Debug 编辑器的双配置 Game 导出模板，六份打包 EXE 与对应最终 Bin 产物逐字节相等。

构建证据：Build/a2-debug-build.log、Build/a2-debug-final-<模块>.log、Build/a2-release-build.log、Build/a2-debug-final-package.log；哈希汇总为 Build/a2-product-build-evidence.json。源码 oracle 核对见 Build/a2-oracle-source-evidence.json。

运行证据：Build/JobsValidation/Skinning/{Debug,Release}-{stages-frontend,stages-frontend-immediate,frontend,frontend-immediate}-command-list/result.json，以及各自 current/cold-*/ 的 run.log、RGBA 和 draws。构建/专项汇总见 Build/a2-suite-evidence.json。

入口：node Tests/Skinning/run-validation.mjs Debug --stages --vs <VS>；Release 同理。追加 --frontend-immediate 使用未改动的旧即时路径作为 oracle。--frontend 与 --frontend-immediate 继续运行 A1 回归。

专项包括真实 GL 冷启动三路图像/全部活跃 UBO/绘制 trace、混合 Stage 同对象多网格、两 Worker 闩锁与反向完成、部分输出失败/第二次提交失败/取消/恢复、单 Worker、旧 FilterDrawables 对照、override/fallback/无效材质/类别豁免、流水线子集与顺序、未支持 Stage 拒绝。测试仅在编译副本插入探针，不在生产接口接收任意 Worker 回调。

## 尚留适配器与后续边界

StaticOpaqueRequest/Extract 继续作为 A1 限定输入适配器；它与 ExtractScene 共用提取实现及同一个协调器、准备、录制后端，没有新前端内部的旧 Renderer 模式。A6 移除不用的适配入口。

Game、GameView、SceneView、AssetView 尚需 A3–A5 的灯光、蒙皮、辅助视图、编辑器与后处理，因此仍使用既有完整路径。新请求独占整个静态视图，调用者不得为同一视图再提交旧场景绘制。内置阴影/反射绑定、singleUse、活动蒙皮、灯光/探针/后处理仍在明确拒绝的未迁移范围；SceneUI 此处指静态网格 Stage，不包括最终 ImGui。

A3 下一步：显式功能绑定、蒙皮/灯光、singleUse 兼容与并行绑定/命令块；旧探针 124 字节 std140 问题仍须 A3/A4 处理。本项不修改或宣称完成有探针路径、完整产品入口迁移及 A4 资源计划校验。
