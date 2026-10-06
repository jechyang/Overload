# Frame Graph 复杂场景验收档案

日期：2026-10-06。基于主线 `754c59d9`，按用户要求先建立可以人为观察的复杂多 Pass
场景，再继续实际渲染性能优化。当前变更是场景、专用宿主、验证和文档，未改动生产渲染器。

## 已完成

- [FrameGraphLab](Examples/FrameGraphLab/README.zh-CN.md)：99 Actor、11 材质，方向光阴影、
  双探针、透明混合、金属/发光雕塑、移动物体、两个骨骼角色、48 个共享材质方块，
  自动曝光历史、五级 Bloom 下采样/上采样、ACES/Gamma/FXAA。
- 默认一个 Once 全局探针、一个逐面实时局部探针；可以切换每帧一面/六面、请求全量重捕获。
- Lua 控制器：阴影/反射/Bloom/曝光/FXAA/透明/物体组/运动开关；运动开关同时控制骨骼播放。
- 专用 Editor 入口：打开场景并 Play，画面与 Frame Graph 并排；Game View 聚焦时接受快捷键，
  搜索/编辑文本时不响应。保留生产 Editor 更新、帧提交、面板、场景和资源管理逻辑。
- 专用入口隔离编辑器偏好，资源目录链接到现有 Resources。工程包含实际原生预览图，
  总大小约 650 KiB；没有复制模型、另建引擎项目或添加大体积 Git 构建产物。
- 人工验收步骤覆盖依赖/资源版本、完整捕获与逐面 History、阴影深度图、资源预览、
  Bloom Load 读写、曝光 ping-pong、生命周期/池、分辨率变化和 GPU 热点。

## 原生验证结果

RTX 3070 Ti / NVIDIA 616.92 / OpenGL 4.5，本机 VS2022、SDK 10.0.22621.0。
测试独立运行 Debug/Release 的 Main/Render consumer，共四次完整产品 Editor 验收。

| 项目 | 实测结果 |
|---|---|
| 首次捕获 | 41 Pass / 34 资源，两个完整 cubemap 共 12 个 ReflectionCapture，曝光初始化 |
| 稳定逐面更新 | 27–28 Pass / 32–33 资源，每帧 1 个捕获，完成一轮时生成 mip |
| 稳定六面更新 | 33 Pass / 32 资源，每帧 6 个捕获 |
| 手动重捕获 | R 触发两个完整捕获，共 12 个捕获 Pass |
| 关闭效果 | 对应 Shadows、Reflection、Bloom、Exposure、FXAA Pass 全部消失 |
| 画面变化 | 7 个关闭状态以及运动状态均有实际像素变化 |
| 骨骼动画 | 骨骼动画时间前进，P 暂停后时间不再变化，暂停画面保持逐字节一致 |
| 分辨率 | 960×540 → 640×360 → 960×540；旧尺寸 scratch 进入 idleUnused，恢复画面逐字节一致 |
| 图契约 | 所有报告的资源访问/内容版本/依赖索引有效，暖帧含 History，物理池统计相符 |
| 线程模式 | 每配置 15 张场景图 Main/Render **逐字节一致**，无像素容差 |
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
recapture、motion、paused、paused-stable、resized、restored 的完整 JSON，及 15 张 RGBA、
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
