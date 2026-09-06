# 阶段 3 第六增量：Tracy GPU 查询与 TLS 收尾

日期：2026-09-06。前置提交：e257ffa（ImGui GPU 代际与 loader）。

## 生命周期

TracyOpenGL 新增显式 CreateGpuContext/DestroyGpuContext，分别由 TracyGpuContext 和 TracyGpuContextDestroy 宏调用。初始化先准备内存和查询池，构造成功后才发布 TLS；重复创建拒绝覆盖活上下文。构造中途失败删除已生成的查询名称并释放 CPU 内存，不发布半初始化 TLS。

GpuCtx 销毁在原 GL owner、上下文仍当前且所有 GPU zone 已结束后执行：glFinish 完成尾部 GPU 工作，Collect 取回未收集的时间戳，再删除 64×1024 个查询名称。随后释放 GpuCtx 内存并清空 TLS；重复 Destroy 为 no-op。未启用 Tracy 的宏保持空操作。没有增加依赖线程退出的自动 GL 析构，也没有复用或重置 Tracy 的上下文编号。

Driver 正常析构在释放 baregl Context 前调用 profiling 销毁；构造期间的局部 guard 在 profiling 已初始化、但后续初始化抛出时做相同收尾。RenderThread 原先的帧/GPU 排空先完成，随后 Driver 收尾，最后解除窗口上下文。legacy Driver 调用方也必须保持原 GL 上下文当前并结束全部 GPU zone。

新增 glFinish 只用于 profiling 上下文退出，不进入每帧路径。这里保留了上游 8-bit GPU context ID 的会话限制，不能通过重置计数器让同一采集内的不同上下文冒用旧编号。

## 验证

先完整构建 Debug/Release，再运行（按需追加 --vs）：

~~~text
node Tests/Skinning/run-validation.mjs Debug --gpu-profiling
node Tests/Skinning/run-validation.mjs Release --gpu-profiling
~~~

专项链接当前 Driver 实现，复制源码仅在 profiling 初始化前、局部构造 guard 建立后插入两个测试回调，以便在 GLAD 已加载后安装 GL hooks 并注入失败；生产源码没有测试 API，实例布局与所有其余实现保持一致。其他回归直接链接正常构建的 Driver。

两配置均验证：

- 同一主线程连续 8 次创建/销毁 Driver，每次 glGenQueries 和 glDeleteQueries 的 65,536 个名称逐项相等，恰好删除一次，TLS 归零。
- 活 TLS 上再次创建被拒绝且原指针不变；正常退出后的重复销毁不执行第二次 GL 删除。
- 8 次固定 Render owner 创建/退出，Main TLS 保持空，查询生成/读取/删除均在 owner 且当前上下文非空，finalize 解除上下文前 TLS 已清理。
- 注入 GpuCtx 查询池生成后的初始化失败，以及 profiling 成功后的 Driver 初始化失败；两者都回收完整名称池、传递异常并清空 TLS。尚未成功构造的 GpuCtx 不执行最终 Collect，已完成 profiling 初始化的 Driver guard 执行一次退出收集。

Debug/Release 完整解决方案零警告、零错误；两配置 --render-thread、--imgui-lifetime、--editor --command-list --editor-ui --product-loop、--game-frame --product-loop 回归通过。保留队列背压/GPU 在途/失败、32 组 ImGui 图像和代际释放、18 组 Editor UI、动态场景/输入/拾取和实际产品退出验证。

## 实际 Tracy 连接

独占运行 Release 专项的 --capture 模式，启动本机 Tracy capture 后才创建 Driver。16 次正常生命周期各写入最后一个 GPU zone，且不调用正常帧完成收集；GL hooks 确认每次析构读取两个时间戳。测试与 capture 进程均退出 0。

tracy-csvexport 成功解析采集文件，Collect 共 17 次，对应 16 次正常退出和 1 次 profiling 初始化成功后的 Driver 构造失败。GPU 初始化中途失败没有发布可收集上下文。这证明尾部采样回收协议；此处没有声称已实现产品 Game/Render 交叠或得到性能收益。

证据：Build/JobsValidation/msbuild-{debug,release}-gpu-profiling-final.log；Skinning/<配置>-gpu-profiling/result.json 和各回归模式的 result.json/current 日志。实际采集为 Build/JobsValidation/tracy-gpu-profiling-release.tracy，配套 -harness.log、-capture.log、-result.json、-Collect.csv。--capture 可在生成的专项 exe 后追加输出目录与 --capture 参数，由外部本机 capture 工具连接；常规回归无需连接 Tracy。

## 下一步

资源/通知/ImGui/Tracy 的已完成归属协议还需接入真实产品循环。下一步处理 Context 中的 Driver/RenderThread 所有权，将资源创建/上传与必要 GPU 元数据操作送到固定 owner，拆开 ImGui 初始化与主线程 CPU 构建、窗口平台事件与交换，并核查录制中的 GPU 埋点/查询。随后去除产品逐帧同步等待，引入有界多帧输入/结果与失败恢复，进行窗口/场景/退出压力、严格图像和真实交叠/CPU/GPU/队列/输入延迟验收。阶段 2/3 整体目标仍未完成。
