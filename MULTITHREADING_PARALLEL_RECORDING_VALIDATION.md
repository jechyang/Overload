# 阶段 4：多 Pass/View 并行命令录制

日期：2026-09-08。范围：MULTITHREADING_NEXT_STEPS.md 第 6 节。**阶段 4 已完成约定范围验收；未推进阶段 5/6。**

## 所有权与依赖

Main 仍按原顺序准备视图和 Pass：相机、灯光、反射历史、材质 singleUse、feature 缓存与 UI 不允许被多个 worker 同时修改。新的 Material::PrepareSnapshot 固定选中 program 的代际、数值和 sampler 存储；不会把 Material、Shader 资产或 Texture 资产裸指针交给编码任务。相同签名复用不可变输入，属性和完整快照通过 once_flag 各编码一次。

| 阶段 | 依赖与线程 |
|---|---|
| View/Pass 准备 | Main，维持既有顺序，先处理真实 CPU 历史依赖，再捕获拥有型输入 |
| 命令编码 | Stage 0 JobSystem，各连续块独占输出槽；仅访问固定值和持有的只读 program 元数据，不调用 GL/窗口/UI |
| 汇合与封口 | 显式依赖全部 worker 的 fan-in；Main 计算自己一个块，WaitAllPassive 不泵入任意 Main 回调；检查每块成功标志后才发布 READY |
| GPU 消费 | 固定 owner，原 Shadow → Reflection → Opaque → Transparent → PostProcess/UI，以及 Asset → Game → Scene → UI 的命令位置保持不变 |
| 失败与释放 | 部分提交、编码或汇合失败先 drain；整帧失败且不执行任何 GPU 前缀，相关历史失效；成功帧仍由原 fence 协议退役 |

Editor 将多个视图的未封口列表交给同一帧，所有拥有型输入在最终 Seal 中统一编码。独立列表仍支持在 FinishCommandRecording 中直接编码。CPU 编码可互相独立，是因为真实 CPU 状态依赖已经在顺序准备期解析；纹理的 GPU 写读依赖继续由固定命令顺序表达，并未把 GPU Pass 本身并行执行。

不修改 Parse/Filter 阈值或姿态评估次数；不在 worker 再次 Parse 或重新评估动画。现有 renderer、feature、palette 上传缓存保持各自顺序归属，没有通过在外层并发调用旧 Render/DrawFrame 来绕过共享状态。

## 配置与门槛

项目设置增加 parallel_recording=true，要求使用已有的拥有型命令路径。默认 false，render_thread 默认值也保持 false。此开关可用于同线程 GL owner 或独立 Render owner，改变的是 CPU 命令编码。

少于 128 个待编码 draw、少于两个 worker 或没有提供池时采用同步编码。并行路径每块目标至少约 128 个 draw，最多 Main 加四个 worker；Main 只计算自身块，不运行其他队列回调。小批次与具体材质的收益需按实际负载判断。保留旧 RecordCachedSnapshot 路径和准备后串行编码对照。

## 验证入口

先 gen_proj.bat vs2022，再完整构建 Debug/Release。可跟踪测试入口：

~~~text
node Tests/Skinning/run-validation.mjs Release --parallel-recording
node Tests/Skinning/run-validation.mjs Debug --parallel-recording
node Tests/Skinning/run-validation.mjs Release --parallel-recording --asan
node Tests/Skinning/run-validation.mjs Release --parallel-recording --mutex-deque
node Tests/Skinning/run-validation.mjs Release --editor-stress
node Tests/Skinning/run-validation.mjs Debug --editor-stress
node Tests/Skinning/run-validation.mjs Release --game-thread
node Tests/Skinning/run-validation.mjs Debug --game-thread
node Tests/Skinning/run-validation.mjs Release --recording-benchmark
node Tests/RenderingCommands/capture-parallel-recording.mjs
~~~

专项使用隐藏窗口，包含原动态阴影/反射/渐变曝光/后处理图像和历史恢复回归；新增有界批次、固定合并顺序、部分提交/汇合失败、主线程重入隔离、跨视图整帧失败、源销毁资源代际测试，以及三种编码路径的真实多 Pass 对拍。ASan 直接插桩测试、JobSystem、Scene、ABaseRenderer、CompositeRenderer、Material、RenderCommandList 和 RenderFrame，其他引擎库保持原构建，不能称为整个引擎的 TSan/ASan 证明。

## 最终验证结果

全部在本工作区保留用户既有修改的条件下完成；没有把整个工作树称为干净历史提交。

| 检查 | 结果与证据 |
|---|---|
| 完整解决方案 Debug/x64、Release/x64 | 均退出 0，零编译警告/错误；msbuild-debug-stage4-verified、msbuild-release-stage4-complete 日志 |
| 程序与代表性资源复制 | 两配置 Editor/Game、Cube、Mannequin、Standard 共 10/10 项 SHA-256 一致；stage4-build-artifacts.json |
| Debug/Release --parallel-recording | 均通过原完整动态命令回归和新增专项；包括 0/1/127/128/1024 批次、失败排空、Main 回调不重入和移动/取消 |
| 跨视图依赖与整帧失败 | 三个 view 的未封口列表聚合；共享 history 顺序、child completion 晚于整帧 GPU fence；中间编码失败则整帧 FAILED/retired，不播放前缀 |
| 共享材质/资源压力 | 每配置 12×3×4×192 = 27,648 次真实 draw；像素及 draw 顺序/模型/tint 精确一致；1,024 份输入、8 代 program/texture 在源销毁后编码回放并最终释放 |
| Release --parallel-recording --asan | 通过，插桩范围如上；无内存错误 |
| Release --parallel-recording --mutex-deque | 通过，同一图像、生命周期和异常断言 |
| Debug/Release --editor-stress | 各通过 16 组产品线程图像/姿态、完整 UI 与拾取、Gizmo 积压输入、96 帧完整 Canvas/12 次切换、实际最小化恢复、退出/重建/失败 |
| Debug/Release --game-thread | 各通过 32 帧产品图像/姿态、两帧在途、更新交叠、最小化恢复、退出/重建/失败 |
| Release --draw-inputs 历史串行基线 | 七份 trace/image 文件逐字节一致；包含材质、几何、palette、蒙皮及源销毁测试 |
| Release --recording-benchmark | 九个真实 Editor case、1,440 个统计帧；时间顺序、帧 ID、退役、交互及容量 2 断言通过 |
| Release 独占 Tracy | harness/capture/export 均退出 0；36 个完整 case，12/12 并行 case 真实重叠，最大并发 5，全部 worker 在封口前结束 |

## 性能与默认配置决定

本机和编译条件与阶段 3 报告相同：i5-12490F、12 逻辑 CPU、RTX 3070 Ti、Windows、VS2022 v143 Release /O2、TRACY_ON_DEMAND。未连接与连接 Tracy 的结果分开；正式性能运行和 Tracy 采集保持独占，不与构建或其他引擎测试并发。

专项固定 4 pass × 192 draw；共享材质使用 27 个 active uniform（tint、transform、sampler 和 24 个矩阵），每个 draw 修改单次 tint，stable 矩阵保持复用。每种模式 12 帧，未连接结果去掉前 2 帧后统计 10 帧。表中时间是 CPU 墙钟微秒，包含该模式完整准备和 Seal；不包含 GPU 回放。旧录制在准备期完成 uniform 编码，所以其 Seal 很短。

| 模式 | 准备 p50 | 编码/调度/等待/合并 p50 | 全部录制 p50 | 全部录制最大值（10 样本） |
|---|---:|---:|---:|---:|
| 旧录制 | 1500.0 | 0.8 | 1500.6 | 1911.1 |
| 准备后串行 | 606.1 | 343.9 | 960.3 | 1092.4 |
| 准备后并行 | 641.8 | 189.3 | 826.9 | 996.3 |

总录制中位数比相同准备的串行路径减少 **13.9%**，比旧录制减少 **44.9%**。后一个差值还包含不可变属性/快照复用收益，不能全部归因于 worker。各列分位数独立计算，不要求其相加等于总量分位数；小样本不用于宣称稳定尾延迟或普遍加速。

Tracy 核对的是实际源码区间而非线程存在：36 个 Stage4 Recording 外层区间各包含一个 Record Pass Commands；串行仅 Main 一个块，并行每次 Main 一个块加四个 worker 块，12/12 次有真实重叠，峰值 5。没有任何块越过编码封口边界。连接采集的全部 12 帧中位数如下，单位仍为微秒：

| 模式 | 全部录制 | 编码（含调度/等待/合并） | Wait Pass Recording | Merge Pass Recording |
|---|---:|---:|---:|---:|
| 旧录制 | 1776.940 | 0.022 | 0 | 0 |
| 准备后串行 | 1207.784 | 412.008 | 0 | 4.178 |
| 准备后并行 | 1019.142 | 241.675 | 76.825 | 4.295 |

并行总体仍减少 188.642 微秒，已经包含提交和 wait/merge 开销，满足有足够编码工作的多 Pass 负载验收。Wait 区间与 worker 执行重叠，不能再加到编码时间上。Tracy 连接有观察开销，不与未连接样本交叉计算收益。

真实 Editor 使用既有 crowd 场景的 64 个动画实例，Asset/Game/Scene 三视图与固定 Render owner，隐藏窗口、关闭 VSync、固定步长 1/60 秒。每模式三轮，每轮 192 帧前 32 帧预热，正序/逆序/正序运行。每种方式 480 个统计帧，GPU span、队列、呈现和交互含义沿用 MULTITHREADING_FRAME_PERFORMANCE_VALIDATION.md，未混用 CPU/GPU 时间原点。

| 模式 | FPS 三轮中位数（范围） | PreUpdate+Update p50 ms | Main 整帧 p50 ms | GPU span p50 ms | 呈现返回延迟 p95 ms | 交互观察延迟 p95 ms |
|---|---:|---:|---:|---:|---:|---:|
| 旧录制 | 90.38（89.83–90.52） | 4.4205 | 10.6896 | 9.5365 | 29.3460 | 42.1129 |
| 准备后串行 | 90.51（90.12–90.60） | 4.4509 | 10.8040 | 9.5355 | 29.4242 | 42.2088 |
| 准备后并行 | 90.53（90.09–90.70） | 4.4273 | 10.7824 | 9.5375 | 29.4141 | 42.2827 |

实际编辑器这组负载没有可据此主张的明显吞吐提升；PreUpdate+Update 也并非单独的录制时间。GPU span 约 9.54 ms，容量等待中位数约 6.1–6.3 ms；不能将小幅 FPS 波动解释为 CPU 并行收益。两帧接收容量和按序交互语义保持不变。早期三个 uniform 的简单材质实验甚至出现准备成本超过收益，因此最终采用有界分块、跨视图统一编码，并保留 **parallel_recording=false** 为产品默认值。有较重材质编码的项目可显式开启并复测；阶段 4 不承诺所有材质、场景或硬件均更快。

## 证据路径与范围

Build/JobsValidation/ 为本地生成的忽略目录；可跟踪的测试和分析脚本在 Tests/RenderingCommands 与 Tests/Skinning。主要证据：

- msbuild-debug-stage4-verified-result.json、msbuild-release-stage4-complete-result.json 及同名日志；stage4-build-artifacts.json。
- Skinning/<配置>-parallel-recording-command-list[/current]、对应 -asan/-mutex 的 result.json、compile.log、run.log 与图像/trace；Skinning/Release-draw-inputs 的 baseline/current 文件。
- stage4-product-regressions.json 与 stage4-<配置>-editor-stress/game-thread 等日志；各 Skinning 产品结果目录。
- Skinning/Release-parallel-recording-command-list/current/parallel-recording-metrics.csv、parallel-recording-metrics-analysis.json，以及独立 -connected 文件。
- Skinning/Release-recording-benchmark-command-list-editor-ui/current/product-frame-metrics.csv 与 -analysis.json。
- tracy-parallel-recording-release.tracy、-result.json、-events.csv、-events-analysis.json；分析器核对完整外层 case、源码文件、线程身份、实际重叠与发布边界。

CPU 准备成功后已消费的 singleUse 不因后续编码分配失败而回滚；失败使整帧及其 speculative history 失效，并在所有任务终止后释放持有资源。该协议不是场景状态通用事务。这里只把拥有型 draw 命令的编码交给 worker，pass/view 的可变 CPU 准备、平台/UI 和所有 GL 消费继续遵守原 owner 边界；没有引入 RHI、Vulkan、fiber 或跨帧可变缓存共享。
