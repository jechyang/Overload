# FrameGraph F6：统一产品图与最终验收

2026-10-06；基于 F5 `78c6d9f0`。本轮轻量 FrameGraph F0–F6 开发与验证完成。
环境：Windows x64、i5-12490F（12 逻辑线程）、RTX 3070 Ti / NVIDIA 616.92 / OpenGL 4.5，
MSVC 14.42.34433、SDK 10.0.22621.0。JobSystem 默认 11 个 Worker；正确性矩阵另测串行、
单 Worker 和双 Worker。证据位于主线 Build/FrameGraphValidation/F6，没有另外创建工程副本。

## 最终实现

Editor 和 Game 均通过一个 RenderPlan 声明场景、后处理、工具、Picking/Readback、
UI 及 Present，随后统一编译、实现物理映射、准备、录制和 GPU 退役。即时和提交模式
共享相同产品路径；parallel_recording 在有效执行模式中控制同一 CPU 准备/录制实现。

F6 删除 ExternalComposition/trailing、SceneFrame 的尾插接口和无调用的旧 Editor/Game
渲染入口。提取及片段组合不再反复编译，生产 PrepareFrame 统一编译一次；即使零视图
也不能重复准备。通用手写 ComposeFrame 保留契约检查。材质采样声明按快照身份去重，
修正 Picking 独立入口的统计汇总，检查管线示例通过 AddView 声明真实附件和采样。

临时目标按完全兼容描述复用，并保留到 GPU fence 退役；同帧跨视口可别名，生命周期
重叠、外部 UI/材质 pin、格式/尺寸/mip/采样不符时隔离。曝光、反射历史和最终输出仍由
原 owner 持有。Main 池预留一套，RenderThread 预留两套；默认闲置预算 256 MiB，
在途租约不会被闲置淘汰。resize 后当前图所需的备用目标更新 LRU 时间，避免重复淘汰。
超过预留需求的缓存仍可淘汰，预算不足时允许重新分配。

ImGui 的原生/拥有型回放共享设备程序代际；切换只重新建立字体纹理 owner。
已发布字体与暖池映射只做 CPU 查询；新字形、尺寸或效果首次启用仍可能等待 GL owner。
本轮不改变后处理算法、颜色格式、采样方式、固定 GPU 顺序或输出像素容差。

## 构建与正确性

Debug/Release 原生 baregl、ImGui、OvReflection、OvMaths、OvPhysics、OvRendering、
OvUI、OvCore、OvEditor、OvGame 构建通过。缓存修复后重新构建 OvRendering 并链接
Editor/Game。独立 MSBuild 禁用后处理发布事件，测试运行器将当前 Resources/Engine、
Resources/Editor 的差异文件同步到配置运行目录，避免使用旧 shader。

| 本次入口 | Debug | Release |
|---|---|---|
| --framegraph、--pipeline、--auxiliary、--postprocess | 通过 | 通过 |
| --command-list、--parallel-recording | 通过 | 通过 |
| --resource-executor、--resource-lifetime、--render-frame | 通过 | 通过 |
| --target-state、--imgui-lifetime | 通过 | 通过 |
| --editor --command-list --editor-ui --product-loop --framegraph-baseline | 通过 | 通过 |
| --editor-stress --framegraph-baseline、--editor-camera、--game-thread | 通过 | 通过 |

共 30 个配置/入口结果，精简 result.json 和运行日志汇总为 F6/suite-final.json。
缓存修复后 Debug/Release 的 --framegraph、Editor 压力及 Game 线程再次通过；
最终四次产品复测见 suite-final-pool-policy.json，运行日志已纳入精简证据。
失败注入产生的 ERROR 日志是检查的一部分，以实际测试断言和最终退出码为准。

关键检查包括：

- 资源句柄作用域/代际、子资源、内容版本、RAW/WAR/WAW、Load/Clear/Discard、反馈及
  初始化提升拒绝；实际 GL 别名与禁用复用逐字节一致。16 效果组合 × 1×1/薄/奇数/正常
  尺寸，完整 luminance mip、Bloom Load 混合、跨帧曝光历史和暖池零分配。
- 两帧在途隔离、外部上一代纹理重叠 pin、GPU timeout、取消、编码/执行/fence/wait
  失败、分配回滚、idle trim、域关闭及重建。新增预算足够时 resize 后连续八次不新增
  物理分配或 owner dispatch 的回归。
- 实际 Editor/Game 产品图矩阵：Main 即时、Main 提交、RenderThread 提交，分别采用
  串行/单 Worker/双 Worker，场景、各视口和 UI 图像精确一致。非法 RenderThread+
  即时组合仍拒绝。Prepare 和 Record 实际在不同无 GL context 的 Worker 上重叠。
- Editor 压力测试 96 个完整 Canvas 帧、95 次面板访问、12 次场景/窗口/视口变化，
  Play/Pause/Step/Resume、真实 Gizmo 在途拖动、Picking/材质拖放、6 次最小化恢复、
  4 次重启和呈现失败。Game 32 组 Main/Render 姿态/场景/UI 精确对照、15 次移动场景
  过渡、6 次最小化恢复、4 次重启、容量两帧的背压和受阻 owner 下的暖态 CPU 进度。
- 完整 UI 及真实面板、Item Picker、已销毁 Texture Debugger 共 26 组原生/拥有型精确
  对照；切换不重复创建 GL 程序，纹理生命周期及关闭面板释放检查通过。

F0 冻结数据保持相等：editor-command-frames.bin 为 460800 字节、SHA256
`d9ee1593edaed4bbd991d6341593c79ca5b1a7989a1a74e6090f8b46762ae25a`；
editor-game.rgba 为 2449496 字节（1162×527）、SHA256
`cf96081556131b8af84d568b775dee4278e1310e10efa6a317f86904db9fe8ef`。
旧 A5 清单未更新，F0 之前已存在的 Editor 布局差异仍记录在 result.json；其他适用
A5 文件继续精确比较。旧历史测试记录不替代本次执行结果。

## 验证中修正的问题

1. 默认窗口四倍 MSAA 的 UI 边缘读回存在单像素不稳定：相同原生 DrawData 连续重放
   自身也出现差异，顶点/索引/布局及 GL 状态检查相同。完整/单面板原生与拥有型 UI
   现改在明确的无 MSAA RGBA8 全尺寸目标上比较，并先检查原生重放自身精确稳定。
   没有增加像素容差；实际产品窗口仍由 EditorFrame/线程/Game 测试比较。
2. 单独构建 Release 没有发布四份当前 shader，旧 Billboard 尺寸逻辑覆盖 Picking
   图像。运行器同步源资源后，保留原图像基线及 Picking 断言，Release 通过。
3. Game 的零 delta 配置矩阵改变后续 FPS 数字，ImGui 1.92 按需生成新字形，需要合法
   owner 上传。受阻 owner 的进度检查先在实际 FPS 控件预热全部数字，并检查池分配
   不变，再执行原两次 Update/背压测试；没有把冷字体上传宣称为无等待。
4. 测量夹具曾把空环境变量当作开启，修正为仅接受 `1`，mode 文件记录真实参数。
   随后发现 resize 暖池预留不断被淘汰；修正 LRU 并增加真实 GL 回归，保留零分配断言。

失败尝试的 suite ledger 保留在 F6；最终汇总只使用成功执行及当前结果清单。

## 内存和性能测量

Debug/Release × 单链/三链共享输出/三链独立输出 × 一套/两套预留 × 三次独立进程，
共 36 次成功测量。每次七组时间各 6 个样本，首样本预热，合计 1260 个暖态样本均
零新增临时目标。各模式的纹理字节在三次重复间完全一致；进程内存和时间存在波动。
原始 memory.csv、textures.csv、pool.csv、pool-mode.txt、product-timings.csv 及汇总
memory-summary.json 保存在 F6/measurements，未复制可执行文件或 PDB。

这项测试隔离生产后处理链，不含场景网格、Picking、阴影、深度 renderbuffer。
同机 GL 查询每个去重纹理的实际 mip 尺寸和通道位宽；未计驱动对齐、其他 GPU 对象，
所以以下是纹理存储，不是全部 VRAM。三条链按生产顺序在一个产品图中执行。
共享单输出与 F0 输入一致；独立输出额外计入三个 1920×1080 RGBA32F 持久视口目标。

| 模式 | 全关 1080p MiB | 全开 1080p MiB | 两帧在途 MiB | resize 两帧峰值 MiB | 720p 暖态 MiB | 恢复 1080p MiB |
|---|---:|---:|---:|---:|---:|---:|
| 单链/三链共享输出，预留 1 | 63.3 | 158.4 | 285.2 | 346.2 | 251.3 | 268.8 |
| 单链/三链共享输出，预留 2 | 94.9 | 279.9 | 285.2 | 393.0 | 266.5 | 286.4 |
| 三链独立输出，预留 1 | 126.6 | 221.7 | 348.5 | 437.6 | 279.4 | 332.1 |
| 三链独立输出，预留 2 | 158.2 | 343.2 | 348.5 | 484.5 | 294.6 | 349.7 |

单链与三链共享输出的纹理占用相同，说明同帧不同链真实复用 scratch；CPU/GPU 工作
仍随链数增加。上述全开阶段包括曝光、Bloom、Tonemapping、FXAA；曝光/Bloom 已有
相同的最大兼容目标集合，后两项没有增加物理存储。

与 F0 相同的三链/单输出/曝光+Bloom/单套口径：412.0 → 158.4 MiB，减少约 61.5%。
不能把该比例套用到三份独立输出或两帧在途。预留两套的相同隔离口径为 279.9 MiB，
两套都消费后 285.2 MiB；冷备用亮度纹理的 mip 在首次实际使用后才全部创建，
描述估算提前计算完整 mip，因此实际查询和估算会暂时相差约 5.3 MiB。

全部效果开启时，单套 scratch 估算闲置 126.8 MiB，两套 253.6 MiB。三链全开逻辑
请求总和 665.2 MiB，编译存活峰值 94.9 MiB；物理槽因描述兼容限制及缓存，不能直接
等同逻辑峰值。全部 36 次检查 idle ≤ 256 MiB；在途旧/新尺寸和持久目标另计，所以
resize 总量可以超过闲置预算。即使预留一套，手动保留两个提交帧也必须分配第二套。

再关闭效果不会立即清空闲置池，纹理仍与全开阶段相同。显式 trim 到零后仅剩持久
输出/曝光历史：单输出 31.6 MiB、三输出 94.9 MiB；最后全部销毁，纹理归零，池租用
归零，累计分配全部淘汰。没有发现另一份运行时 FrameGraph 常驻；已定位的增长是
旧每视口私有后处理缓存，以及现在明确统计的预留、闲置缓存和在途旧代际。

Windows Private Bytes 不能替代纹理字节。Release 三独立输出重复三次，全开阶段：
单套中位 719.24 MiB（719.21–719.32），两套 888.52 MiB（888.27–888.54）；销毁后
仍分别为 578.41 MiB（576.84–579.02）和 595.26 MiB（594.01–595.74），纹理却已为零。
这些包含驱动/CRT 缓存，不能把未回落的进程字节直接称作引擎泄漏。

### 暖态 CPU 成本

下表全开 1080p，各格为中位微秒；每格来自 3 进程 × 5 暖帧。时间顺序为
Compile / Realize / Encode+Execute。未包含片段 Build 和场景 Extract；最后一项包含
命令执行与 GPU fence 退役等待，不是纯 GPU 时间。

| 管线/输出/预留 | Debug μs | Release μs |
|---|---|---|
| 单链共享，1 | 406.7 / 76.9 / 1570.1 | 36.1 / 8.9 / 1281.2 |
| 单链共享，2 | 406.9 / 120.3 / 1601.9 | 35.8 / 12.1 / 1275.7 |
| 三链共享，1 | 1207.2 / 115.8 / 4238.7 | 91.5 / 13.8 / 3638.9 |
| 三链共享，2 | 1201.4 / 166.8 / 4247.9 | 93.5 / 19.2 / 3653.8 |
| 三链独立，1 | 1225.2 / 119.3 / 4207.0 | 94.6 / 13.6 / 3570.6 |
| 三链独立，2 | 1222.2 / 170.0 / 4173.8 | 90.2 / 19.3 / 3587.0 |

例如 Release 三链独立/双套的 Compile 范围 84.9–99.8 μs，Realize 17.5–20.5 μs，
Encode+Execute 3546.4–3876.3 μs。完整范围和其他阶段在汇总 JSON 中；图编译有真实
CPU 成本，本次没有冻结同口径 F0 CPU 时间，不能宣称 FrameGraph 本身提升帧率。

### 实际编辑器时间

Release `--recording-benchmark --framegraph-baseline`：同一 crowd 场景、960×320 窗口、
三面板各 320×260、VSync 关闭、固定 RenderThread，两帧容量。串行/11 Worker 交替
运行三次，每次 192 帧丢弃前 32 帧，共 960 暖帧；每个模式 480 样本。
实际 GL_TIMESTAMP 测量从执行开始到 Present 前的 GPU 时间；CPU 与完成时间分别
取产品真实边界。数据及结果在 F6/measurements/recording-benchmark。

| 指标 ms（中位 / P95） | 串行录制 | 并行录制 |
|---|---:|---:|
| Main PreUpdate+Update | 5.046 / 7.139 | 5.139 / 7.041 |
| Main 完整生产帧，含 PostUpdate/背压 | 10.053 / 15.916 | 10.226 / 15.929 |
| GL owner 执行到 Present 前 | 3.137 / 4.797 | 3.571 / 5.114 |
| GPU 到 Present 前 | 8.214 / 8.532 | 8.320 / 8.625 |
| 从 Main 起点到完成被观测 | 30.727 / 32.036 | 30.854 / 32.142 |
| 容量等待 | 4.833 / 10.831 | 4.886 / 10.885 |

三个独立运行的 Main Update 中位分别为串行 5.049/5.000/5.116 ms、并行
5.148/5.107/5.199 ms；GPU 中位为串行 8.228/8.205/8.209 ms、并行
8.325/8.320/8.319 ms。本样本并行没有证实帧时间提升，且 owner 执行增加约
0.434 ms。源码检查发现 RecordRange 每块独立建立 LightSSBO 内容，开启 Worker
会增加 range、buffer Allocate/Upload 和命令块；这是额外执行成本的候选原因，
不能仅凭这些总时间断言它解释全部差额。

当前保留每块独立绑定的正确性和通用 range 回放契约；没有调整默认配置或用缩短
工作负载换取结果。下一轮可单独测量共享 View 绑定上传与小任务分块门槛，完成
绑定污染/重放回归后再优化。这份结果比较当前串行/并行，不是与 F0 的 CPU/GPU
严格前后对比；2026-09 的旧记录包含其他 UI/反射变化，不能用来声称本轮更快。

### 复现

按测试 README 构建对应配置，从仓库根目录运行。例如：

```powershell
$env:OV_FRAMEGRAPH_MEMORY_PRODUCT='1'
$env:OV_FRAMEGRAPH_MEMORY_SINGLE=''
$env:OV_FRAMEGRAPH_MEMORY_DISTINCT_OUTPUTS='1'
$env:OV_FRAMEGRAPH_MEMORY_RESERVE2='1'
node Tests/Skinning/run-validation.mjs Release --framegraph-memory
node Tests/Skinning/run-validation.mjs Release --recording-benchmark --framegraph-baseline
```

将 SINGLE 设 `1` 为单链，DISTINCT_OUTPUTS 设空为共享输出，RESERVE2 设空为单套；
切换 Debug/Release，每组独立运行三次并保存 CSV。仅 `1` 启用开关，mode 文件应与
预期参数一致。性能夹具插桩在 Build 下当前源码副本中，生产渲染实现仍相同。

## 完成范围与后续

本轮是固定顺序的轻量 FrameGraph：有明确资源声明、契约编译、生命周期分析、物理
别名和池复用、统一产品组合、GPU 退役及诊断转储。没有自动裁剪、Pass 重排、Vulkan
barrier、多队列调度、脚本 SRP SDK 或新的渲染效果。

Frame Info 在 UI 捕获时显示上一份已准备的视口统计；当前产品准备完成后发布新值，
下次 UI 捕获可见。它不是实时显示当前 GPU 消费位置的调试器。

下一轮建议把现有图和池转储接入编辑器诊断面板：查看 Pass 读写、资源存活区间、
物理共享及缓存/在途占用，并提供预算调整和明确的闲置 trim 操作。性能优化应先根据
本轮 CPU/GPU 时间继续定位，再决定是否缓存稳定图结构；不能仅凭图像一致宣称更快。

架构与复现入口见 [当前架构](RENDERING_ARCHITECTURE.md)、
[图解](RENDERING_ARCHITECTURE_DIAGRAM.zh-CN.md)、
[测试 README](Tests/RenderingCommands/README.md)。F0–F5 历史验收文档保留。
