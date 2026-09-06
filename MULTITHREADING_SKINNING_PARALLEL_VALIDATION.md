# 阶段 1 第二增量：有界蒙皮批次并行验收

日期：2026-09-06。基于阶段 0 与同步计算/发布分离的未提交工作区；保留既有用户改动。本增量完成生产调度接入和下列自动化验收，未引入独立 Render 线程。本文件记录第二增量时的状态。Debug 性能与代表性编辑器项目交互已在第三增量验收，最新结论见 MULTITHREADING_SKINNING_STAGE1_COMPLETION.md；下文旧性能数值作为历史基线保留。

## 实现和生存期

- `Scene::Update(float, JobSystem&)` 按原 Actor 顺序扫描，只合并连续、激活、没有 behaviour 的 Actor。组件必须是精确的 CTransform / CModelRenderer / CMaterialRenderer / CSkinnedMeshRenderer 类型；前三种的 OnUpdate 已核实为空。派生/未知组件、缺失蒙皮、重复蒙皮、模型尚未同步均形成边界。
- 每批主线程推进播放时间、筛选需要评估的实例、准备借用输入；worker 只读骨架/动画/节点映射，独占对应实例的暂存姿态。批次期间不执行任意组件、脚本或主线程队列回调。主线程在全部任务终态后按实例原顺序发布，随后才更新边界 Actor。
- 不提前推进边界之后 Actor 的时间；Lua 可以看到已更新的前序 Actor、自身新姿态和仍未更新的后序 Actor。模型/动画变更、Stop、SetTime、手动姿态和直接 OnUpdate 继续同步完成。
- 借用输入仅在本批次内有效，不跨帧。其他线程不得修改场景或资源；这沿用主线程资源所有权约束，不提供任意异步热重载保护。未增加伪装为所有权保证的 generation 标志。
- 新增 `WaitAllPassive`：仅在线程池构造线程等待本池 Any 亲和任务，不帮忙运行任意队列工作。直接线程/句柄/亲和误用抛出异常；传递依赖不得等待主线程，由调用方保证。蒙皮只提交无依赖任务，主线程显式计算自己的连续块，避免普通 Wait 重入场景删除回调。
- 两个 Scene::Update 重载均用销毁事件使已删除 Actor 的快照项失效；回调新建 Actor 从下一次 Update 开始更新，即使地址被复用也不会误更新。这个修复限于 Update，不声称改造了 FixedUpdate/LateUpdate 的全部重入行为，也不允许在自身调用栈中立即销毁正在更新的 Scene。
- Editor 的 Play/逐帧路径和 Game 已接入。Edit 模式不推进动画，AssetView 无独立骨骼动画评估；渲染仍只消费已发布姿态，GL 保持主线程所有权。

## 调度和失败语义

少于 8 个候选实例或少于 2 个 worker 直接走原 OnUpdate。准备后实际待评估少于 8 个实例或总节点数少于 512 时串行计算。大批次分成 `min(实例数, 2×worker+1)` 个连续块，主线程显式处理一个块，其余提交 worker。阈值仅为本机当前负载的保守门槛，不能视为所有骨架/硬件的最优值。

每个任务逐实例捕获计算异常，不从 worker 调用日志/UI 错误回调。全部任务结束后，成功实例发布，失败实例保留旧数组和版本；主线程按实例顺序重抛第一个异常。提交到一半失败则先等待已提交任务，整批不发布，然后重抛。所有引用和暂存容器在正常返回与异常展开时均活到任务终态。

这是姿态发布保证，不是整批播放状态事务：准备阶段已经推进的时间、播放状态和频率累积不会回滚。并行计算失败时成功同伴仍发布，区别于旧串行路径遇到首个异常就停止；没有将半成品作为成功姿态发布。准备/分配阶段抛错亦不能声称完整回滚。线程池并非全局内存耗尽可恢复系统。

## 自动化结果

| 验证 | 结果 |
|---|---|
| Debug / Release 当前串行与生产批次 | 每配置 9,600 份实例状态精确一致，0/1/4/7/8/16/64 实例，96 帧 |
| 单 worker 回退 | 每配置另有 1,024 份状态精确一致 |
| 实际 Lua / 自定义组件 | 前序、自身、后序姿态观察一致；跨 Actor 修改、组件/模型替换、后序 Actor 删除再创建均通过 |
| 等待隔离 | 主线程删除任务不会在批次中执行；普通 Wait 后续可执行；错误线程、外部池、错误亲和拒绝检查通过 |
| 失败 / 生命周期 | 计算中途非法层级保留旧姿态并可恢复；4 处部分提交失败；40 次更新/销毁循环通过 |
| 多实例实际蒙皮渲染 | 32 个共享模型不同时间实例，24 帧 RGBA 逐字节一致，23 次图像变化 |
| MutexDeque 对照 | Release 同一整套批次检查通过 |
| Release ASan | 测试、CSkinnedMeshRenderer、Scene、JobSystem 插桩路径通过；其他引擎库未插桩，非 TSan 证明 |
| 原同步旧源码回归 | Debug / Release 各 11,616 份状态一致（12,456,864 字节）、24 帧一致（7,372,800 字节）；7 处失败/发布隔离/重建恢复通过 |
| 阶段 0 Parse / Filter | Debug / Release 各 580 组一致，原离屏图像一致，37,341 个非背景像素 |
| 完整解决方案 | Debug/x64、Release/x64 均退出 0，0 警告、0 错误；10 项程序与资源复制核对一致 |

状态对照包含 local/global pose、骨骼矩阵及转置、版本、手动标志、时间和播放状态、boundsScale、骨骼查询。覆盖速度正负/零、循环/非循环、停止/暂停/拖动、频率限制、非激活、缺失/零时长动画、不同源及映射、模型替换；无浮点宽松容差。ASan 禁用 vector/string 容器注解以兼容正常引擎库。

## 性能和 Tracy

使用真实 Mannequin（59 节点、45 骨骼）、4 个 worker；测量包含 Scene 扫描、准备、调度、等待与发布，不含渲染或固定 sleep。以下取独占运行的 `tracy-skinning-parallel-release-final-harness.log` 中、Tracy 尚未连接时的数据；每组预热 16 次，串/并行交替测量 64 次，分别取中位数。

| 实例数 | 串行 μs | 批次路径 μs | 串行 / 批次 |
|---:|---:|---:|---:|
| 1 | 12.5 | 13.0 | 0.96× |
| 4 | 49.4 | 51.9 | 0.95× |
| 8 | 102.4 | 104.3 | 0.98× |
| 16 | 201.4 | 93.5 | 2.15× |
| 32 | 404.9 | 184.6 | 2.19× |
| 64 | 811.5 | 380.7 | 2.13× |
| 128 | 1656.7 | 590.7 | 2.80× |
| 256 | 3341.3 | 1182.2 | 2.83× |

该模型 8 个实例合计 472 节点，因此仍为串行分支；小样本额外扫描/准备约 0.5～2.5 μs。节点数只能粗略估算工作量，不能把 512 当作普遍最优门槛。Debug 验证运行中，大批次约为串行耗时的 1.5～1.7 倍；其运行曾与其他验证重叠，数值不作为独占基准，但多次均确认了变慢方向。原因尚未定位，保留串行重载供对照，下一增量处理 Debug 默认策略。不能把 Release 的加速比外推为 Debug 或完整编辑器帧率收益。

Tracy 连接后独占采集了 522 个完整的 256 实例批次：每批 9 个连续块、256 次评估和发布，均在 5 个线程上发生区间重叠（主线程及 4 worker），共 4,698 个块、133,632 次评估。逐批检查全部块结束后才出现 Publish，所有块均在 Batch 结束前完成。CSV 源码路径核实为 Release-parallel/current。

| Tracy 区间 | 中位数 | p95 |
|---|---:|---:|
| 整个 Skinning Batch | 1465.0 μs | 1600.2 μs |
| Batch Prepare | 39.5 μs | 73.7 μs |
| 主线程完成自己块后的 Wait | 738.9 μs | 1012.3 μs |
| 256 次 Publish 区间耗时之和 | 4.4 μs | 5.8 μs |
| 首次 Publish 开始到末次结束 | 13.6 μs | 18.7 μs |
| 最后一个块结束到首次 Publish | 3.3 μs | 5.0 μs |

Wait 包含 worker 尚未完成的计算和唤醒，不是纯调度成本。Prepare 不包括外层 Actor 扫描；Batch 也不覆盖整个 Scene::Update。Tracy 连接本身改变耗时，不能拿连接后的 1465 μs 与未连接串行数值算加速比。sleep 仅控制采集体积；区间重叠不是无竞争的形式化证明。采集后补强了重排动画源测试夹具及兼容性断言，生产代码和这个 256 实例轨迹负载未改变。

## 证据和复现

- 可跟踪入口：Tests/Skinning/run-validation.mjs，参数见 Tests/Skinning/README.md。
- 批次日志/参数：Build/JobsValidation/Skinning/{Debug-parallel,Release-parallel,Release-parallel-mutex,Release-parallel-asan}/；同步旧/新比较在 Debug/Release 子目录。
- 最终构建：Build/JobsValidation/msbuild-{debug,release}-skinning-batch-final.log；复制：skinning-parallel-build-artifacts.json。
- 阶段 0 本次回归：Build/JobsValidation/scene-skinning-{debug,release}.log。
- 最终独占 Release 采集：Build/JobsValidation/tracy-skinning-parallel-release-final*。采集程序必须是 Release-parallel/current，不能与其他 Tracy 客户端同时运行。采集中的 sleep 仅用于限流。
- Build/JobsValidation 被忽略，换机器不会自动带走这些产物；测试入口和本报告在可跟踪目录。

后续状态（2026-09-06）：上述 Debug 性能与代表性新建项目/编辑器交互待办已完成，阶段 1 已在声明范围内验收。见 MULTITHREADING_SKINNING_STAGE1_COMPLETION.md 和 MULTITHREADING_NEXT_STEPS.md 的 3.6 节；下一增量从阶段 2 的同线程命令与数据所有权开始，不直接引入 Render 线程。
