# 自动曝光亮度缓冲验收（2026-10-06）

自动曝光的 `Exposure.Luminance` scratch 从 RGBA32F 改为 R32F。
曝光 shader 使用其红通道；保留原来的 32 位浮点精度、1024×1024 分辨率、
11 级 mip 链、LINEAR_MIPMAP_LINEAR/LINEAR 过滤及 CLAMP_TO_BORDER。
分配时外部像素格式配套使用 RED/FLOAT。

该资源仍由产品 FrameGraph 池管理，格式兼容条件、GPU lease 退役、历史提交和
取消处理走现有路径。曝光历史与输出继续使用原格式。无需新增项目设置。

## 格式与数值对照

`Tests/RenderingCommands/luminance_validation.inl` 纳入 `--framegraph` 验证。
每组先 Build 一次实际 PostProcessPipeline，冻结 Pass 操作、材质 uniform、
源图像和 ping-pong 历史引用；复制计划，只把参考版亮度描述符恢复为
RGBA32F/RGBA。两次执行读取相同的上一帧历史，写入相同的当前帧历史。
因此渐进曝光的墙钟 elapsedTime 对两次执行相同。

Debug、Release 各 80 组：4 种源尺寸（1×1、1×32、63×17、96×64）×
5 种输入（黑、近黑、梯度、HDR、不同 RGB 比例）× 4 帧。
覆盖首次初始化、渐进适应、亮度变暗/变亮、中心权重 0/0.5/1、曝光补偿、
不同 speed-up/speed-down。命令在双 Worker 编码，经真实 RenderFrame 提交和
GPU 退役。

每组逐位检查：

- 全部 11 级 mip 的红通道浮点值一致。
- 两个曝光历史纹理的全部 RGBA 浮点值一致。
- 最终 HDR 输出的全部 RGBA 浮点值一致，且无非有限数。
- 查询每级实际 GL 内部格式和通道位数：R32F 的 R 为 32 位，G/B/A 为 0；
  参考 RGBA32F 四通道均为 32 位。
- GL 查询的字节总和与描述符估算一致，GPU 退役后不保留活动 lease。

完整亮度 mip 链的实际分配：

| 格式 | 字节 | MiB |
| --- | ---: | ---: |
| RGBA32F | 22,369,616 | 21.33333 |
| R32F | 5,592,404 | 5.33333 |
| 减少 | 16,777,212 | 16.00000（75%） |

这是单条亮度链的收益。多个视口在一个产品图中可共享物理 scratch，整帧收益需
按实际物理存储计数。

## 验证命令与证据

构建 Debug、Release 的 OvCore、OvGame、OvEditor 后执行：

```powershell
node Tests/Skinning/run-validation.mjs Debug --framegraph
node Tests/Skinning/run-validation.mjs Debug --postprocess
node Tests/Skinning/run-validation.mjs Debug --framegraph-editor --framegraph-baseline
# Release 使用相同三个入口。
```

同时覆盖原有的 16 后处理组合 × 4 尺寸、四视口共享与禁用复用逐字节一致、
暖池零分配、历史/取消/失败、串行/单 Worker/双 Worker 主图/阴影/cube mip
一致，以及实际 Main/Render owner 编辑器产品图和诊断面板。

测试不更新 F0/A5 图像基线；Editor 的两份冻结图像继续检查大小和 SHA-256。
Frame Graph 面板中 `Exposure.Luminance` 显示 R32F 和约 5.33 MiB。

证据目录（构建输出，Git 忽略）：

- `Build/JobsValidation/Skinning/{Debug,Release}-framegraph-command-list/current/`
  的 `luminance-formats.csv`、`run.log`。
- `Build/JobsValidation/Skinning/{Debug,Release}-postprocess-command-list/` 的
  三种模式捕获和 `result.json`。
- `Build/JobsValidation/Skinning/{Debug,Release}-framegraph-editor-command-list-editor-ui/`
  的 `result.json` 和窗口捕获。

多视口内存测量使用同机、独立进程、三个独立 1080p 输出、一个产品图与共享池、
256 MiB 闲置预算，分别预留 1/2 套。改动前基于 cc431ab1 的原生库采样，再使用
本轮原生库采样；保留前后 CSV 于
`Build/FrameGraphValidation/Luminance/{Before,After}/reserve{1,2}/`。

```powershell
$env:OV_FRAMEGRAPH_MEMORY_PRODUCT='1'
$env:OV_FRAMEGRAPH_MEMORY_DISTINCT_OUTPUTS='1'
$env:OV_FRAMEGRAPH_MEMORY_RESERVE2='0' # 第二组设为 1
node Tests/Skinning/run-validation.mjs Release --framegraph-memory
```

纹理字节通过实际 GL mip 尺寸和通道位数相加得到，进程 private bytes 另行记录。
池预留的可变纹理在首次使用前只有 base level，实际字节与完整 mip 描述符估算
可不同。缓存预算和 resize 淘汰也会影响整帧总量，应按阶段比较。

## 三视口实测结果

下表为全部追踪纹理的实际分配总和，单位 MiB。

| 预留套数 | 阶段 | 改动前 | 改动后 | 减少 |
| ---: | --- | ---: | ---: | ---: |
| 1 | 1080p 全效果预热 | 221.72 | 205.72 | 16.00 |
| 2 | 1080p 全效果预热 | 343.18 | 315.18 | 28.00 |
| 1 | 两帧在途 | 348.52 | 316.52 | 32.00 |
| 2 | 两帧在途 | 348.52 | 316.52 | 32.00 |
| 1 | resize 两帧在途 | 437.58 | 405.58 | 32.00 |
| 2 | resize 两帧在途 | 484.45 | 452.45 | 32.00 |
| 1 | 720p 后恢复 1080p | 332.11 | 331.75 | 0.36 |
| 2 | 720p 后恢复 1080p | 349.69 | 349.33 | 0.36 |

每个进程的七阶段各有 6 个样本，预热后的 **35 个样本零新增物理分配**；
前后四个进程共 140 个暖池样本。预算均未超出 256 MiB，退役后的 leased 为 0，
清理闲置后均回到约 94.92 MiB 持久纹理，释放全部管线后纹理计数/字节为 0。

恢复尺寸后的总量减少较小，是因为释放出的预算可保留更多旧尺寸闲置目标；
此时改动后的 idle 估算分别为 **236.83 / 254.41 MiB**。因此该修改减少每条
亮度链和在途分配，保留闲置缓存的总量仍受现有预算/淘汰策略影响。进程 private
bytes 同时受到驱动和缓存影响，此次不据此宣称固定比例的整进程内存下降。

后续优先优化旧尺寸和停用效果的闲置缓存回收，可进一步降低 resize 后的留存。

验收设备为 NVIDIA GeForce RTX 3070 Ti，OpenGL 4.5 / NVIDIA 616.92。
构建、数值、图像、暖池和生命周期检查全部通过；本轮未重写冻结图像基线。
