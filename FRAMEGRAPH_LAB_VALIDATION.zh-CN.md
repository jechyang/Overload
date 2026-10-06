# Frame Graph 复杂场景验收档案

首次建立：2026-10-06，基于主线 `754c59d9`。2026-10-07 继续加入 HDRI 环境和
生产提取器的局部探针完整包含/全局回退修正，便于人为观察多 Pass 和真实反射。

## 已完成

- [FrameGraphLab](Examples/FrameGraphLab/README.zh-CN.md)：100 Actor、12 材质，HDRI 天空、方向光阴影、
  双探针、透明混合、金属/发光雕塑、移动物体、两个骨骼角色、48 个共享材质方块，
  自动曝光历史、五级 Bloom 下采样/上采样、ACES/Gamma/FXAA。
- 默认一个 Once 全局探针、一个逐面实时局部探针；可以切换每帧一面/六面、请求全量重捕获。
- Lua 控制器：阴影/反射/Bloom/曝光/FXAA/天空/透明/物体组/运动开关；运动开关同时控制骨骼播放。
- 专用 Editor 入口：打开场景并 Play，画面与 Frame Graph 并排；Game View 聚焦时接受快捷键，
  搜索/编辑文本时不响应。保留生产 Editor 更新、帧提交、面板、场景和资源管理逻辑。
- 专用入口隔离编辑器偏好，资源目录链接到现有 Resources。工程包含实际原生预览图和
  约 1.38 MiB 的小尺寸 HDR 派生图；模型直接引用主线资源，构建产物放在 Build。
- 人工验收步骤覆盖依赖/资源版本、完整捕获与逐面 History、阴影深度图、资源预览、
  Bloom Load 读写、曝光 ping-pong、生命周期/池、分辨率变化和 GPU 热点。

## 原生验证结果

RTX 3070 Ti / NVIDIA 616.92 / OpenGL 4.5，本机 VS2022、SDK 10.0.22621.0。
测试独立运行 Debug/Release 的 Main/Render consumer，共四次完整产品 Editor 验收。

| 项目 | 实测结果 |
|---|---|
| 首次捕获 | 41 Pass / 36 资源，两个完整 cubemap 共 12 个 ReflectionCapture，曝光初始化 |
| 稳定逐面更新 | 27–28 Pass / 34–35 资源，每帧 1 个捕获，完成一轮时生成 mip |
| 稳定六面更新 | 33 Pass，每帧 6 个捕获 |
| 手动重捕获 | R 触发两个完整捕获，共 12 个捕获 Pass |
| 关闭效果 | 对应 Shadows、Reflection、Bloom、Exposure、FXAA Pass 全部消失 |
| 画面变化 | 8 个关闭状态以及运动状态均有实际像素变化 |
| 骨骼动画 | 骨骼动画时间前进，P 暂停后时间不再变化，暂停画面保持逐字节一致 |
| 分辨率 | 960×540 → 640×360 → 960×540；旧尺寸 scratch 进入 idleUnused，恢复画面逐字节一致 |
| 图契约 | 所有报告的资源访问/内容版本/依赖索引有效，暖帧含 History，物理池统计相符 |
| 线程模式 | 每配置 16 张场景图 Main/Render **逐字节一致**，无像素容差 |
| 探针选择 | 大地面、局部包含、边界相交、旋转 OBB、多个候选、全局与中性回退通过 |
| 天空纹理 | 实际 GPU 存储 1024×512 RGBA32F，无 mip，保留 HDR 数值；基础级 8 MiB |
| 资产保护 | 自动验证前后场景、材质、脚本和项目设置 SHA-256 保持不变 |

首次与稳定帧的 Pass 数不同是初始化/历史/逐面刷新造成的正常变化，不能只按固定数量验收。
默认仍是生产既定 Pass 顺序，图反映实际读取与写入，不新增自动调度。
相交雕塑的等深度边缘曾产生少量像素差异，已给本例各材质明确绘制优先级，使验收结果可复现；
未放宽比较阈值，也未改变生产排序规则。

验证入口：

~~~powershell
node Tests/RenderingCommands/run-framegraph-lab.mjs Release
node Tests/RenderingCommands/run-framegraph-lab.mjs Debug
~~~

结果：`Build/FrameGraphLabValidation/{Release,Debug}-result.json`，passed 均为 true。
各 `<配置>-<main|render>/` 下保存 cold、12 个 progressive、关闭效果、六面/一面、
recapture、motion、paused、paused-stable、resized、restored 的完整 JSON，及 16 张 RGBA、
尺寸和原生 UI 捕获。日志没有 Lua/GL ERROR；同步读回的 GL 性能提示来自验收捕获。
帧时间不作为本轮性能结论；这里使用固定 dt、初始暂停运动和非渐进曝光保证像素可比。

## 仍未做与下一步

人工验收入口已就绪，具体观感与操作由用户按指南确认；自动验收不能替代用户的体验判断。
真实反射仅包含 cubemap 探针，透明面板为 alpha 混合；没有新增 SSR/光线追踪/真实折射，
也没有新增级联/点光源阴影、G-buffer/延迟渲染、自动实例化、LOD 或遮挡剔除。
Frame Graph 通用 Buffer 图、Pass 自动裁剪/重排、多后端 barrier/异步队列仍按
[Frame Graph 交接档案](FRAMEGRAPH_HANDOFF.zh-CN.md) 列为独立扩展。

下一步回到 [实际场景性能](RENDERING_PERFORMANCE_HANDOFF.zh-CN.md)：提前判定各视图可见性，
减少不可见对象的材质/几何/骨骼快照准备。保留主视图之外的阴影与反射可见性，
以本例的图契约和逐像素基线作正确性保护，再用 PerformanceLab 独占采样判断收益。
本轮尚未实现该裁剪优化；没有把搭建场景算作帧率提升。

## 本次画面校正（2026-10-06）

人工检查原生预览时发现，金属地面作为一个很大的网格与局部 cubemap 探针相交，
但当前材质按每个网格只选一个探针、shader 再按探针盒逐像素拒绝盒外 IBL，
因此在地面形成明显的矩形反射/照明色块。最初把地面反射彻底关闭虽去掉色块，
但也让基线画面过暗；最终改为粗糙、低金属度介电地面并保留 IBL，降低色块，
同时将探针移离雕塑中心、调整金属粗糙度、灯光能量、曝光补偿和 Bloom。
阴影覆盖范围由 38 收紧至 30，阴影图仍为 1024²，避免提高贴图分辨率带来的额外显存开销。

更新后的 `Examples/FrameGraphLab/Preview.png` 与 `EditorOverview.png` 来自 Release Main
产品进程的原生读回。再次运行 `node Tests/RenderingCommands/run-framegraph-lab.mjs Release`
通过；本次资产组合的 Main/Render 两个 consumer 15 张画面逐字节一致，场景行为和图契约均通过。
该轮只调整示例场景/材质与截图，未改生产渲染器；当时项目尚未接入 HDRI 天空环境，
金属材质依赖场景 cubemap 捕获。后续更新见下节。

## HDRI 与探针回退（2026-10-07）

- 复用现有 Skysphere shader。新增天空材质以明确的绘制优先级先于其他不透明网格，
  参与主视图和反射捕获、不投射阴影。天空可通过 `7` 或 Inspector 的 `environment` 开关，
  切换时请求两个探针完整重捕获，避免 Once 探针保留旧天空。
- `PureSky.hdr` 原图为 4096×2048，当前加载器使用 RGBA32F，直接加载需要约 128 MiB。
  `environment.mjs` 在 HDR 线性空间做确定性的块平均，生成 1024×512 RGBE 派生图，
  不进行 tonemap 或 LDR 截断。实际 GL owner 查询及浮点读回确认纹理基础级为 8 MiB、
  无 mip，仍含大于 1 的 HDR 亮度。这个数字只代表天空纹理存储，不是进程总显存。
- 生产 `SceneRenderExtractor` 改为局部盒完全包含变换后的网格包围球时才选择局部探针，
  其余使用最近的全局探针或中性回退；包含判断考虑 OBB 旋转、缩放与蒙皮 bounds 放大。
  因为当前每个网格只有一个环境 map，该策略避免亮天空下再次出现地面矩形 IBL 缺口。
- 新回归通过公开 ExtractFrame 入口检查真实模型。旧库明确失败于“大地面未回退全局”，
  修复后通过包含/边界/旋转/多候选/无探针情况；没有暴露生产私有接口。
- Debug/Release 均完成真实 Editor 的 Main/Render 16 张画面精确对照；现有 Release
  `--auxiliary` 的串行、双 Worker、单 Worker 三种模式也通过，阴影/cubemap 图像与失败汇合保持有效。
- 更新 Preview、EditorOverview，并保存 `EnvironmentOff.png` 作为关闭天空并重捕获后的原生对照。
  阴影仍为 1024²、探针仍为 128²，新增天空没有单独增加 Pass。

尚未实现逐像素局部/全局探针混合和预滤波 GGX；当前粗糙度仍使用普通 cubemap mip。
完整包含判断是保守的，跨越边界时可能发生整网格环境切换。下一步可做探针平滑混合和
更准确的粗糙度预滤波，或按既定计划继续实际场景 CPU 提取/准备成本优化。
