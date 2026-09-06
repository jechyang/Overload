# 蒙皮计算/发布与批次并行回归

覆盖阶段 1 的同步分离与有界批次调度。同步旧/新路径对照入口：

```bat
node Tests\Skinning\run-validation.mjs Debug
node Tests\Skinning\run-validation.mjs Release
node Tests\Skinning\run-validation.mjs Release --asan
```

需要先生成并构建对应配置的完整解决方案，且 Build/<配置>/Data/Engine 有模型和着色器资源。脚本通过 vswhere 发现 VS2022 的默认工具链和已安装 Windows SDK；测试使用与引擎相同的 /WX、运行库和 Tracy 开关。输出、独立编译参数、日志和二进制快照位于被忽略的 Build/JobsValidation/Skinning/<配置>/。

每个配置分别编译两个独立程序：baseline 取自提交 `73ae710a57889bfde94184c3c8f073228e998144` 的 CSkinnedMeshRenderer 头文件、实现及 Scene.cpp，current 使用工作树源码。两者直接编译当前 JobSystem.cpp，链接相同配置的其他引擎库。测试头文件副本只开放访问权限，方便观测内部矩阵并构造无 GPU 资源的合成骨架；不会修改生产头文件或增加生产测试 API。`--current` 可只运行新路径的检查，不声称完成旧路径比较。

验证内容：

- 32 组速度、循环与评估频率组合；两实例共享模型、不同时间，连续 180 帧。覆盖暂停/停止/时间跳转、手动位置/旋转/缩放、非激活对象、外部动画源和节点重排、不兼容源、零时长、空轨道、缺失动画、默认 ticksPerSecond、越界 track/骨骼/动画索引、模型替换/清空、组件和 Actor 删除、Scene 销毁。
- local/global pose、矩阵与转置、版本、手动姿态标志、时间/播放状态、boundsScale，以及骨骼查询结果按标量写入 states.bin；两个程序的输出必须逐字节一致，不使用浮点容差。
- 真实 Mannequin 动画 24 帧，在隐藏 OpenGL 窗口中渲染并读回 RGBA，要求有非背景像素且动画导致图像变化；frames.bin 必须逐字节一致。读取渲染前后版本，防止渲染额外推进姿态。
- 新路径单独检查四个矩阵缓冲和手动编辑缓冲的分配失败、计算到中途才发现的非法父节点；失败不得改变已发布的数组、指针、版本或手动姿态标志。检查成功计算在 Publish 前不可见、错误暂存区可恢复，以及模型重建失败时矩阵保持为空。
- 真实模型预热后的单实例 SetTime 中位数和 p95，仅用于观测同步重构开销，不代表整帧收益或并行加速比。

ASan 模式插桩本测试、组件实现、Scene.cpp 和 JobSystem.cpp，链接其余正常引擎库；为保持与这些库一致，禁用 STL 的 vector/string 容器注解。它可以发现被插桩路径中的越界和释放后访问，但不等价于全引擎 ASan、TSan 或跨线程安全证明。分配失败仅注入目标矩阵或任务控制块分配，不模拟整个进程耗尽内存。

Tracy：新路径有 `Skinning Prepare`、`Skinning Evaluate`、`Skinning Publish` 三个区间。给独立测试程序第二个参数（输出目录之后）即可进入 15 秒采集段，测试仍在主线程执行；其中的 sleep 仅用于限制采集体积，不能当作调度开销或帧率。

## 批次并行

```bat
node Tests\Skinning\run-validation.mjs Debug --parallel
node Tests\Skinning\run-validation.mjs Release --parallel
node Tests\Skinning\run-validation.mjs Release --parallel --asan
node Tests\Skinning\run-validation.mjs Release --parallel --mutex-deque
```

`--parallel` 编译 parallel_validation.cpp，在同一程序中比较当前串行 Scene::Update 与生产批次重载，输出目录追加 `-parallel`；`--mutex-deque`、`--asan` 分别追加目录后缀，互不覆盖。

- 0/1/4/7/8/16/64 个实例、96 帧，共 9,600 份精确状态比较；单 worker 另有 1,024 份回退路径比较。
- 真正的 Lua behaviour 与派生自定义组件验证前序/自身/后序 Actor 的即时观察顺序；覆盖跨 Actor 姿态修改、模型解绑、组件替换、回调删除后序 Actor 后立即创建新 Actor。
- 验证被动等待不执行排队的主线程删除任务；拒绝非构造线程调用、外部线程池句柄和主线程亲和句柄。普通 Wait 仍能执行主线程任务。
- 计算失败保留失败实例旧姿态、成功实例有序发布、主线程收到异常且可恢复；四处部分提交失败均 drain 后才销毁 Scene，另有 40 次场景更新/销毁循环。异常不得进入 worker 错误回调。
- 32 个真实 Mannequin 实例、24 帧串/并行 RGBA 完全一致，检查非空画面及动画变化。
- 1/4/8/16/32/64/128/256 个真实实例各预热 16 次、取 64 次中位数，串/并行交替测量。并发运行其他测试时的数据不能作为独占性能基线。Debug 大批次已观察到变慢，Release 收益不能外推为 Debug 或完整编辑器帧率收益。

并行程序附加第二个参数后采集 256 实例轨迹；新增 `Skinning Batch`、`Skinning Batch Prepare`、`Skinning Job`、`Skinning Wait` 区间。采集时只运行一个 Tracy 客户端，检查导出 CSV 的源码路径确属 `Release-parallel/current`。验收与具体所有权/失败语义见根目录 MULTITHREADING_SKINNING_PARALLEL_VALIDATION.md。

## 阶段 1 收尾：性能与真实编辑器项目

```bat
node Tests\Skinning\run-validation.mjs Debug --editor
node Tests\Skinning\run-validation.mjs Release --editor
```

先完成对应配置的完整解决方案构建。编辑器测试链接该配置的真实 OvEditor 对象文件，运行 Editor / EditorActions 的 Edit、Play、Pause、NextFrame、Stop 与延迟切场景；不能使用过期对象文件。仅测试副本将 Context 窗口设为隐藏、不聚焦，并将 SystemCalls::GetPathToAppdata 返回值重定向到测试输出下的 AppData，不改用户编辑器设置、布局或项目注册表。访问权限副本用于读取姿态和 framebuffer，不改变对象布局或生产模式切换逻辑。

输出位于 Build/JobsValidation/Skinning/<配置>-editor/current/：

- SkinningProject/SkinningProject.ovproject 及 Assets 下三个实际序列化场景，可从编辑器打开。crowd 是 64 实例连续批次，fragmented 每四个蒙皮 Actor 放一个 Lua behaviour，rate_limited 为 15 Hz 姿态评估。资源引用引擎自带 Mannequin / Default 材质。测试会重新生成这个专用项目。
- project-batches.csv：每个场景连续 96 次 Update 的候选/实际评估/并行实例和任务数。统计只覆盖已审查的候选批次，不能把它解释为整个场景中任意脚本触发的全部姿态计算。脚本实例仍按原顺序同步更新。
- 真实编辑器模式与另一个串行 Scene 逐状态精确比较；暂停和 Edit 不推进；逐帧恰好一次并自动回 Pause；Stop 从序列化备份恢复。
- 实际 Scene View / Game View / Asset View 独立 renderer，12 帧调换消费顺序，24 组同视图 RGBA 精确比较，且渲染前后完整姿态状态不变。再执行 8 次延迟场景切换、目标尺寸变化与停止还原。
- editor-game.rgba 和 editor-game-size.txt 保存第一帧 Game View 原始证据，像素从左下向上排列。

这是隐藏窗口中的真实编辑器代码验收，未模拟鼠标键盘操作，不代表所有 UI 布局、平台或任意用户项目已覆盖。

批次回归现另检查可选 SkinningUpdateStats 的小批次、门槛、任务计数与暂停后重置。蒙皮热区改为只读 span 采样及直接 TRS 矩阵表达式，保持运算顺序；Debug 与 Release 均保留批次默认路径及原阈值，完整结果见 MULTITHREADING_SKINNING_STAGE1_COMPLETION.md。

若 vswhere 在受限环境下返回空，可设置 VSINSTALLDIR，或添加 `--vs "C:/Program Files/Microsoft Visual Studio/2022/Community"`。脚本不会修改系统工具链设置。性能结论只使用没有其他构建、测试或 Tracy 客户端同时运行的日志。
