# 阶段 1 首增量验收：同步姿态计算与发布分离

日期：2026-09-06。工作区：H:/overload/Overload。阶段 1 **尚未整体完成**；本次只完成任务化之前可独立验收的同步重构，没有生产蒙皮 job、独立 Render 线程或跨帧交接。

## 调用顺序与实现边界

Scene::Update 按 Actor 顺序调用 OnUpdate。Actor 先按原组件顺序更新，再调用 Lua behaviours；Lua 绑定包含时间控制、动画源切换、骨骼 local/global 查询和手动编辑。脚本还可查询其他 Actor，因此提前推进全场景动画或统一延后发布都会改变可观察行为。后续调度必须处理这个边界，不能仅在 Scene::Update 末尾 Wait。

Game、Editor Play/逐帧模式均为 Scene::Update → LateUpdate → 后续渲染。Editor Edit 模式当前不推进场景动画；AssetView 的资源 Actor 没有蒙皮组件，也未发现独立动画预览评估。渲染的 SkinningUtils 保存矩阵指针/count/poseVersion/boundsScale，SkinningRenderFeature 消费该指针并上传 GL；Picking/Outline 也会读取组件。所有这些消费者继续位于同步姿态发布之后，GL 所属线程未改变。

CSkinnedMeshRenderer 的变化：

1. PreparePoseEvaluation 从当前组件状态构造只读骨架/动画引用、节点映射 span、时间和循环参数。输入不持有资源，稳定期仅为当前同步调用。
2. 静态 ComputePose 和 ComputeBoneMatrices 写入独占 PoseEvaluationOutput；不读取 Actor、UI、服务注册表，也不执行 GL。
3. PublishPose 通过不抛异常的 vector swap 发布 local/global pose、矩阵及转置，再更新手动标志和版本。暂存区复用上一份数组容量，正常连续采样不为每个矩阵另行分配。
4. 手动编辑先复制 local pose 到暂存区，编辑与派生矩阵全部完成后才发布。Stop/SetAnimation 的 manualPoseOverride 清除也随成功姿态发布。
5. 普通评估失败保留旧姿态；模型重建失败则保持空矩阵，防止使用与替换模型不匹配的旧姿态或未计算的占位矩阵。父节点必须早于子节点，非法层级在计算中抛异常。原采样、插值、乘法、转置运算顺序未调整。

这保证的是同步计算/发布边界，不是跨线程原子发布。播放时间、播放状态、模型/动画选择仍由原同步方法更新；异常不会回滚整个组件。当前沿同步调用链传播异常，生产异步错误观测策略仍待下一增量实现。借用引用也不保证未来后台任务期间的资源生存期。

## 正确性证据

可跟踪源码和运行说明：Tests/Skinning/serial_validation.cpp、run-validation.mjs、README.md。测试使用两个独立程序；旧组件源码固定取自提交 `73ae710a57889bfde94184c3c8f073228e998144`，新组件取工作树，其余引擎库使用相同配置。访问权限只在测试头文件副本中开放。

| 验证 | Debug/x64 | Release/x64 |
|---|---|---|
| 状态快照 | 11,616 份完全一致 | 11,616 份完全一致 |
| states.bin | 12,456,864 字节逐字节一致 | 12,456,864 字节逐字节一致 |
| 真实蒙皮 RGBA | 24 帧完全一致 | 24 帧完全一致 |
| frames.bin | 7,372,800 字节逐字节一致 | 7,372,800 字节逐字节一致 |
| 普通评估/手动编辑失败 | 7 个注入点通过 | 7 个注入点通过 |
| 计算结果未发布前不可见、模型重建失败及恢复 | 通过 | 通过 |
| 阶段 0 Parse/Filter | 580 组通过 | 580 组通过 |
| 原阶段 0 离屏图像 | 逐像素一致，37,341 非背景像素 | 逐像素一致，37,341 非背景像素 |

状态快照覆盖 32 组循环、正负/零速度和频率限制的组合，每组两个共享模型实例、180 帧；包含暂停/停止/时间跳转、手动位置/旋转/缩放、非激活对象、不同动画源与节点重排、不兼容源、零时长、缺失动画/空轨道、默认 ticksPerSecond、越界 track 与索引、模型替换/清空、组件删除、Actor 删除和 Scene 销毁。比较四组矩阵、版本、手动标记、时间/播放状态、boundsScale 与骨骼查询结果，未使用容差。

真实图像用隐藏 320×240 OpenGL 窗口、Mannequin 的 root|air_jump 动画。24 帧中有 22 次相邻帧图像变化，最后一帧 933 个非背景像素，避免“空画面相等”或“未播放动画”的误判。渲染前后姿态版本一致，未重复推进动画。这不是完整编辑器多视图交互验收。

Release ASan 对本次组件及测试程序插桩，同样完成状态、7 个失败注入、重建恢复和连续图像检查。其他引擎库未插桩；为链接这些库，关闭 STL vector/string 容器注解。因此不能将结果称为全引擎 ASan 或数据竞争证明。

早期测试的全局分配失败注入也命中了 Debug STL/FTransform 内部容器，出现测试进程超时。最终测试把注入限定到矩阵大小的分配，逐一覆盖四个输出矩阵缓冲和手动编辑缓冲；Debug/Release 均通过。该测试不模拟整个进程内存耗尽，也不证明所有旧引擎容器具备异常安全性。

## 开销与 Tracy

同配置、未连接 Tracy 的 1,000 次预热后单实例 SetTime 样本，模型为 59 nodes / 45 bones：

| 配置 | 旧路径中位数 / p95 | 新路径中位数 / p95 |
|---|---|---|
| Debug | 128.8 / 161.7 μs | 128.0 / 147.6 μs |
| Release | 12.1 / 13.3 μs | 12.1 / 12.6 μs |

这些是一次本机微基准观测，不证明整帧加速；没有根据它调整任何并行阈值。新增存储代价是每实例额外一份 local/global pose 与两份 bone palette 数组；手动编辑还增加 local pose 复制。

最终 Tracy 采集：Build/JobsValidation/tracy-skinning-serial-release-final.tracy，对应 CSV 与 analysis.json 同前缀。核对 CSV 源路径为 Release/current，三个区间各 523 次，均在测试主线程执行：

| 区间 | 中位数 | p95 | 最大值 |
|---|---:|---:|---:|
| Skinning Prepare | 0.140 μs | 0.412 μs | 70.677 μs |
| Skinning Evaluate | 44.681 μs | 98.373 μs | 293.446 μs |
| Skinning Publish | 0.064 μs | 0.253 μs | 0.777 μs |

连接 Tracy、采集间隔及系统调度会影响这些样本，不与未连接时的 SetTime 数值混算加速比。采集循环的 sleep 只限制采集体积，不代表实际帧率或调度延迟。尚无 Skinning Wait 区间或并行收益数据；这是同步分离增量的预期范围。早先 `tracy-skinning-serial` 误连同时运行的 Debug 测试，已排除；采用所有测试结束后的单独 Release 采集。

## 构建与文件

- 最终全解决方案 Debug/x64、Release/x64：均退出 0、0 警告、0 错误，保留 /WX。日志：Build/JobsValidation/msbuild-{debug,release}-skinning-serial-final.log。
- 编辑器/游戏输出复制，以及 Cube、Mannequin、Standard shader：10/10 与来源逐字节一致；证据为 Build/JobsValidation/skinning-serial-build-artifacts.json。
- 独立对拍与 ASan：Build/JobsValidation/Skinning/{Debug,Release,Release-asan}/ 下的编译日志、运行日志、参数、快照和 result.json。
- 阶段 0 回归日志：Build/JobsValidation/scene-skinning-{debug,release}.log。
- 没有新增引擎编译单元，无需重新生成工程。没有操作 VS 界面，没有创建分支或提交，保留其他已有修改。Build 内证据被 Git 忽略；Tests/Skinning 及本文可随仓库保存。

## 下一增量

继续定义稳定的跨实例批次与同步边界，保留 Actor 组件/脚本即时观察顺序、直接 OnUpdate/SetTime 同步行为，明确资源生存期、失败结果和删除/切换/退出时的等待规则，再接入 JobSystem 与真实并行测量。阶段 1 的异步生命周期压力、多实例并行、Wait 开销、小批次阈值和完整阶段验收尚未完成；不自动进入阶段 2/Render 线程改造。
