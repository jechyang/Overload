# Frame Graph 诊断面板验收（2026-10-06）

本轮在 main 上增加诊断面板，建立在已完成的 F0–F6 产品资源图之上。入口为编辑器
`Window > Frame Graph`，默认关闭。

## 使用

- Dependencies：默认以独立 Pass 节点和资源节点展示数据流；蓝色箭头表示资源读取，
  橙色箭头表示写入，紫色表示同一 Pass 同时读写。资源节点标注格式、尺寸和物理槽，
  悬浮可看来源与存活区间。选择资源可查看访问 Pass，点击资源节点可聚焦它的读写方。
  勾选/取消 Resource flow 可在资源流图和 Pass 依赖图间切换。Pass 依赖图默认聚焦
  依赖最多的 Pass，也可显示全图；两种图都支持名称筛选、Fit、滚轮缩放和中键平移。
- Passes：按产品计划顺序列出 Pass，查看资源访问、mip/layer 范围、附件
  Load/Store、传入和传出的 RAW/WAR/WAW/显式顺序依赖；点击资源或依赖跳转。
- Resources：查看来源、尺寸、格式、估算字节、最终内容版本、存活区间，以及
  同一物理 scratch slot 的逻辑资源；点击关联 Pass 跳转。
- 支持名称筛选和只看 transient。未命名的导入附件按首次使用的 Pass 和用途
  显示名称。筛选后详情跟随可见项；空结果不会继续显示不匹配的详情。
- Freeze plan 固定当前计划。取消冻结后恢复采集；冻结期间池计数仍保持更新。
- 显示池的闲置与 leased/pinned 字节和数量、闲置预算、帧预留、历史 leased 峰值、
  allocations/hits/aliases/evictions 和自动回收计数。
- 默认对连续 60 个成功 Realize 图计划都未再使用的资源自动回收；可关闭或重新启用。
  当前计划和帧预留、在途 lease、外部 pin 不会被自动回收。计数按成功图计划推进，
  停用效果或 resize 后，旧资源等过期再释放。
- Apply 修改当前产品池的闲置预算，范围 0–4096 MiB，0 禁用闲置缓存。
  预算是本次会话设置，不写入项目配置。
- Trim idle 释放闲置 scratch；保持预算和帧预留，保留活动帧 lease 和外部 pin。
  该操作可能使下一帧重新分配 scratch。请求在下一个产品录制边界执行。

## 数据与线程边界

面板显示最近一次 **PrepareFrame 完成的提交前计划**，并明确标注；它不代表
GPU 已完成该计划。UI 绘制在当前 PrepareFrame 之前，因此实时查看的是之前准备
完成的计划。池统计与计划快照不是同一时间采样，冻结不会冻结统计。

`FrameGraphDiagnostics` 只包含名称、标量和访问/依赖数组，不包含 texture /
framebuffer Snapshot、资源 owner、命令回调、视图或池 mapping/lease。
冻结的值可在无 GL context 的 Worker 上读取。Frontend 的采集开关和快照查询要求
其构造线程；面板更新和请求消费要求 UI 线程。

打开面板才在 PrepareFrame 末尾复制诊断值，冻结后停止采集；关闭时释放面板值，
在同一次产品录制中关闭 Frontend 采集并释放其值。关闭状态不查询池统计，不复制
计划；没有每帧诊断历史队列。

诊断与编译共用 EffectiveAccesses：传统 reads/writes 与显式访问使用同一套
子资源和附件清理语义。logical/peak/physical scratch 字节分别表示使用的 transient
逻辑资源总和、逻辑存活峰值、该计划实际占用的不同物理 slot 总和。它们是描述符
估算值，不等于进程内存或全部 VRAM，也不包括池中其他帧/历史/闲置存储的总量。

## 验证入口

先生成工程，构建 Debug、Release 的 OvRendering、OvUI、OvCore、OvGame 和
OvEditor。此轮没有更新 F0/A5 图像基线。

```powershell
node Tests/Skinning/run-validation.mjs Debug --framegraph
node Tests/Skinning/run-validation.mjs Debug --pipeline
node Tests/Skinning/run-validation.mjs Debug --framegraph-editor --framegraph-baseline
# Release 使用相同三个入口。
```

检查覆盖：

1. 实际 GL aliasing 的值快照：逻辑/峰值/物理字节、存活区间、访问和依赖、
   未命名导入资源的用途名称，拒绝 foreign compiled scope。
2. 留存冻结诊断值，同时释放 mapping、执行 TrimIdle，弱 texture owner 失效，
   原 GL texture 不存在；冻结值仍可由无 GL Worker 读取。
3. TrimIdle 删除两帧预留的闲置 spare，同时保留 active lease；外部 UI pin 也
   保持有效；预算和帧预留不变。
4. 原池的 GPU timeout/退役、取消、编码/执行/fence/wait 失败、回滚、关闭和
   context domain 重建继续检查；四视口共享和禁用复用的图像逐字节一致；
   16 种后处理组合 × 4 种尺寸，暖池零分配。
5. 4 种自定义管线配置 × 串行/单 Worker/双 Worker，双目标精确图像及
   7 种无效计划/owner 拒绝。
6. 真实 Editor Main/Render owner 多视口面板：默认关闭、全图/聚焦依赖、Pass/资源
   数据流、筛选、空结果、
   自动回收开关、
   冻结/恢复和 resize、线程归属、预算请求恰好消费一次、TrimIdle 后重建并继续
   渲染、关闭后 Frontend 和面板释放快照。
7. Editor Edit/Play/Pause/单步/恢复/Stop、实际产品帧/交互检查，27 组完整/
   单面板/Picker/Debugger 原生与拥有型 UI 精确图像；两份 F0 冻结产品图像
   大小和 SHA-256 相同。

该测试场景的产品图捕获到 **54 个 Pass、35 个资源**，覆盖 Asset/Game/Scene
视口、后处理、工具与 UI、最后的 Window.Present。截图为真实拥有型产品 UI
的窗口读回，不是绘制的示意图。手动检查 Passes 和 Resources 两页，确认列表、
详情、存活条和缓存控制显示正常。

日志和图像位于（忽略的构建输出）：

- `Build/JobsValidation/Skinning/{Debug,Release}-framegraph-command-list/current/`
- `Build/JobsValidation/Skinning/{Debug,Release}-pipeline-command-list/current/`
- `Build/JobsValidation/Skinning/{Debug,Release}-framegraph-editor-command-list-editor-ui/current/`
  的 `run.log`、`framegraph-{main,render}-{passes,resources}.rgba`、尺寸文件；
  生成的 PNG 用于视觉检查。
- 相应入口目录的 `result.json`，诊断面板结果含 `framegraphEditor: true`，
  `framegraphComparison.files` 记录两份 F0 检查。

面板依赖图按产品计划及其资源访问绘制；聚焦和筛选显示相邻链路，便于检查多视图
计划。以历史内存数据解释当前缓存行为时需留意它采集于过期回收实现之前；新的
自动回收效果以本轮内存测量为准。

## 自动回收内存实测

本次 Debug 内存实测使用单池、3 条视口后处理链、默认 256 MiB
闲置预算，先测两帧在途，再执行 720p resize 并恢复 1080p；之后各运行 60 个相同
1080p 全效果图计划及 60 个无效果计划。两个阶段暖池物理分配都保持 0。

- 恢复 1080p 后，自动回收 18 个旧目标，池闲置从约 **236.83 MiB** 降到
  **110.83 MiB**；追踪到的产品纹理从 **268.50 MiB** 降到 **142.46 MiB**。
- 关闭效果后再运行 60 个计划，累计自动回收 27 个目标，池闲置进一步降到约
  **31.64 MiB**，纹理统计降到约 **63.28 MiB**。

```powershell
$env:OV_FRAMEGRAPH_MEMORY_PRODUCT='1'
node Tests/Skinning/run-validation.mjs Debug --framegraph-memory
Remove-Item Env:OV_FRAMEGRAPH_MEMORY_PRODUCT
```

纹理统计只包含该测试中 GL 当前存活纹理，不等于显卡总 VRAM 或任意项目的完整
显存占用。过期回收按成功完成的 Realize 图计划推进；自动开关不会改变闲置预算，
预算超出时仍由原 LRU 预算路径立即回收。
