# 多线程阶段 0：实现审查与验收记录

日期：2026-09-05；最终验收更新：2026-09-06。状态：阶段 0 已验收完成，包括核心实现、后台回归、Tracy 线程/等待采集及最终 Debug/Release 全解决方案增量构建。后续实施入口：[MULTITHREADING_NEXT_STEPS.md](MULTITHREADING_NEXT_STEPS.md)。

## 实现范围

- OvTools::Jobs：依赖计数、Submit/Wait/WaitAll、MainThread 亲和队列、ParallelFor、worker 本地 Chase-Lev 队列及 MutexDeque 对照实现。
- Editor/Game Context 创建并注册线程池；ParseScene/FilterDrawables 按连续块并行，返回前同步等待并按块序合并。
- GL 仍在主线程。FilterDrawables 还会由 DrawFrame 内的反射 pass 调用，原计划“全部只在 BeginFrame 内运行”不准确。
- 本轮没有操作 VS2022 或继续使用桌面自动化；后台测试采用隐藏 GLFW/OpenGL 窗口和命令行编译。

## 审查发现与处理

| 问题 | 处理与证据 |
|---|---|
| 停机时重复依赖/共享后继使用裸指针 worklist，释放后再次访问 | 改为持有 shared_ptr 的闭包遍历；先 join worker 再退休队列。ASan 修复前报告 heap-use-after-free，修复后回归通过 |
| worker 只检查全局队列的唤醒条件，本地入队可能睡过通知 | 搜索队列前观察 epoch，发布时更新 epoch 并通知；原约 62ms 回归降至 0ms（毫秒精度） |
| Wait 自旋后 sleep_for(200µs)，Windows 可实际睡约 15ms | 改进度条件变量，新任务及完成通知均可唤醒；约 0.8ms 工作的等待中位数 0.803ms、P95 0.807ms |
| 任务先发布终态，错误回调/捕获仍在使用调用者状态 | 回调执行并释放 callable 捕获后才发布终态；保留句柄的捕获释放测试通过 |
| 错误回调抛异常可能终止 worker、截断依赖图 | 隔离回调自身异常，失败任务后继仍执行；handler_throw 回归通过 |
| Chase-Lev Steal 缺少 top/bottom 读取之间的顺序屏障 | 补 seq_cst fence；校正 Pop 最后元素 CAS 后的 bottom 恢复。对照 Le 等人 PPoPP 2013 Figure 1，不采用计划中的“双 CAS”描述 |
| 跨线程池句柄无法保证正确挂接/等待 | 控制块携带池身份；Submit/Wait/WaitAll 对外池句柄抛 invalid_argument |
| Submit 部分挂接失败时可能提前执行；分块提交失败后引用局部对象的任务仍在跑 | 提交哨兵保证全部挂接结束前不就绪；失败清理节点；ParallelFor 与两个渲染阶段失败路径等待已提交任务再展开局部变量 |
| 线程池构造过程中创建部分线程后抛异常 | 停机并 join 已创建线程，避免析构 joinable thread |
| ParallelFor 的 count+grain-1 可溢出 | 改除法/余数形式；UINT64_MAX 区间完整覆盖测试通过 |
| 少量对象按 2×worker 门槛拆成大量任务 | Parse 门槛 max(128,2×worker)，Filter 门槛 max(256,2×worker)，分别匹配两阶段开销 |
| 文档声称 Retired 永不完成、shared_ptr 必须 complete type、所有路径在 BeginFrame 内、Release 完全不限制亲和性 | 已按实际状态机、内联查询、调用点及队列实现修正计划和公开注释 |

算法参考：[Correct and Efficient Work-Stealing for Weak Memory Models](https://fzn.fr/readings/ppopp13.pdf)。ASan/压力测试覆盖已知失败场景，不等于对所有平台内存序的形式证明。

## 已执行验证

所有临时源码、可执行文件、编译参数、日志保存在忽略目录 Build/JobsValidation，不向生产代码插入测试逻辑。根目录先前测试文件已移至其 legacy 子目录；未删除内容。

1. 两种 deque 各自运行同一套冒烟测试，全部通过：基本任务、依赖顺序、菱形、千级链、百万元素 ParallelFor、等待参与、异常、嵌套任务、无效句柄、万节点 DAG、MainThread 和停机。原第 11 项仅表示双编译，证据以两个独立运行日志为准。
2. 两种 deque 均通过 ASan 扩展回归：4 个提交者共 40000 个任务与并发 WaitAll、单 worker 内 40000 个任务的本地溢出和嵌套等待、外池句柄、UINT64_MAX 区间、捕获释放、等待进度通知。
3. 最终 ASan 边界回归 callback/wake/retire/handler_throw 均退出 0。
4. 实际引擎 Release（自动 11 workers）和 Debug（4 workers）场景验证：每配置 580 组比较，涵盖空场景、旧门槛、新门槛 127/128 及 drawable 255/256、257/1024/4096 对象，Cube、真实 Mannequin 蒙皮、不可见对象、缺失/无效材质、透明/UI、fallback/override、视锥及重复排序键。
5. 逐元素比较 mesh/material/actor 身份、有效状态位、矩阵、bounds、visibility、蒙皮指针/count/poseVersion/boundsScale、featureSet 和三个 multimap 的键与稳定顺序。
6. 两条输入经过同一个实际 SceneRenderer 绘制到 640×480 RGBA 帧缓冲：逐像素相同，37341 个非背景像素。参考路径使用 SetDescriptor 真正替换串行输入。测试开发期间曾误用 AddDescriptor，在 Debug 断言发现后修正；此前的图像结果不作为最终证据。最终 Debug/Release 日志均来自修正后的程序。
7. 最终涉及的 SceneRenderer.cpp、JobSystem.cpp 使用匹配 Debug/Release 宏重新编译，链接用户已构建的引擎静态库；/WX 下通过。它是实际引擎链接测试，不等同于再次构建整个编辑器/游戏解决方案。

图像为人工构造测试场景，不替代编辑器实际项目场景、反射/后处理完整交互验收。串行基准使用提取后的共享单对象函数，因此验证重点是分块、调度、合并与输出等价，而不是独立重实现全部渲染算法。

主要证据：

- smoke-final-lockfree.log、smoke-final-mutex.log
- stress-final.log、stress-final-mutex.log
- regression-final-*.log、asan-before.log
- compile-scene_final_release.log、compile-scene_final_debug.log
- scene-final-release-auto.txt、scene-final-debug-4.txt
- scene_validation.cpp、stress.cpp、regression.cpp、tools.mjs、*.args.json
- parallel.ppm、serial.ppm、parallel.png（PPM 是最终原始读回）

## Release 性能样本

本机 12 个硬件线程，自动 11 workers；RTX 3070 Ti / OpenGL 4.5。每场景预热后串并交替 24 次，表中为中位数，包含输出销毁；无 Tracy 采集连接，避免把采集开销混入基线。单位毫秒。

| 对象数 | drawable 数 | Parse 串行 | Parse 并行 | Filter 串行 | Filter 并行 |
|---:|---:|---:|---:|---:|---:|
| 257 | 315 | 0.452 | 0.142 | 0.251 | 0.169 |
| 1024 | 1259 | 1.869 | 0.409 | 0.937 | 0.536 |
| 4096 | 5037 | 8.117 | 1.808 | 4.335 | 1.984 |

4096 对象下分别约 4.49×、2.19×。这是两个 CPU 阶段的微基准，不是整帧 FPS 提升承诺。Debug 的 multimap/分配检查开销使 Filter 并行仍可能更慢，性能判断使用 Release。

## 仍需关注的设计

- **P2：Filter 合并成本。** 每块先建立 multimap，主线程再逐节点重新分配/插入，串行合并成为上限。可在独立后续改造中评估节点 extract/insert 或 vector+stable_sort；必须保持同键顺序，不能未经对拍改成不稳定合并。
- **P2：错误可观测性。** API 是 fire-and-forget，Wait 表示终态，不保证成功。生产 Context 尚未安装错误回调，异常可只留下部分渲染结果。后续应明确记录/传播失败的接口；不能把“后继可继续”误认为结果正确。
- **亲和/停机契约。** 任务不得等待自身、后继或需要被阻塞构造线程执行的任务；直接 MainThread Wait 断言不能检测传递依赖。析构不能与外部 Submit/Wait/Pump 并发，运行任务必须能在停机后结束。Render 线程阶段开始前需要重新设计这些约束。
- **阈值及分配。** 当前门槛来自本机场景，不是通用最优值；高核心数、很轻任务及不同模型 mesh 数仍需代表性场景复测。
- **资源耗尽。** 本次加强了提交失败路径，但没有将调度器宣称为无分配或全局 OOM 可恢复系统；队列/停机闭包仍可能分配。

## Tracy 验收更新（2026-09-06）

命令行采集与分析已完成，详见 [MULTITHREADING_TRACY_VALIDATION.md](MULTITHREADING_TRACY_VALIDATION.md)。

- 10 秒实际引擎记录确认 JobWorker-0 至 JobWorker-10、主线程等待参与；649 次 Parse 和 649 次 Filter，每次 44 个任务均在阶段返回前结束。
- 1000 次约 0.8 ms 工作的 Wait，Tracy 中位数 0.803 ms，最大 1.026 ms；工作结束至 Wait 返回中位数 3.391 µs。20 次本地队列唤醒均由另一 worker 完成，入队至子任务开始最大 0.318 ms。
- 580 组引擎对拍及离屏像素比较再次通过；两份采集器和被测程序全部退出 0。
- Filter 最后一个任务结束至阶段返回中位数 0.960 ms，仍需关注串行收尾成本；现有埋点无法分离唤醒、合并和析构。

## 最终构建验收与后续范围

- 最终当前工作区的 Debug/x64、Release/x64 全解决方案增量构建已完成，均退出 0、0 警告、0 错误；包括 OvEditor/OvGame。日志为 Build/JobsValidation/msbuild-debug-verified.log 和 msbuild-release-verified.log，结构化参数/退出状态见同名前缀的 -result.json。
- 首轮构建成功但带有本机全局 vcpkg DLL 收集的环境警告；确认项目程序导入为系统/MSVC DLL 后，仅在验收命令传入 VcpkgApplocalDeps=false，保留所有项目 /WX，不修改工程或全局配置。
- 构建后额外核对两配置编辑器、游戏复制输出，以及 Cube/Mannequin/Standard shader 的资源副本，均与源文件一致；证据为 Build/JobsValidation/stage0-build-artifacts.json。未构建 Publish，既有 post-build 的 Publish/可选 DLL 缺失提示不作为 Debug/Release 缺失产物。
- 默认 PowerShell 入口曾遇到 CET 问题，已找到可用的 cmd.exe 入口，不能再称所有命令行均不可用。后台浏览器路由超时仍是独立问题；诊断记录见 Build/Diagnostics/CODEX_NETWORK_DIAGNOSIS.md。
- 官方 0.11.1 capture/csvexport 已下载至 Build/Tools/Tracy-0.11.1；下载阻碍已解除，实际本机采集连接也已成功。采集无需 VS2022 或桌面自动化。
- 完整编辑器项目场景、更新/反射/后处理与整帧 FPS 仍不在本轮独立负载验收范围。
