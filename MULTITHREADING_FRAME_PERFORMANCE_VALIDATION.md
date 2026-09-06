# 阶段 3 第十二增量：产品帧性能、队列与延迟

日期：2026-09-07。生产前置提交：c697649（含本轮发现的 Tracy Unicode 修复）。测试运行在包含已有用户改动的工作区；本增量只增加测试、分析工具和验收文档。

## 方法与负载

正式比较使用未连接 Tracy 的独占进程，三次重复按正序、逆序、正序运行模式。每个普通 case 384 帧，前 64 帧预热，统计 320 帧；每个拾取 case 224 帧，前 32 帧预热，统计 192 帧。初始化、模型加载、布局稳定和样本前预热不计入统计。固定模拟步长为 1/60 秒，没有用 sleep 模拟工作或推算帧率。

| 项目 | 固定内容 |
|---|---|
| 本机 | Windows 11 Home China；i5-12490F，12 个逻辑 CPU；约 15.84 GiB RAM |
| GL | RTX 3070 Ti；OpenGL 4.5.0，NVIDIA 526.98，GLSL 4.50 |
| 编译 | VS2022 v143，14.42.34433；Debug /Od，Release /O2；/WX，TRACY_ON_DEMAND |
| Game | 新建并保存的 frame.ovscene，8 个 Mannequin 动画实例、相机、默认灯光；320×240，启用实际调试 UI |
| Editor | 新建并保存的 crowd.ovscene，64 个 Mannequin 动画实例，Play；实际 Asset/Game/Scene 三视图，窗口 960×320，面板各 320×260 |
| Asset View | 实际预览场景与后处理；渐变曝光关闭，反射按六面更新；使用既有 ViewsFixture 固定布局和相机 |
| Editor picking | 相同三视图负载，强制 Scene View hover，实际 Picking pass 与完整图像读回；捕获输入的坐标置于视图中心，不移动桌面鼠标；比较同线程与双线程命令模式 |
| 窗口 | 隐藏且不抢焦点；性能测试显式关闭 VSync；测试使用独立 AppData |

普通 Editor 性能负载是固定的三视图画布。完整产品 Canvas、15 个面板、窗口和真实 Gizmo 操作的功能压力证据见 MULTITHREADING_WINDOW_UI_VALIDATION.md；它们的耗时没有混入这里的性能样本。拾取测试只量化 hover/readback 结果应用，不将其描述为物理鼠标点击或完整拖拽的测量。Game 的 8 实例属于轻量场景。

三种模式为当前代码的 `render_commands=false, render_thread=false` 即时路径、`render_commands=true, render_thread=false` 同线程拥有型命令路径，以及两者均为 true 的固定 Render 路径。不是检出历史源码后跨版本比较。每个 case 使用新的 Application/Context，但在同一进程中顺序运行。

Release 最终样本在 c697649 的生产源码重新链接后采集；Debug 保留本轮此前完整通过的独占样本。该修复只影响连接 Tracy 时的外部线程名解析，不改变未连接的渲染热路径；Debug 修复已通过完整构建和专门回归。这里不计算跨配置或跨版本的加速比。

## 计量边界

- `main_start_ns` 在 PreUpdate 前；`main_updated_ns` 在 Update 返回后；`main_end_ns` 在 PostUpdate 返回后。整段 Main 帧时间包含平台事件、更新/录制、提交及可能的容量等待。逐帧 completion 观察另有少量测试开销，吞吐量也计入它。
- Render 开始在当前生产 RenderFrame::Submit 的命令执行之前；即时路径在场景渲染之前。消费时间统计至 Present 调用前，包括当前帧场景、各视图、UI 和相应驱动调用的墙钟时间。
- 两个 GL_TIMESTAMP 包围从渲染开始到 Present 前的 GPU 时间线跨度。所有查询预先分配，仅在整批排空/glFinish 后读取，稳态帧不为计时增加 glFinish 或查询结果等待。该跨度可能包含 CPU 提交间隙，**不是 GPU 忙碌占用率**，也不包含显示扫描输出。
- GPU 原始时间戳可能超过 JavaScript 安全整数范围。分析器使用 BigInt 相减后才换算毫秒；不把 GPU 时间戳与 CPU steady_clock 相减。
- 容量时间在队列锁内、等待谓词前开始，到成功入队后结束，包含等待及入队操作；不包含获取锁之前的时间。队列长度是在入队瞬间采样。每帧断言排队数不超过已接收数，已接收数不超过 2。
- 呈现延迟为 Main 帧开始到 glfwSwapBuffers 返回；GPU 退役在正式 retired 标记发布前记录；完成观察在 Main 看到 IsRetired 后记录。Editor 另记录 Interaction::consumed 首次被观察到的时间，上界包括观察器开销。
- 这些是帧输入处理边界到呈现请求返回、GPU 退役或应用交互的延迟，**不是输入设备到屏幕发光的延迟**。隐藏窗口、关闭 VSync 的数据不能直接换算可见窗口的显示延迟；即时路径没有每帧 fence 退役/交互观察值可用于同口径比较。
- 即时 CSV 的 completion/interaction 观察列使用 Update 返回时间作为夹具占位；分析器将即时模式的两项观察统计设为 null，不把占位值当作 GPU 完成或真实交互耗时。
- 帧率为“已统计帧数 / 第一统计帧开始至整批 GPU 排空”的吞吐量，包含尾部 drain。分位数来自三次重复合并的逐帧样本；不把某次最快结果当作整体提升。

队列接收容量 2 包含排队、正在消费和 GPU 在途的帧；Main 可以另外准备一个待提交帧，然后在 PostUpdate 背压等待。因而从输入处理开始观察的延迟可以覆盖三个帧阶段，不能把容量 2 解释为最多两次更新的输入延迟。生产不丢弃正常成功帧，交互按其对应帧顺序应用。

## 正式结果

两配置共 48 个完整 case、13,824 个预热后帧样本，编译与运行均成功；所有时序、连续帧 ID、GPU 退役、交互和容量断言通过。下表单位为 ms，FPS 取三次重复吞吐的中位数，括号内为范围。

| 配置 / 负载 | 模式 | FPS 中位数（范围） | Main 帧 p50 | Render 至呈现前 p50 | GPU 渲染跨度 p50 |
|---|---|---:|---:|---:|---:|
| Release Game | 即时 | 1288.9（1228.3–1317.9） | 0.736 | 0.397 | 0.225 |
| Release Game | 同线程命令 | 695.1（686.4–701.8） | 1.361 | 0.289 | 0.478 |
| Release Game | 双线程命令 | 1029.9（998.8–1036.7） | 0.907 | 0.283 | 0.476 |
| Release Editor 三视图 | 即时 | 104.6（102.1–105.4） | 9.535 | 4.169 | 4.605 |
| Release Editor 三视图 | 同线程命令 | 63.5（63.5–64.0） | 15.616 | 3.855 | 8.889 |
| Release Editor 三视图 | 双线程命令 | 87.8（87.6–88.2） | 11.178 | 4.029 | 8.750 |
| Release Editor 拾取 | 同线程命令 | 52.6（51.5–53.1） | 18.922 | 12.990 | 10.799 |
| Release Editor 拾取 | 双线程命令 | 69.9（69.5–70.2） | 14.095 | 13.116 | 10.842 |
| Debug Game | 即时 | 235.0（234.8–242.0） | 3.796 | 2.214 | 0.530 |
| Debug Game | 同线程命令 | 120.3（119.3–125.9） | 7.674 | 0.867 | 1.082 |
| Debug Game | 双线程命令 | 218.5（209.2–223.3） | 4.394 | 0.823 | 0.697 |
| Debug Editor 三视图 | 即时 | 22.5（22.5–22.6） | 42.908 | 31.997 | 8.701 |
| Debug Editor 三视图 | 同线程命令 | 11.7（11.3–11.7） | 85.393 | 10.891 | 20.247 |
| Debug Editor 三视图 | 双线程命令 | 15.6（15.1–16.1） | 62.985 | 12.769 | 16.267 |
| Debug Editor 拾取 | 同线程命令 | 9.7（9.3–9.9） | 103.535 | 32.843 | 26.770 |
| Debug Editor 拾取 | 双线程命令 | 13.7（13.2–14.0） | 71.891 | 23.937 | 17.100 |

呈现与交互观察延迟同样从 Main 帧开始计算。以下为 p50 / p95；即时路径没有同口径 GPU 退役/交互观察数据，所以不列入此表。完整 p99、最大值、呈现调用时间及退役延迟保留在 analysis.json。

| 配置 / 负载 | 模式 | 至呈现返回 | 至 GPU 完成被 Main 观察 | 至交互应用被 Main 观察 |
|---|---|---:|---:|---:|
| Release Game | 同线程命令 | 1.315 / 1.825 | 1.361 / 1.934 | — |
| Release Game | 双线程命令 | 2.691 / 3.501 | 2.758 / 4.025 | — |
| Release Editor 三视图 | 同线程命令 | 15.122 / 16.523 | 15.616 / 16.985 | 15.617 / 16.985 |
| Release Editor 三视图 | 双线程命令 | 33.080 / 34.691 | 33.768 / 43.588 | 33.781 / 43.807 |
| Release Editor 拾取 | 同线程命令 | 18.363 / 19.517 | 18.923 / 20.070 | 18.923 / 20.070 |
| Release Editor 拾取 | 双线程命令 | 41.717 / 43.475 | 42.428 / 48.627 | 42.444 / 49.425 |
| Debug Game | 同线程命令 | 7.368 / 9.979 | 7.674 / 10.356 | — |
| Debug Game | 双线程命令 | 6.457 / 7.905 | 9.184 / 13.991 | — |
| Debug Editor 三视图 | 同线程命令 | 83.112 / 93.523 | 85.394 / 96.209 | 85.394 / 96.209 |
| Debug Editor 三视图 | 双线程命令 | 84.641 / 95.540 | 126.169 / 140.326 | 126.169 / 140.326 |
| Debug Editor 拾取 | 同线程命令 | 100.552 / 112.154 | 103.536 / 114.976 | 103.536 / 114.976 |
| Debug Editor 拾取 | 双线程命令 | 97.503 / 110.981 | 145.066 / 160.456 | 145.066 / 160.457 |

双线程入队计量如下；长度是入队时采样的最小–最大值，等待为 p50 / p95。每个成功帧都有检查，不把未采样的瞬时队列长度当作观测值。

| 配置 / 负载 | 排队帧数 | 已接收未退役帧数 | 容量等待及入队 ms |
|---|---:|---:|---:|
| Release Game | 1–2 | 1–2 | 0.467 / 0.792 |
| Release Editor 三视图 | 1–1 | 2–2 | 6.479 / 17.182 |
| Release Editor 拾取 | 1–1 | 2–2 | 8.861 / 9.900 |
| Debug Game | 1–1 | 1–2 | 0.001 / 0.002 |
| Debug Editor 三视图 | 1–1 | 1–1 | 0.002 / 0.003 |
| Debug Editor 拾取 | 1–1 | 1–1 | 0.002 / 0.003 |

Release 中，以三次吞吐中位数比较：Game 双线程相对同线程命令提高约 48.2%；Editor 三视图 双线程相对同线程命令提高约 38.1%；Editor 拾取 双线程相对同线程命令提高约 32.8%；但 Game 与普通 Editor 的双线程吞吐仍低于当前即时路径。Debug 也有命令捕获/消费成本，不能把 Release 的收益外推为 Debug 的绝对速度。GPU 跨度含提交间隙，不能据其变化推断 GPU 指令量或忙碌时间的相同比例变化。

Editor 三视图 交互观察 p95 从同线程命令 16.985 ms 增加到双线程 43.807 ms。Editor 拾取 交互观察 p95 从同线程命令 20.070 ms 增加到双线程 49.425 ms。Debug 中 Main 更新较慢，结果可能直到下一次 Main 检查才被观察。这里明确保留吞吐与交互延迟的取舍，不宣称独立 Render 在所有负载下更快，也不宣称两帧是所有项目的最优缓冲数。

### Tracy 独占交叠验收

Game 与 Editor 的 harness/capture 均退出 0，官方 CSV 导出和两个分析器全部通过。每个 case 的 Main Frame、Main Update 与 Render Frame 数量、源码路径和线程身份均检查完整，不使用门闩或 sleep 推算自然交叠。下表含预热帧，连接数据只用于时序证明，不与未连接表计算加速比。

| 负载 | 完整 case / 帧 | 双线程每组 Update 与 Render 交叠数 | 同线程命令 |
|---|---:|---:|---|
| Game | 9 / 3,456 | 383 / 384（三组） | Render 与 Main 同线程，零交叠 |
| Editor 三视图 | 9 / 3,456 | 383 / 384（三组） | Render 与 Main 同线程，零交叠 |
| Editor 拾取 | 6 / 1,344 | 223 / 224（三组） | Render 与 Main 同线程，零交叠 |

Main Update zone 只包围真实 Product::Update，排除 PostUpdate 中的容量等待；Render zone 包围真实 RenderFrame::Submit。双线程各 case 的 Render ID 与 Main ID 不同。原始 CPU 计时还检查到呈现前的消费者区间与 Main 准备区间交叠。墙钟区间相交不是 CPU 忙碌率或形式化无竞争证明，正确性仍由既有图像、所有权与压力回归共同支撑。

首次长采集发现并修复的 Unicode 线程名崩溃已经单独提交 c697649，诊断与两配置回归见 MULTITHREADING_TRACY_THREAD_NAME_VALIDATION.md；这里使用修复后的完整采集。

## 默认配置决定

保留 `render_commands=true` 的拥有型帧默认路径；`render_thread=false` 保留为默认配置，双线程由项目显式开启。此决定基于上述吞吐收益、较高的编辑器交互尾部延迟，以及相对即时路径仍有成本的结果。拥有型帧路径的成本明确保留在表中，即时对照继续可用；本机结果不足以支持所有项目默认开启 Render 线程。

Editor 在项目的 `.ovproject`，Game 在 `Game.ini` 中设置：

~~~ini
render_commands=true
render_thread=true
~~~

重启 Application 后生效。独立 Render Context 不允许运行时切回即时路径；`render_thread=true` 配合 `render_commands=false` 在启动时明确拒绝。需要即时对照时，将两者都设为 false 并重启。

## 复现与证据

先完整构建相同配置，再按顺序独占执行；可在运行入口后添加 `--vs` 指定 VS2022：

~~~text
node Tests/Skinning/run-validation.mjs Release --game-benchmark
node Tests/Skinning/run-validation.mjs Release --editor-benchmark
node Tests/Skinning/run-validation.mjs Debug --game-benchmark
node Tests/Skinning/run-validation.mjs Debug --editor-benchmark
~~~

输出目录分别为 `Build/JobsValidation/Skinning/<配置>-game-benchmark/current`、`<配置>-editor-benchmark-command-list-editor-ui/current`。每项保留 compile.log、run.log、product-frame-metrics.csv，父目录的 result.json 仅在完整退出成功后生成。

~~~text
node Tests/RenderingCommands/analyze-product-benchmark.mjs <上述目录>/product-frame-metrics.csv
node Tests/RenderingCommands/capture-product-benchmark.mjs game
node Tests/RenderingCommands/capture-product-benchmark.mjs editor
~~~

采集工具默认使用本地 `Build/Tools/Tracy-0.11.1/tracy-capture.exe`，可用第二个参数指定路径。单次只启动一个引擎进程；等待 TRACE READY 才连接，捕获结果为 `Build/JobsValidation/tracy-<game|editor>-benchmark-release.tracy`。连接后的 CSV 单独写入 product-frame-metrics-connected.csv，不覆盖正式未连接数据，也不用于计算未连接性能提升。

~~~text
tracy-csvexport -u -f Benchmark <capture.tracy> > <events.csv>
node Tests/RenderingCommands/analyze-product-tracy.mjs <events.csv>
~~~

测试链接当前生产对象。Window、RenderFrame、RenderThread 与对应 Game/Editor.cpp 的当前源码副本只加入计时/Tracy hook；没有改队列容量、调度、呈现/fence 顺序或生产 ABI。资源/窗口/AppData 隔离沿用已通过的产品测试覆盖。Raw query 池在 owner 分配、读取和释放。

首轮 Debug 编辑器被旧外层脚本的 300 秒超时截断，不作为最终成功或性能证据；后续入口为长基准预留更长执行时间。功能回归的超时保持原值。
