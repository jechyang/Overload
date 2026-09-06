# 阶段 2、3 最终验收

日期：2026-09-07。范围来自 MULTITHREADING_NEXT_STEPS.md 第 4、5 节；阶段 1 的历史基线保留在 MULTITHREADING_SKINNING_PARALLEL_VALIDATION.md 及 MULTITHREADING_SKINNING_STAGE1_COMPLETION.md。

**阶段 2、3 已在本机 Windows/OpenGL、内置渲染路径和约定代表性项目范围内完成验收。** 原始要求逐项具备实现及证据；功能、所有权、GPU 回收、窗口/输入/退出、真实交叠、性能/延迟量化和默认配置决定均已闭合。生产最新提交 c697649，性能与通用分析工具提交 cd419de。

## 最终线程与所有权协议

| 对象 / 操作 | 归属和发布边界 |
|---|---|
| 窗口、平台事件、ImGui CPU 布局、场景/组件更新 | Main；场景更新可以使用既有 JobSystem，只在批次稳定期并行，并在消费者之前完成发布 |
| 拥有型场景/视图/UI 帧 | Main 录制，封口后移动交接；CPU 值、GPU 代际、目标配置、palette、UI draw data 和输入结果由对应帧持有 |
| GL、Driver、Tracy GPU、呈现 | 一个固定 Render owner；同线程模式保留 Main 作为 GL owner |
| 资源创建/上传/查询/resize | Main 的显式 ResourceExecutor 同步分派至 owner；借用输入只活到调用返回，返回值必须有所有权 |
| 最后释放 | GPU 使用由帧 fence 保证完成；外线程最后释放进入预登记删除队列，实际原生删除在 owner，域关闭使旧 ID 失效 |
| GPU 退役 / 交互 | 全帧成功且 GPU 退役后，Main 按对应帧顺序应用 Interaction；失败、取消、重复和过期场景结果拒绝 |
| Logger / TextureDebugger | 后台日志进入有界 Main 通知；纹理元数据/代际同步发布，Main 面板读取拥有型快照 |
| 停机 | 停止新帧；取消未提交帧并排空已接收帧；释放场景/UI/资源；关闭回收域和 Driver；Render 解除上下文、join；Main 销毁窗口和依赖 |

RenderThread 与 JobSystem 独立。MainThread 亲和性仍表示任务池构造线程，不能代表 Render owner。owner 内嵌资源 Invoke 直接执行；owner 的 Submit/WaitIdle/Shutdown 自等待明确拒绝。帧消费者不调用要求 Main 同步执行的 UI/日志回调，避免 Main 等 Render、Render 又等 Main。自定义任务/回调也必须遵守此约束。

## 阶段 2 要求核对

| 原始要求 | 最终实现 / 证据 | 结果 |
|---|---|---|
| 命令生成与 GL 执行分离 | ABaseRenderer、SceneRenderer、内置 feature/pass → RenderCommandList；同线程产品路径与独立 Render 路径共用拥有型帧 | 已核对 |
| 相机/视图/pass/draw/mesh/material/palette 所有权 | 按值常量、几何与材质/纹理/目标代际持有、独占 palette；销毁原 renderer、scene、UI、源数组后仍可回放 | 已核对；COMMAND_LIST、DRAW_INPUT、MATERIAL_SNAPSHOT、FRAME_INPUT、TARGET_STATE 报告 |
| 排序、多视图、fallback/override | 保持原生产顺序和块序合并；材质缓存/singleUse、灯光覆盖、共享资源污染恢复及三视图对拍 | 已核对；阶段 0、MATERIAL_COMMAND、DRAW_PACKET、EDITOR_COMMAND 报告 |
| 反射/阴影/动态资源历史 | 顺序提交上传/mipmap/pass 写读；逐面反射和渐变曝光在失败/取消后失效并重建，后继不能超车 | 已核对；TEMPORAL_HISTORY、COMMAND_LIST 报告 |
| UI、debug、picking/readback | 真实调试场景命令、独立读回图像与场景身份、全部已定位 UI 纹理 owning 入口、ImGui 稳定 draw data | 已核对；EDITOR_COMMAND、IMGUI_SNAPSHOT、EDITOR_UI 报告 |
| GPU 完成与资源保留 | RenderFrame CPU 提交和 GPU completion 分开；失败前缀资源同样保留到 GPU 退役 | 已核对；RENDER_FRAME、RESOURCE_LIFETIME 报告 |
| 新旧图像/状态与功能等价 | 真实动态阴影/反射/曝光/Bloom/tonemap/FXAA 与源销毁回放；实际 Game/Editor 模拟、视图/UI 与交互精确对拍 | 已核对；最新 WINDOW_UI 与既有各增量报告 |

表中缩写报告均指仓库根目录的 `MULTITHREADING_<名称>_VALIDATION.md`。早期增量中“尚未接入下一步”的描述是当时的历史状态，以本报告和 NEXT_STEPS 顶部的最终状态为准。

## 阶段 3 要求核对

| 原始要求 | 最终实现 / 证据 | 结果 |
|---|---|---|
| 固定 Render owner 与有界队列 | 独立上下文 owner；最多 2 个已接收帧，GPU 退役后释放容量；资源任务容量 64，与帧共享 FIFO | 已核对；RENDER_THREAD 报告 |
| 全部受支持 GPU 操作归属 | 七类 baregl 原生资源与 UI 后端分派，定向 glProgramUniform，ImGui 唯一引擎 GLAD、Tracy TLS/query 收尾 | 已核对；RESOURCE_EXECUTOR、IMGUI_LIFETIME、GPU_PROFILING 报告 |
| 延迟删除与 GPU 使用完成 | owner 删除域、外线程最后释放唤醒、关闭失效；CPU Submit 不冒充 GPU 完成 | 已核对；RESOURCE_LIFETIME、RENDER_FRAME 报告 |
| UI/多视图/resize/最小化/反射/picking | 全画布 96 帧、12 次场景/窗口/视图变化、播放切换；Game/Editor 各 6 次实际最小化/恢复；微小 Bloom 目标与积压 Gizmo | 已核对；WINDOW_UI 报告，两配置通过 |
| JobSystem 与等待关系 | CPU 批次不继承 ResourceExecutor；资源同步分派无 Main 回调闭环；owner 自等待拒绝、失败仍可资源清理 | 已核对；RENDER_THREAD、RESOURCE_EXECUTOR 与产品 Context/循环 |
| 正常/异常退出 | 实际 Application.Run、未提交取消、在途排空、四次应用重建、呈现/命令/初始化失败、后继失效和 shutdown 唤醒 | 已核对；GAME_THREAD、EDITOR_THREAD、RENDER_THREAD 报告 |
| GL owner、图像、输入、多帧稳定性 | draw/ImGui BaseVertex/query/current-context 检查；Game 32、Editor 16 组实际帧姿态/图像精确一致；对应帧输入与失败/过期拒绝 | 已核对；GAME_THREAD、EDITOR_THREAD、WINDOW_UI 报告 |
| 真实交叠、CPU/GPU、队列、延迟和配置 | 专用独占产品基准、GPU timestamp、Main/Render/退役边界、Tracy Update/Render 区间与线程身份；默认显式开启决定 | 已通过；FRAME_PERFORMANCE 报告，两配置 48 个 case、13,824 个统计帧与 24 个完整 Tracy case |

## 构建、回归与复现范围

最新生产修改 c697649 的完整 Debug/Release 构建是 `Build/JobsValidation/msbuild-{debug,release}-tracy-thread-names.log`，均 0 警告、0 错误，10 项构建产物复制一致。新发现的 Tracy Unicode 线程名越界复制已修复，两配置各 224 次真实名称查询及原 GPU/query/TLS 回归通过，实际长采集也完整结束。窗口/Bloom 修改的上一轮完整构建与压力证据仍见 WINDOW_UI 报告。

本次核对了两配置的 render-thread、resource-lifetime、resource-executor、log-dispatch、texture-registry、imgui-lifetime、gpu-profiling、render-frame、editor-stress、game-thread 和 command-list，共 22 组已有 result.json 和 run.log，均有成功结果且没有 FAIL 行；清单位于 `Build/JobsValidation/stage23-evidence-inventory.json`。它是对应增量及最新集成回归的证据清点，不是本轮重新执行了全部历史专项。最新生产改动后已重跑的图像/窗口/输入/动态回归见 WINDOW_UI 报告。

新增性能入口、测量样本、Tracy、指标口径和复现命令见 MULTITHREADING_FRAME_PERFORMANCE_VALIDATION.md 及 Tests/RenderingCommands/README.md。正式未连接样本与连接数据分开；Release 已在 Unicode 修复后重新链接采集，Debug 保留此前相同未连接渲染热路径的样本，并单独验证修复后的构建与 profiling 专项，没有计算跨配置/版本的加速比。最终证据清单及 12 项原始计量/分析/采集文件哈希在 `Build/JobsValidation/stage23-final-artifacts.json`。所有测试使用保存的专用项目和隐藏窗口；原始产物在被忽略的 Build/JobsValidation 下，另一台机器应由可跟踪测试入口重新生成。

## 交付边界与之后的工作

- Windows / 当前 OpenGL 驱动、内置 pass/feature 与上述代表性项目完成验收；不将结果外推为任意平台、任意驱动或任意用户扩展的形式化无竞争证明。
- 自定义 IMesh、未知 feature/pass/ImGui callback 不能自动跨帧。扩展必须显式提供拥有型数据和录制支持；既有拒绝与即时对照保持可见。
- 资源 CPU 元数据仍由 Main 顺序修改；shared_ptr 只保证存活，不授予任意 worker 并发写资源/GL 的权限。RenderThread/Context 不支持多个并行全局 GL owner，也不支持对象析构与外部 Submit/Shutdown 无约束竞态。
- 正常成功帧不丢弃；两帧接收容量之外 Main 可持有一个待提交帧。完整图像拾取读回仍同步发生在 Render；PBO、录制缓存/冗余上传优化与更低延迟策略可按后续测量独立推进，不属于本次必须新增的接口。
- 性能结论同时报告相对同线程命令和即时路径的成本。独立 Render 功能可用不等于所有负载都更快；默认保持 render_thread=false，通过项目显式开启并重启。
- 本任务范围到阶段 3 为止。阶段 4 的多 Pass/View 并行录制、阶段 5 RHI/Vulkan、阶段 6 fiber 是后续独立目标，本次没有将它们标成完成。

本轮分阶段提交 c697649（Tracy Unicode 稳定性）、cd419de（产品性能/延迟验收）及本文件所属的最终审计提交。未推送远端；用户原有 Editor/ItemPicker/SkeletalData/OvUI/.zcode 等修改继续保留。
