# Tracy 线程验收记录

日期：2026-09-06（北京时间）。结论：本轮 Tracy 线程调度与等待验收通过；未发现已知等待停顿或本地队列唤醒问题复现。随后已补齐 Debug/Release 全解决方案增量构建（0 警告、0 错误），阶段 0 验收完成。后续入口：[MULTITHREADING_NEXT_STEPS.md](MULTITHREADING_NEXT_STEPS.md)。

## 采集方式与范围

- 使用官方 Tracy 0.11.1 的 capture / csvexport，全程后台命令行。没有操作 VS2022、桌面或修改权限配置。
- 工程客户端也是 0.11.1。生产源码和现有 scene_validation.cpp 的 SHA-256 与上次 source-manifest.json 全部一致。
- 引擎采集运行现有 scene_final_release.exe（自动 11 workers），仍使用隐藏 OpenGL 窗口。先完成对拍，在 TRACE READY 时启动 10 秒采集。
- 另编译独立 tracy_scheduler.cpp，链接当前 JobSystem.cpp 和项目 Release Tracy 库，/W4 /WX 通过；只在临时用例里添加区间标记，没有修改生产埋点。
- 两份采集器、两份被测程序均退出 0，未触发超时终止。所有工具、源码、原始记录和分析脚本位于 Build（Git 忽略目录）。

## 引擎并行区间

记录：Build/JobsValidation/tracy-release-auto.tracy，9,894,057 字节；采集时间跨度 10.08 秒，2,718,012 个 zone。

- 保存了 JobWorker-0 至 JobWorker-10 全部 11 个名称。官方 CSV 导出只提供数字线程编号，名称通过按官方 v0.11.1 容器格式只读解码字符串核实；图中使用 CSV 编号，不猜测编号与名称的对应关系。
- CSV 中主线程为 12，另有 11 条执行任务的工作线程。共 57,112 个 Execute 区间。
- 649 次 Parse 和 649 次 Filter，各次均有 44 个任务；每次执行区间涉及 8–12 条线程，中位数为 12。峰值重叠区间数为 12（含主线程）。这是任务墙钟区间重叠，不等于操作系统同时运行的 CPU 核心计数。
- 主线程执行了 2,117 个 Parse 任务和 2,589 个 Filter 任务，确认 Wait 参与执行路径实际生效。
- 全部任务的 Execute 区间均在所属 Parse/Filter 区间内结束；没有采集到任务越过阶段返回边界。

| 指标 | Parse | Filter |
|---|---:|---:|
| 阶段耗时中位数 | 1.031 ms | 1.527 ms |
| 阶段耗时 P95 | 1.274 ms | 1.856 ms |
| 阶段耗时最大值 | 2.847 ms | 2.442 ms |
| 最后一个任务结束至阶段返回，中位数 | 0.475 ms | 0.960 ms |

负载是 4096 个 Cube，已连接 Tracy；不是先前混合模型、未连接采集器的串并行微基准，两个数据集不能直接计算性能提升。

[单次实际执行时间线](Build/JobsValidation/tracy-thread-timeline.svg)选取接近 Parse 中位数的周期；蓝色为 Parse，橙色为 Filter，灰色为最后一个任务结束后的主线程收尾。

## 已知等待与唤醒问题复测

记录：Build/JobsValidation/tracy-scheduler.tracy；4 workers；10.01 秒，3,100 个 zone。其工作线程在进入忙等工作体后才让主线程调用 Wait，避免主线程直接领走被测任务。

| 指标 | 中位数 | P95 | 最大值 |
|---|---:|---:|---:|
| 1000 次约 800 µs 工作的 Wait 区间 | 803.383 µs | 805.481 µs | 1025.846 µs |
| 工作体结束至 Wait 返回 | 3.391 µs | 5.333 µs | 64.673 µs |
| 20 次本地入队至另一 worker 的子任务开始 | 16.127 µs | 318.461 µs | 318.461 µs |

- 1000 个 Wait 和 1000 个工作区间完整配对，等待者与执行者全部不同；没有一次 Wait 达到 5 ms 或 10 ms。之前约 15 ms 的等待停顿本轮未复现。
- 本地队列用例先等待 110 ms 让其他 workers 空闲，再由正在执行的 worker 提交子任务。父任务等待子任务，主线程不参与领任务；20 个子任务全部由不同于父任务的 worker 执行，均完成，无长时间丢失唤醒。
- 原有引擎 580 组 Parse/Filter 对拍再次通过；离屏 RGBA 逐像素相同，37,341 个非背景像素。

## 设计观察与解释边界

1. Filter 主线程收尾仍是明确的优化方向。最后一个 Execute 结束至返回的耗时占阶段耗时的逐周期中位数约 62.7%。源码中有按块逐节点 multimap 重新分配/插入和局部容器析构；当前埋点没有分开 Wait 返回、合并和清理，因此不能把这 0.960 ms 全部归因于锁等待或全部归因于合并。后续优化应先细分埋点，并继续维护相同排序键的稳定顺序。
2. Trace 循环周期中位数约 15.57 ms，但两阶段后的空隙中位数约 12.91 ms。测试程序每轮显式 sleep_for(2ms)，并有局部输出销毁；不能把这个间隔当作线程池 Wait 停顿，也不能将该测试周期宣称为编辑器帧率。
3. Tracy 提供了本次运行的时序证据，不替代已有 ASan、双 deque 压力测试，也不证明所有竞争和死锁都不存在。本轮没有采集完整编辑器更新、绘制、反射和后处理帧。
4. 已知的生产 Context 错误回调缺失、亲和/停机调用契约等设计关注项仍保留在 MULTITHREADING_VALIDATION.md，本轮未修改这些策略。

## 复现与证据

在工程根目录执行（先后运行，不同时启动两个 Tracy 客户端）：

~~~bat
node Build\JobsValidation\run-tracy-capture.mjs tracy-release-auto 0 scene_final_release.exe
node Build\JobsValidation\export-tracy.mjs tracy-release-auto
node Build\JobsValidation\run-tracy-capture.mjs tracy-scheduler 4 tracy_scheduler.exe
node Build\JobsValidation\export-tracy.mjs tracy-scheduler WaitProgress WorkerBusy800us LocalQueueWake LocalWakeChild WakeParent Execute
node Build\JobsValidation\analyze-tracy.mjs
node Build\JobsValidation\plot-tracy.mjs
~~~

分析脚本的名称提取使用 Node 的 Zstd 支持；本次使用 Codex 附带的 Node。采集脚本保护被测程序 120 秒、采集器 45 秒，只有 TRACE READY 后才启动采集。复跑同名会更新对应证据文件。

主要文件：tracy-release-auto / tracy-scheduler 的 .tracy、-harness.log、-capture.log、-result.json、逐区间 CSV、-analysis.json；tracy-release-auto-thread-names.json；compile-tracy_scheduler.log、tracy_scheduler.args.json、tracy-thread-timeline.svg。编译参数及原始源码均已保留。

工具来源与大小校验：Build/Tools/Tracy-0.11.1/download-manifest.json。
容器读取参考：[Tracy 0.11.1 FileRead](https://github.com/wolfpld/tracy/blob/v0.11.1/server/TracyFileRead.hpp)。
