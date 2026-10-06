# Frame Graph 诊断面板验收（2026-10-06）

本轮在 main 上增加诊断面板，建立在已完成的 F0–F6 产品资源图之上。入口为编辑器
`Window > Frame Graph`，默认关闭。

## 使用

- Dependencies：默认以独立 Pass 节点和资源版本节点展示数据流；蓝色箭头表示资源读取，
  橙色箭头表示写入，紫色写入箭头表示附件 Load。资源节点标注版本、格式、尺寸和物理槽，
  悬浮可看来源与存活区间。选择资源可查看访问 Pass，点击资源节点可聚焦它的读写方。
  勾选/取消 Resource flow 可在资源流图和 Pass 依赖图间切换。Pass 依赖图默认聚焦
  依赖最多的 Pass，也可显示全图；两种图都支持名称筛选、Fit、滚轮缩放和中键平移。
  Versions 开关可切回每个资源一个节点的总览。点击版本节点选择对应内容状态，
  下方显示该版本的生产/消费 Pass；悬浮链路显示输入/输出版本和 mip/layer 范围。
  布局依据画布宽高在每侧 1–3 列中选择节点最大的方案；长标题限制在节点内，完整
  名称可悬浮查看，下方关联按钮按可用宽度换行。
  有成功计时样本后可开启 GPU heat：自动冻结实际测量的计划并聚焦最慢 Pass。
  Pass 填充由蓝色到橙红色表示相对耗时，按整份测量计划的最大值归一化，筛选和
  聚焦不改变颜色尺度；节点显示毫秒和总和占比，选中边框与资源/依赖边颜色保留。
  悬浮或选择节点可查看测量帧与耗时，GPU hotspots 返回该样本的列表。
  取消 Freeze plan 恢复实时计划并移除热力着色；没有对应样本的计划保持普通颜色。
- Passes：按产品计划顺序列出 Pass，查看资源访问、mip/layer 范围、附件
  Load/Store、传入和传出的 RAW/WAR/WAW/显式顺序依赖；点击资源或依赖跳转。
- Resources：查看来源、尺寸、格式、估算字节、最终内容版本、存活区间，以及
  同一物理 scratch slot 的逻辑资源；访问列表也标注读/写版本，点击关联 Pass 跳转。
  Show version flow 直接聚焦该资源的版本流。
- Lifetimes：按 Pass 顺序显示资源的首次/末次使用。默认 Physical slots 把复用同一
  物理 scratch 槽的逻辑纹理放在一行，非重叠区间显示为独立条；取消后每个逻辑资源
  一行。Transient only 可切换到外部/历史/输出资源，它们始终单独列出，不与 scratch
  合并。资源条保持原字号，Zoom 与水平滚动可放大和平移时间轴，左侧行名固定，
  行列表支持垂直滚动；Fit 恢复默认比例，未使用资源明确标记未分配。
  上方曲线只计算整份计划的逻辑 scratch 存活字节，筛选不改变曲线和峰值；点击曲线
  或调整 Pass 选择位置，Peak 定位峰值，At selected Pass 只显示该位置仍存活的资源。
  点击条选择资源，双击或 Resource details 查看资源详情；首末端点按钮和 Pass
  details 查看对应 Pass。Resources 的 Show lifetime 和 Passes 的 Show lifetimes
  提供双向入口。Export 延长到计划末尾并标注橙色端点；未使用的 scratch 不分配条。
  时间轴沿用当前准备快照与 Freeze plan，不保留额外图历史或 GPU 存储。
- Preview：在资源详情或版本流中点 Preview content，或手动选资源后点 Capture。
  在下一份结构匹配的产品计划中，于所选版本的生产 Pass 后抓取一次图像；v0 在
  首次访问前抓取。支持 2D、cube 的六个面、mip、RGB/单通道/Alpha 和深度窗口，
  HDR 可调 EV 和 Tone map。图像标题显示实际 scope 和抓取位置，修改参数后需
  再点 Capture。Texture Debugger 可打开同一份图像，Clear image 释放面板图像。
  冻结后保留已有图像，取消冻结才能发起新抓取。未初始化、Discard、越界的范围
  或变化后的计划会显示原因，不会用错误像素替换上一次成功图像。
- Timings：启用 Capture GPU timings 后每 4 份计划采样一次。显示每个 Pass
  的 GPU 毫秒、占 Pass 总和的比例和视图编号，默认按耗时降序排列；取消
  Slowest first 恢复产品计划顺序，支持名称筛选。点击行会冻结该次测量的
  计划并跳到 Pass 详情，详情显示该次耗时，可继续检查资源和依赖。
  按住鼠标期间保持列表数据稳定，防止新样本重排热点导致点击取消。
  结果标注实际测量的 frame/snapshot/scope；冻结后 resize 不改变已有结果。
  关闭采样释放查询池，已显示的值可继续查看；关闭面板释放所有诊断值。
  Show GPU heat 从列表直接定位到测量图的最慢 Pass；Pass 详情中的同名按钮
  可定位当前 Pass，在资源版本流和 Pass 依赖图之间切换仍保留该次测量。
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

预览图像单独保存在 `FrameGraphPreviewImage` 中，不加入值诊断快照。只有显式
抓取才创建转换命令；目标为保持宽高比、最大 512×512 的 RGBA8，单张最多
1 MiB。源 Snapshot、转换程序和临时 resolve 只保留到该 RenderFrame 的 GPU
退役；发布后的图像不含 mapping/lease 或源纹理。深度 renderbuffer 抓取期间
额外使用原尺寸 resolve texture，该临时空间不包含在 1 MiB 图像上限内。
关闭面板、Clear image 会释放其图像和 Frontend 预览状态；已打开的 Texture
Debugger 可独立持有同一图像，直到关闭或选择其他纹理。

UI 只在 RenderFrame 成功完成 GPU 退役后发布图像；取消或执行失败保留旧图。
抓取复用子资源编译器检查版本与初始化状态，在原图的两个 Pass 间插入诊断
命令，不改变原 Pass 顺序、资源复用或产品输出。转换恢复程序、texture/sampler/
image 绑定与活动 texture unit，保留 FBO、viewport 和像素 pack/unpack 状态。
当前不支持 backbuffer、多重采样和整数纹理预览；深度显示原始值，不做线性化。

### GPU 耗时边界

GPU 计时默认关闭，与诊断计划的采集开关独立。每个 Pass 两个 `GL_TIMESTAMP`
查询，在命令实际执行的 GL consumer 上发出；编码 Worker 和 UI 线程不查询 GL。
查询只在 RenderFrame 成功完成 GPU fence 后读取，且先检查每个结果是否可用。
不可用、查询失败、取消或帧失败时不发布部分结果，也不等待或强制完成 GPU。
`OnRetired` 诊断观察者每帧执行一次，异常不能使产品帧失败。

最多保留 3 个在途样本，每份最多 512 个 Pass；满容量或超大计划跳过采样。
查询按样本槽复用，暖机后相同规模不继续分配；最大 3072 个查询对象，停止
采样或关闭时随最后的在途使用退役。查询使用 baregl 删除队列，Worker 释放
不执行 GL，domain close 会删除剩余查询并使旧域失效；新域重新建立查询。
结果只保存测量计划的值快照、帧号和纳秒数组，不含查询、源纹理或池 lease。

毫秒数是 Pass 前后 GPU 时间戳的差，不是 CPU 编码时间或活跃 GPU 利用率。
总和只相加这些区间；实际 swap/vsync 在产品计划之外，不纳入计时。
一次性预览转换插在 Pass 区间之外，也不计入 Pass 总和。空 Pass 仍可能显示
查询开销；profiling 本身存在开销。结果与其自身的计划一起保留，不把老帧
的数组套在新的计划上；点击热点时冻结这一份实际测量的图。
热力图只使用与显示计划相同的不可变快照对象；即使新图的 Pass 数量相同，也不会
继承旧耗时。显示热力图不创建新的 GPU 查询、纹理、资源副本或采样历史。
普通图的筛选、缩放、选中边框、资源版本和读写链路保留，颜色尺度使用整图最大
Pass 耗时，不代表 GPU 利用率或 CPU 时间。全部为零时仍可查看零值，不发生除零。

### 生命周期时间轴的内存口径

Lifetimes 的横轴是固定执行顺序的 Pass 编号，不是 CPU/GPU 毫秒。首次和末次使用
均包含在区间内，Export 的末次标记可以等于 Pass 数量，画面钳制到最后一列。
整图 scratch 曲线按编译结果累加仍存活的逻辑 transient 字节，峰值与编译器一致。
物理槽按本计划的 mapping 序号分组；每槽容量与槽内多个逻辑资源的字节总和不同。
外部资源可能在计划之前/之后由 owner 保留，时间轴只显示本计划内使用，不能据此
推断其实际创建/销毁或全部显存占用。条结束也不表示池已释放该槽，实际回收仍由
GPU 退役、外部 pin、帧预留、闲置预算和过期策略决定。

当前页只计算一份值快照的曲线和可见条矩形；筛选、缩放、冻结和关闭都不发出 GL
调用，不添加 texture、lease、native query 或新的每帧历史队列。关闭释放这些 UI 值。

打开面板才在 PrepareFrame 末尾复制诊断值，冻结后停止采集；关闭时释放面板值，
在同一次产品录制中关闭 Frontend 采集并释放其值。关闭状态不查询池统计，不复制
计划；没有每帧诊断历史队列。

### 内容版本语义

版本表示同一个逻辑资源在每个写入 Pass 后的内容状态，一帧计划内从 0 开始；
v0 的读取来自已初始化的导入/History 范围，未使用的资源节点也可显示 v0，但并不
表示其内容已经初始化。Clear/DontCare 覆盖只有输出版本，Load 有输入和输出版本。
同一 Pass 写多个不相交 cube 面或 mip/layer，只增加一次版本；mip 生成读取之前
的 mip0 状态，再生成一个包含 mip 链的新状态。

版本是整个资源的计数，具体范围仍由每条 access 记录；局部写入后的状态包含未被
覆盖的其他范围。Discard 的写入也增加版本，但对应范围不可继续读取，图的悬浮详情
标记 discarded range。实际初始化/丢弃和 RAW/WAR/WAW 校验仍由子资源编译器执行。
捕获和编译共用读写分类和版本推进规则，并检查最终版本一致；片段合并后按产品顺序
计算，而不是各片段从 0 重新开始。版本不同于 ResourceHandle 的 generation。

多个版本节点使用同一个物理槽，并不自动保留旧像素或分配额外 GPU texture。
显式预览是在下一份匹配计划执行到对应内容状态时抓取，并非读取已结束帧的
历史纹理。版本快照只增加每个 access 的两个可选整数值，仍只在诊断采集开启
时生成；冻结、关闭和线程边界不变。

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

0. 计时查询在 GPU 退役前不读取，availability 为 false 时零 result 读取；
   精确时间戳 oracle 检查 Pass 顺序、帧/图对应及排除预览转换。暖机复用、
   3 样本/512 Pass 上限、取消/失败不发布、查询异常不影响产品帧、观察者只
   执行一次、冻结值由无 GL Worker 读取、域重建、Worker 删除和产品像素不变。
   Main/Render 两种编辑器模式实际操作采样开关、点击最慢行冻结测量图、
   resize、切回计划顺序、关闭采样和面板；暖机不分配，F0 图像基线不变。
   GPU heat 验证两种实际画布的原生窗口像素：最慢 Pass 着暖色、停用后恢复原色，
   资源颜色不变；实际 ImGui 按钮切换与节点点击、同规模不同图拒绝旧样本、测量图
   恢复、冻结 resize、返回热点列表及取消冻结恢复实时计划，产品视口像素不变。
   生命周期验收还覆盖逐 Pass 与编译存活区间/峰值一致、实际物理槽的非重叠条、
   原生条和内存曲线点击、峰值与筛选、逻辑/外部资源、缩放平移、详情双向跳转、
   冻结 resize 与恢复，以及 Export 末尾、未使用和空图的受控边界值。
1. 预览在 alias 覆盖前得到指定 v1/v2 内容；RGB/Red/Alpha、24 个 cube
   face/mip、深度比较模式覆盖、depth/stencil renderbuffer resolve、R32F、
   HDR 控制和尺寸上限。拒绝未初始化 v0、Discard、过期选择和无效范围；
   TrimIdle 后源纹理删除，已有图像仍有效。GL 绑定与像素 pack 状态保持不变。
   Main/Render 两种编辑器模式直接触发 ImGui Capture/Clear image 按钮，验证
   完成后发布、冻结/resize、取消/失败保留旧图、Texture Debugger 跳转和独立
   关闭、关闭面板释放状态；抓取前后三个产品视口图像逐字节一致。
2. 实际 GL aliasing 的值快照：逻辑/峰值/物理字节、存活区间、访问和依赖、
   未命名导入资源的用途名称，拒绝 foreign compiled scope 和不一致的最终版本。
   版本覆盖 Clear/Load、导入初始 v0、cube 不相交写入、mip 生成、局部保留、Discard、
   Export；捕获前后池分配数和存储字节不变。
3. 留存冻结诊断值，同时释放 mapping、执行 TrimIdle，弱 texture owner 失效，
   原 GL texture 不存在；冻结值仍可由无 GL Worker 读取。
4. TrimIdle 删除两帧预留的闲置 spare，同时保留 active lease；外部 UI pin 也
   保持有效；预算和帧预留不变。
5. 原池的 GPU timeout/退役、取消、编码/执行/fence/wait 失败、回滚、关闭和
   context domain 重建继续检查；四视口共享和禁用复用的图像逐字节一致；
   16 种后处理组合 × 4 种尺寸，暖池零分配。
6. 4 种自定义管线配置 × 串行/单 Worker/双 Worker，双目标精确图像及
   7 种无效计划/owner 拒绝。
7. 真实 Editor Main/Render owner 多视口面板：默认关闭、全图/聚焦依赖、Pass/资源
   版本数据流（每条读/写边匹配访问版本和节点）、v0/Load、真实 ImGui 版本节点点击、
   非版本资源总览、筛选、空结果、
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
  的 `run.log`、`framegraph-{main,render}-{passes,resources,preview-color,timings,timings-pass,timings-order,gpu-heat-resources,gpu-heat-dependencies,lifetimes-physical,lifetimes-peak,lifetimes-logical,lifetimes-zoom}.rgba`、尺寸文件；
  生成的 PNG 用于视觉检查。
- 相应入口目录的 `result.json`，诊断面板结果含 `framegraphEditor: true`，
  `framegraphComparison.files` 记录两份 F0 检查。

面板依赖图按产品计划及其资源访问绘制；聚焦和筛选显示相邻链路，便于检查多视图
计划。以历史内存数据解释当前缓存行为时需留意它采集于过期回收实现之前；新的
自动回收效果以本轮内存测量为准。

## 当前完成度与后续范围

F0–F6 核心迁移已完成，见 FRAMEGRAPH_F6_VALIDATION.zh-CN.md。主线目前包含资源/
子资源与内容版本契约、依赖/初始化校验、固定顺序的产品组合、生命周期分析、
兼容物理复用、池预算/自动回收、GPU 退役、Editor/Game 和多线程回归，以及依赖图、
版本图、资源预览、GPU 耗时/热力图、生命周期时间轴。

按当前 OpenGL 产品需求，建议时间轴之后再做 2–3 轮有明确验收的收尾：

1. 编译/实现/准备/录制的 CPU 耗时与慢点定位，再按证据评估稳定图结构缓存。
2. 内存占用来源诊断，把当前图、持久输出/历史、闲置预留、在途旧尺寸和外部 pin
   的边界讲清楚；保留描述估算与实测显存的区别。
3. 大图操作与导出：折叠统计、扩大画布、查找和定位、导出可复核的值诊断。

自动 Pass 裁剪/重排、通用 Buffer 图、显式 API barrier、Vulkan/D3D12 与多队列/
异步计算是更大的独立扩展。它们没有包含在 F0–F6 或上述收尾估计中，是否实现
应由实际渲染需求和测量结果决定。

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
