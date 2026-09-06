# 阶段 1 收尾验收：Debug 热区与真实编辑器项目

日期：2026-09-06。基于已有未提交的阶段 0 和阶段 1 两个增量；保留其他用户改动。本轮解决 Debug 蒙皮批次回退，增加按 Update 的批次统计，并按用户授权创建专用编辑器验收项目。渲染仍在主线程，本轮没有引入跨帧借用或 Render 线程。

## 生产改动与默认策略

- SampleKeys 接收只读 span，动画 tracks 遍历也使用 span。MSVC 14.42 的 xmemory 中，调试迭代器的 _Adopt_locked / _Orphan_me_locked_v3 均通过 _LOCK_DEBUG 注册/移除；频繁创建共享 vector 的迭代器会争用这把锁。只改视图，不改查找算法、关键帧顺序、采样时间或循环端点规则，也不全局关闭迭代器检查。
- 采样轨道直接生成 Translation(position) * ToMatrix4(Normalize(rotation)) * Scaling(scale)，与 FTransform::GenerateMatricesLocal 的表达式和运算顺序相同。无需构造带 unordered_map 通知器的 FTransform、计算随后丢弃的 world 分解。这同时减少 Debug 分配/调试容器开销和 Release 冗余工作。没有修改通用 FTransform 或手动姿态编辑行为。
- **Debug 和 Release 保留批次默认启用**，继续使用至少 2 worker、8 个实际评估实例、512 节点及至多 2×worker+1 块的原门槛。小批次继续串行；没有根据微基准调整 Parse/Filter 阈值。
- Scene::Update 的 pool 路径可传入 SkinningUpdateStats 指针；不传时不收集统计。主线程重置并更新计数，不增加全局原子变量或 worker 回调。指标覆盖连续的已审查候选批次，包含候选、实际评估、节点、并行实例、并行批次和已提交任务数。任意脚本/边界组件内的同步姿态操作不计入；异常时数据仅为部分进度，不能当作成功发布证明。
- 借用输入稳定期、有序发布、被动等待、回调边界和异常处理沿用第二增量，见 MULTITHREADING_SKINNING_PARALLEL_VALIDATION.md。

## Debug 原因对照

同机、4 worker、真实 Mannequin（59 节点、45 骨骼），三个实验顺序独占运行，Tracy 未连接。每组沿用预热 16 次、串并交替采样 64 次的中位数。下表为 256 实例 Scene::Update：

| 实验 | 串行 μs | 批次 μs |
|---|---:|---:|
| 本轮修改前 | 35982.8 | 54747.7 |
| 仅采样/轨道 span | 34123.5 | 27497.0 |
| 再使用直接 TRS | 16460.6 | 5851.2 |

隔离 span 修改即消除了变慢方向，直接 TRS 又显著降低计算和构造开销。这里没有通过采样 profiler 把每一微秒归因到某个锁；锁机制来自本机 STL 源码，消融实验支持优化方向。原始日志为 Build/JobsValidation/skinning-debug-{before,spans,trs}.log，对应原生产源码备份 skinning-before-hotpath.cpp。不能用上述数据推算整个编辑器帧率。

## 新建项目与真实编辑器覆盖

入口：Tests/Skinning/run-validation.mjs Debug/Release --editor。可打开的项目：Build/JobsValidation/Skinning/Release-editor/current/SkinningProject/SkinningProject.ovproject；Debug 目录也生成同样项目。三个场景均由生产 Scene 序列化并经 SceneManager 从磁盘加载，64 个不同播放时间的 Mannequin 排列为 8×8 网格，引用引擎模型和材质。

实际 Context 使用默认 11 worker。每场景 96 次、1/60 秒 Update，Debug/Release 的 project-batches.csv 完全一致：

| 场景 | 候选实例累计 | 批次内实际评估 | 并行实例累计 | 并行批次数 | worker 任务 |
|---|---:|---:|---:|---:|---:|
| crowd | 6144 | 6144 | 6144 | 96 | 2112 |
| fragmented | 4608 | 4608 | 0 | 0 | 0 |
| rate_limited（15 Hz） | 6144 | 1536 | 1536 | 24 | 528 |

fragmented 每四个蒙皮 Actor 中最后一个有真实 Lua behaviour，另外 1536 次脚本 Actor 更新不包含在候选计数内；每个连续候选批次只有三个实例，故无法命中并行。没有为了提高命中率越过脚本观察边界。rate_limited 只有四分之一的更新评估姿态。这些是新建验收项目数据，不代表所有用户项目的命中率。

测试链接实际 OvEditor 构建对象，运行真实 Editor / EditorActions、Scene View / Game View / Asset View：

- Edit 与 Pause 状态精确不变；Play、逐帧、恢复播放后，与另一个串行 Scene 的完整姿态/版本/时间逐次精确一致。8 次单步均只执行一次并自动返回 Pause；随后额外 Update 不推进。
- 12 帧真实多视图渲染，调换 Game/Scene 消费顺序，并插入 Asset View；24 组同视图 RGBA 逐字节一致，11 次动画图像变化，渲染前后完整姿态状态不变。Game View 有非背景几何。另有原测试的每配置 24 帧 32 实例串并 RGBA 对照。
- Stop 通过真实编辑器备份还原原始姿态；8 轮 Play → 延迟切换 crowd/fragmented → 更新 → 改变目标尺寸/多视图渲染 → Stop，均还原成功。测试退出时真实 Editor、Scene 和 Context 完成销毁。
- 测试副本仅把窗口设为隐藏、不聚焦，把 AppData 路径改到验证输出；使用只改变访问权限的头文件副本读取状态/framebuffer。不修改生产编辑器模式逻辑、用户布局、设置或项目注册表。未操作 VS2022 或桌面鼠标键盘。

这完成了本阶段约定的代表性项目及编辑器代码交互验收；不是所有 UI 布局、硬件/平台或任意重入场景的穷尽证明。

## 回归与构建

| 验证 | 本轮结果 |
|---|---|
| Debug/Release 旧源码精确对拍 | 各 11616 份状态、12456864 字节；24 帧、7372800 字节完全一致 |
| Debug/Release 当前串并批次 | 各 9600 份状态，单 worker 各 1024 份；24 帧精确一致 |
| 发布/异常/生命周期 | 7 处同步失败、Lua/自定义观察者、模型/组件/Actor 替换、4 处部分提交失败、40 次销毁循环通过 |
| 新统计 | 小批次、并行门槛、提交数与暂停后重置通过 |
| Release ASan | 测试、CSkinnedMeshRenderer、Scene、JobSystem 插桩路径通过；其他库正常链接，非全引擎 ASan/TSan |
| Release MutexDeque | 同一批次回归通过 |
| 阶段 0 渲染回归 | 每配置 580 组 Parse/Filter 一致；RGBA 一致，37341 个非背景像素 |
| 真实 Editor 项目 | 两配置三场景统计一致，模式/多视图/切场景检查通过 |
| 完整 Debug/Release 解决方案 | 均 0 警告、0 错误；10 项程序/资源复制相同 |

正式构建日志：msbuild-debug-skinning-stage1-retry.log、msbuild-release-skinning-stage1-final.log；复制：skinning-stage1-build-artifacts.json。最初沙箱内 MSBuild FileTracker 被拒绝访问，随后停止失败构建，在已授权的沙箱外完成构建。Debug 第一次正式构建与旧进程短暂重叠导致 assimp PDB 写入冲突，停止旧进程后重跑通过；这些失败日志不作为通过证据。

其余证据位于 Build/JobsValidation/Skinning 下对应配置目录、skinning-stage1-*.log 及 scene_stage1_{debug,release}.log。Build 被 Git 忽略；可跟踪的测试入口、测试源码与报告保留复现方法。没有提交或重置工作树。

## 最终独占性能

最终生产源码，Tracy 未连接，4/11 worker 各配置顺序运行，无其他本轮构建或测试重叠。仍为 16 次预热、64 次串并交替测量，中位数，单位 μs：

| 配置 / worker | 实例数 | 串行 | 批次 | 加速比 |
|---|---:|---:|---:|---:|
| Debug / 4 | 16 | 1017.9 | 399.5 | 2.55× |
| Debug / 4 | 64 | 3932.1 | 1426.0 | 2.76× |
| Debug / 4 | 256 | 16615.1 | 5740.7 | 2.89× |
| Debug / 11 | 16 | 1001.1 | 227.3 | 4.40× |
| Debug / 11 | 64 | 4025.1 | 755.5 | 5.33× |
| Debug / 11 | 256 | 16306.4 | 2942.9 | 5.54× |
| Release / 4 | 16 | 122.9 | 54.2 | 2.27× |
| Release / 4 | 64 | 493.1 | 177.6 | 2.78× |
| Release / 4 | 256 | 2031.1 | 673.7 | 3.01× |
| Release / 11 | 16 | 122.8 | 35.6 | 3.45× |
| Release / 11 | 64 | 493.1 | 104.4 | 4.72× |
| Release / 11 | 256 | 2005.4 | 394.1 | 5.09× |

1/4/8 个 Mannequin 仍走串行，小样本 Release 额外约 0.1～0.8 μs；Debug 的小样本差异约在 ±1.7 μs 内。Debug 4 worker 的 256 实例批次从修改前约 54.75 ms 降到约 5.74 ms（约 9.5×），这个跨版本优化收益与表中“最终串行/最终批次”的并行加速比分开报告。

日志：skinning-stage1-perf-{debug,release}-{4,11}.log；起止时间与退出码见 skinning-stage1-perf-results.json。11 worker 调用使用现有测试程序的 trace 参数来选择 worker 数，但未连接采集客户端；表格只取 trace 循环之前的 BENCH 行，不计末尾限流 sleep。

## 最终 Tracy 证据

Release、4 worker、256 实例，独占连接，源码路径核实为 Release-parallel/current。648 个完整批次全部有真实并行，主线程与 4 worker 最大并发 5；5832 个计算块、165888 次评估和发布。每批所有块及 Wait 结束后才开始 Publish，块均在 Batch 结束前完成。

| 区间 | 中位数 μs | p95 μs |
|---|---:|---:|
| Skinning Batch | 683.7 | 779.6 |
| Batch Prepare | 37.5 | 70.1 |
| 主线程自身块完成后的 Wait | 314.1 | 426.2 |
| 256 次 Publish 耗时之和 | 4.3 | 6.8 |
| 首次至末次 Publish | 11.9 | 16.9 |
| 最后一个块结束至首次 Publish | 3.2 | 5.6 |

Wait 包含 worker 剩余计算，不能理解为纯调度开销；Batch 不含外层 Scene 扫描。Tracy 会影响时间，不用连接后的数据与未连接的串行基准计算加速比。采集/导出/分析证据为 Build/JobsValidation/tracy-skinning-stage1-release*，分析脚本 analyze-skinning-stage1.mjs。重叠和 ASan 均不是无竞争的形式化证明。

## 阶段状态

阶段 1 在上述 Windows/VS2022/OpenGL、代表性新建项目与明确生命周期约束内完成验收。下一步是阶段 2 的独立增量：先梳理可交接帧数据的所有权，在同线程建立渲染命令生成/消费对照；多视图、资源保持与蒙皮数据不得浅拷贝借用到下一帧。不要直接引入 Render 线程或把本阶段结论外推为完整多线程渲染器已完成。
