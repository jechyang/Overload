# FrameGraph F5：产品帧统一组合

2026-10-05；基于 F4 提交 29f05a0e。Debug/x64，MSVC 14.42.34433、SDK
10.0.22621.0，RTX 3070 Ti / NVIDIA 616.92 / OpenGL 4.5。

Editor 先在 Main 提取 Asset → Game → Scene → Camera Preview，再把工具、
Picking、读回、ImGui 和 Window.Present 作为有访问声明的节点纳入同一 RenderPlan。
Game 的场景、后处理、最终 blit、调试 UI 和 Present 同样使用一个计划和目标池。
即时和提交模式共享这条链；即时模式也以 GPU fence 退役临时资源。

片段保留作用域及代际映射，归一化相同存储的导入，合并场景访问身份，并偏移
内容版本与固定顺序依赖。后加入的片段不能提升早先资源的初始化状态。
材质、Canvas/Overlay UI、工具及 ImGui 声明实际采样资源。曝光/反射历史和
视口输出仍由原 owner 持有。Texture Debugger 或材质保留池中上一代资源时，
外部导入也持有可重叠的租约，直到消费者帧退役。

池命中和已发布的 UI 字体可在 CPU 查询，不再每帧等待 GL owner。仅缺失资源
批量交给 ResourceExecutor。RenderThread 产品预留两套物理资源，以覆盖其
有界在途容量；闲置预算仍限制缓存，容量/尺寸/效果改变时仍可能需要 owner 分配。
字体快照安全发布，同时保留原 ImGui 后端的 GL_RGBA 创建方式；完整 UI
检查曾定位到一个字体边缘像素差异，未放宽像素容差。

## 验证

- Debug 原生 baregl/OvRendering/OvUI/OvCore/OvEditor/OvGame 构建通过。
- `--framegraph`：四视口同/不同尺寸组合，前三条链真实共享物理 scratch；
  与禁用复用路径逐字节一致。覆盖最终 UI 读取、源 resize/销毁、过期片段句柄、
  初始化提升拒绝、版本偏移、外部池纹理 pin、两帧预留及暖池无 owner dispatch。
  F3 的取消/异常/fence/wait/分配失败/域重建检查继续通过。
- `--editor-thread --framegraph-baseline`：16 组姿态/三视口/UI 精确对照；
  真实 Picking 和材质拖放、两帧录制与 Render 阻塞、4 次应用重启和呈现失败通过；
  19 组完整/单面板 UI 对照通过，实际 Prepare/Record 在无 GL Worker 上交错。
- `--game-thread`：32 组姿态/场景/UI 精确对照；2 帧在途背压、6 次最小化恢复、
  4 次重启、呈现失败、源场景/目标销毁与产品循环通过。
- F0 的 editor-command-frames.bin 大小 460800、SHA256
  `d9ee1593edaed4bbd991d6341593c79ca5b1a7989a1a74e6090f8b46762ae25a` 保持一致。
  `--framegraph-baseline` 显式检查这份开发前冻结数据；旧 A5 清单未修改。
  不加此参数的旧 Editor 产品入口仍报告 F0 已存在的 A5 差异。

精简证据在 Build/FrameGraphValidation/F5，保留日志、结果和资源转储，不复制
测试可执行文件或 PDB。Release 全量及完整配置矩阵属于 F6。

## 内存测量

与 F0 一样，只创建三个后处理 owner 和一个 1920×1080 输出，以隔离临时缓存；
没有场景/物体副本。F5 用同一产品图按原顺序执行三条链，默认共享池的闲置预算
256 MiB。实际查询 GL 纹理的 mip 尺寸和通道位宽，未计 renderbuffer、驱动对齐
或其他对象，因此不是全部 VRAM。

| 阶段 | F0 纹理 MiB | F5 纹理 MiB |
|---|---:|---:|
| 三个管线刚创建 | 48.0 | 0.0002 |
| 所有效果关闭 | 174.6 | 63.3 |
| Bloom | 396.0 | 137.1 |
| 曝光和 Bloom | 412.0 | 158.4 |
| 再关闭效果 | 412.0 | 158.4 |
| 将池闲置预算调为零 | 不适用 | 31.6 |
| 全部销毁 | 0 | 0 |

曝光/Bloom 阶段少约 61.5% 纹理存储；闲置 scratch 126.8 MiB、在途 0，
累计物理分配 10，最终全部淘汰。每种设置预热后 5 帧均无新增临时分配。
此测试使用单套物理资源；渲染线程的两套预留另有占用，不能套用相同比例。
Private Bytes 受驱动缓存影响，销毁后未立即回落，不能用它直接判定引擎泄漏。

复现：设置 `OV_FRAMEGRAPH_MEMORY_PRODUCT=1` 后执行
`node Tests/Skinning/run-validation.mjs Debug --framegraph-memory`。
四阶段各 6 个样本的编译/资源实现/执行时间与原始字节见 product-timings.csv。
这不构成 GPU 性能提升结论。

## F6 剩余工作

移除 opaque trailing 等过渡接口和无人使用的旧产品入口；消除生产链重复编译；
构建 Debug/Release 并完成配置、线程、资源生命周期和产品压力回归；更新架构及
测试文档，复测内存/CPU 代价。Frame Info 目前显示上一份已准备视口统计，需在
最终说明中明确其更新时点。自动裁剪、重排、多队列和新渲染效果不在本轮范围。
