# 实际场景渲染性能：第一轮交接档案

最后更新：2026-10-06。开发位置 `H:\overload\Overload`，分支 `main`。
Frame Graph 三项收尾已在本地提交 `9bb01a23` 完成，详情见
[Frame Graph 交接档案](FRAMEGRAPH_HANDOFF.zh-CN.md)。本档案记录随后开展的实际场景性能工作。

## 已完成与边界

| 工作 | 结果 | 状态 |
|---|---|---|
| 当前主线性能基线 | 六组 A/B、12 个场景，各 216 个有效样本；区分 Update、Extract、Prepare、Record、Execute、GPU playback | 完成 |
| 基准入口修复 | 使用独立适配入口补齐 OvReflection 链接库，编译临时目录留在工作区；原 PerformanceLab 源码和资产保持原样 | 完成 |
| 一次视图提取内复用材质快照 | 同一源材质、实际 Shader program 和接收纹理共用不可变 PreparedSnapshot，减少属性捕获、采样依赖遍历和 Worker 重复编码 | 完成 |
| 正确性与对照 | 全部 12 个最终读回图像逐字节一致，资产哈希和绘制/动画评估计数一致；原生 Debug/Release 与实际编辑器回归 | 完成，见验证记录 |
| 提前裁剪、自动实例化、产品整帧性能 | 尚未修改；剩余工作按下文推进 | 待做 |

复用仅存在于一次 ExtractImpl 调用内，返回后索引销毁；跨视图、跨帧和资产编辑不会
命中旧缓存。Snapshot 继续持有自己的资源 generation，Worker 不读取可变材质资产。
Shader 变体按实际 retained program 区分；一个材质的静态与骨骼变体不能混用。
阴影接收 map/matrix 在视图请求内固定，环境 map 可能因局部探针不同而分组；是否
存在覆盖值也参与区分。新增未知覆盖值保守地走独立路径。

拾取颜色、预览 fallback 的源材质覆盖，以及任何一次性属性继续逐 Draw 处理。
原来的可见性、排序和一次性属性消费顺序保留。源材质只用一次时不建立其快照缓存条目，
400 份独立材质不会被合并成一个源材质。没有新增 GPU 目标或长期资源缓存。

## 测量方法与结果

Release，RTX 3070 Ti，OpenGL 4.5 / NVIDIA 616.92，960×540，固定 dt=1/60，
4 个 Update Worker；Prepare/Record 使用串行模式，Tracy 未连接。每场景三轮各 96 帧，
每轮丢弃前 24 帧，共 216 样本；第二轮反向运行场景。性能采样期间不并行运行编译或其他验证。
单 GL owner、离屏运行，每帧 glFinish；不能换算为 Editor 实际 FPS。

以下对比使用重新运行的**原实现确认组**与**最终实现组**，单位 ms。
CPU 前端是 Extract + Prepare + Record 的逐帧合计，排除诊断计数；不含 Update、
Execute、Swap 或 GPU。CPU/GPU 计时不能相加，GPU playback 时间戳也含提交间隙，
不能将其变化解释为 Shader/GPU 运算量下降。

| 场景 | Extract p50：前 → 后 | CPU 前端 p50：前 → 后 | CPU 前端 p95：前 → 后 | 前端 p50 变化 |
|---|---:|---:|---:|---:|
| 01 裁剪关闭，1600 对象 | 9.105 → 4.498 | 12.270 → 5.079 | 14.623 → 5.596 | -58.6% |
| 01 裁剪开启，581 Draw | 9.100 → 4.593 | 10.194 → 4.841 | 11.529 → 5.475 | -52.5% |
| 02 高模，225 实例 | 1.135 → 0.610 | 1.500 → 0.713 | 1.624 → 0.768 | -52.5% |
| 02 低模，225 实例 | 1.138 → 0.610 | 1.503 → 0.715 | 1.610 → 0.758 | -52.5% |
| 03 400 独立对象 | 1.952 → 1.001 | 2.581 → 1.163 | 2.799 → 1.294 | -54.9% |
| 03 25 合并空间块 | 0.151 → 0.084 | 0.206 → 0.110 | 0.230 → 0.124 | -46.5% |
| 04 每帧动画评估 | 2.341 → 1.288 | 3.075 → 1.478 | 3.426 → 1.653 | -51.9% |
| 04 15 Hz 动画评估 | 2.327 → 1.237 | 3.030 → 1.419 | 3.449 → 1.636 | -53.2% |
| 05 4096² 阴影 | 1.094 → 0.537 | 1.466 → 0.674 | 1.595 → 0.727 | -54.0% |
| 05 1024² 阴影 | 1.078 → 0.539 | 1.448 → 0.677 | 1.633 → 0.744 | -53.3% |
| 06 400 独立材质 | 2.175 → 2.248 | 2.820 → 2.891 | 3.039 → 3.257 | +2.5% |
| 06 共享材质 | 2.001 → 1.003 | 2.628 → 1.166 | 2.915 → 1.281 | -55.7% |

独立材质组没有复用收益，前端增加约 0.071 ms，p95 也上升；不能声称所有场景均加速。
后续应继续控制源材质计数/索引开销。共享材质组的改进同时体现在三轮样本与 p95，
不是靠减少对象、Draw、动画评估或降低效果质量获得。

初始原实现测得 1600 对象 Extract p50 约 11.008 ms，随后原实现确认组为 9.105 ms，
说明绝对值有运行间漂移；表中采用后者，收益不以单轮最慢值计算。完整逐轮 p50、均值、
p95、机器参数、库/可执行文件哈希与像素哈希保存在 JSON/CSV 中。
早期候选出现的两个动画像素差异未作为验收结果；最终实现与两份原实现读回全部精确一致，
比较器不接受容差。

## 验证与证据

- Debug/Release 原生 OvCore、OvEditor、OvGame 构建通过。
- `--frontend`：同输入指针共享、提取边界重建、阴影/环境采样资源保留、一次性属性消费；
  八个冷进程的串行/Worker、干净/污染绑定、无 trace/分阶段 trace 图像与 Draw 数据精确一致。
- `--bindings`：同材质静态/骨骼 program 家族分离；显式绑定、两 Worker 重叠、乱序完成、
  失败/取消/部分提交、源资源销毁及恢复。此入口走 StageValidation，不替代 `--frontend`。
- `--auxiliary`：14 帧动画主视图、阴影与渐进反射捕获，各 cube mip、失败与历史恢复精确对照。
- `--framegraph-editor --framegraph-baseline`：实际 Main/Render owner 的 Editor/Game/Scene、
  拾取、预览、27 个 UI 面板、CPU/内存/导出工具和线程边界通过；原 F0 命令帧及 Game RGBA 基线未更改。

原始证据目录位于 `Build/PerformanceInvestigation/2026-10-06/`：

- `framegraph-mainline-baseline/`：初始原实现基线。
- `framegraph-mainline-confirmation/`：还原提交 `9bb01a23` 的提取源码，重新构建并测量的原实现确认组。
- `shared-material-validated/`：最终源码测量；`comparison.json` 验证 12 场景精确像素、计数、参数、资产及时间对比。
- `shared-material-candidate/`、`shared-material-final/`：排查过程记录，未作为最终验收。

构建和大体积捕获文件留在 Build，不提交项目副本。`Examples/PerformanceLab` 是本机已有、
未纳入本次提交的测试包；新入口需要该包的 run.mjs、measure.cpp 和 Project/Assets，缺失时明确报错。

复现前先构建目标 Release 库；从仓库根目录用以下入口测量新的独立输出目录，
再分析和比较。这里的 baseline/candidate 代表各自代码版本下独立产生的捕获。

~~~bat
node Tests/RenderingCommands/run-performance-lab.mjs --output Build/PerformanceInvestigation/candidate
node Examples/PerformanceLab/analyze.mjs Build/PerformanceInvestigation/candidate
node Tests/RenderingCommands/compare-performance-lab.mjs Build/PerformanceInvestigation/baseline Build/PerformanceInvestigation/candidate
node Tests/Skinning/run-validation.mjs Debug --frontend
node Tests/Skinning/run-validation.mjs Debug --bindings
node Tests/Skinning/run-validation.mjs Debug --auxiliary
node Tests/Skinning/run-validation.mjs Debug --framegraph-editor --framegraph-baseline
~~~

Release 正确性验证使用同样入口；比较器会拒绝未完成采样、机器/参数/资产变化、
计数变化及任何像素差异。性能百分比是观察值，不设自动通过阈值。

## 下一步与还差什么

1. **提前裁剪不可见对象的昂贵准备**。当前开启视锥裁剪后 Extract 仍约 4.6 ms，
   与未裁剪相近，说明仍为视图外对象捕获材质/几何/骨骼信息。先分解轻量边界判断与
   昂贵准备，保持阴影/探针/拾取视图各自可见性及一次性属性顺序；不要直接删除帧共享对象基座。
2. **产品整帧验证与重复提交成本**。测真实 Editor/Game 的 CPU/GPU 分阶段、排队和帧间
   抖动，再决定批量上传/自动实例化。现有静态合并示例是离线资产对比，还没有自动实例化系统。
3. **独立材质与探针多视图成本**。优化无复用组的计数/索引开销，测多探针下材质 key
   命中与资源依赖开销；本轮未实现跨帧缓存、材质 revision 失效协议或资源加载优化。
4. **GPU 效果与动画预算**。按实际 Pass 定位高模、阴影和后处理成本；15 Hz 动画只降低
   Update 预算，动作连续性与远近分级仍未完善。没有新增自动 LOD/遮挡剔除/动态分辨率。

Frame Graph 的通用 Buffer 图、Pass 自动裁剪/重排和多后端 barrier/异步队列属于独立扩展，
仍按 Frame Graph 交接档案列为未实现；本轮没有把它们算进已完成项。
