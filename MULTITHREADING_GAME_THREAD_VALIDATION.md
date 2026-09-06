# 阶段 3 第八增量：Game 产品双线程接入

日期：2026-09-07。前置提交：10db6eb（资源生产者与 UI 后端分派）。

## 产品行为与所有权

Game.ini 可设置 render_thread=true，要求 render_commands=true（后者默认 true）。render_thread 默认 false，保留同线程命令和即时对照；独立线程 Context 内拒绝切换到即时模式。Editor 本增量仍使用同线程产品循环。

Game Context 在 Main 创建窗口、输入与平台状态，解除当前上下文后，由固定 RenderThread 创建和持有唯一 Driver。Main 安装 ResourceExecutor，再正常构建 UI、资源与场景；资源 GL 操作同步派发到 owner。公开 driver 为非拥有共享别名，实际 Driver 寿命由 RenderThread 管理。全部资源和 UI 先释放，再撤销生产者作用域、关闭 Render、销毁 Driver/上下文，最后销毁窗口和设备。

Game Update 录制拥有型帧，PostUpdate 提交到容量为 2 的在途队列，正常逐帧不 Wait。Main 轮询 GPU 退役 completion 并重抛错误；队列满时由既有 RenderThread 背压。窗口事件、游标和输入清理留在 Main，Present 仅在 Render 调用交换缓冲。Application.Run 正常结束时排空；Game 析构取消准备但未提交帧并排空提交帧，失败路径仍完成 owner 清理。

录制期间的 GPU scope 改为 CPU 命令分组。消费时创建真实 Tracy GPU scope，嵌套与异常展开遵循消费调用栈，不把活动 scope 留到 GPU 退役。失败或取消的录制不产生 GPU 查询。立即路径仍在实际 GL owner 建立 scope。

移除了三个稳态同步点：Skinning identity buffer 的尺寸检查进入消费命令；Framebuffer 相同非零尺寸 Resize 直接返回；反射面选择进入拥有型目标快照而非录制时调用 GL。真实资源变更仍按 owner 顺序同步执行。目标快照保留存储及配置，不冻结像素，不能由多个生产者任意并发修改源包装对象。

## 复现入口

完整构建 Debug/Release 后运行（按需追加 --vs）：

~~~text
node Tests/Skinning/run-validation.mjs Debug --game-thread
node Tests/Skinning/run-validation.mjs Release --game-thread
~~~

输出：Build/JobsValidation/Skinning/<配置>-game-thread/current。夹具创建已保存 SkinningProject，再链接实际 Application、Game、Context 与窗口实现。Context 副本只隔离测试项目路径和隐藏窗口；Window 副本在交换缓冲前插入读回 hook，确保比较实际将呈现的 backbuffer。Game 产品逻辑保持生产实现。

## 验收结果

Debug/Release 均通过：

- 同线程与独立 Render Context 共 32 对实际 Game 姿态、场景/UI 像素精确对拍，前 16 帧无调试 UI、后 16 帧有 UI；连续场景帧出现 15 次画面变化。首尾恰好跨一个动画周期，因此以相邻帧变化检查运动，保留完整逐帧对拍。
- 实际 GL draw/query hooks 检查唯一 owner 与当前上下文，Main 无 GL 上下文。自然运行、实际场景重载后观测到 draw 与 Main Update 交叠：最终未连接采集的一次 Debug 为 4,512 次、Release 为 3,714 次；数量随调度变化，不作为固定阈值或加速比。
- 暂时阻塞 Render 时，实际 Game 能推进两个 Update 并提交其中一帧；完成容量限制及排空检查。门闩只用于确定性协议验收，不是性能负载。
- 实际 Application.Run 经 Main 日志事件关闭并排空，4 次完整应用重建退出保留已提交成功、取消未提交帧、使旧纹理句柄失效。呈现失败在 GPU 退役后传给 Main，失败后 owner 清理可继续执行。
- Main 录制/取消无 GPU 查询；嵌套消费异常正确展开，录制异常使列表无法 Seal。
- 原 --command-list、--render-thread、--resource-executor、--editor --command-list --editor-ui --product-loop、--game-frame --product-loop 均通过，保留动态阴影/反射/曝光/后处理、资源调用、真实 Editor 视图/拾取/Gizmo/拖放及 UI 回归。
- 原 --geometry 基线/当前两配置通过：256 次有序绘制、24 帧蒙皮、64 组 CPU 几何快照；draws.bin、geometry-frames.bin、frames.bin 逐字节一致。测试基线覆盖头/实现补齐当前辅助类型和非虚函数，保持旧 DrawEntity 与虚表，避免当前静态库重复拉入 renderer 对象。初次适配的类型/链接失败已修复。

两配置完整解决方案为 0 警告、0 错误；最终构建日志 Build/JobsValidation/msbuild-{debug,release}-game-thread-final.log。各专项 result.json、compile.log 与 run.log 位于对应测试输出目录。修改均在保留既有用户改动的工作区验证。

## 连接 Tracy 的独占验证

最终 Release 可执行文件以输出目录及 --capture 启动，再连接本机 Tracy 0.11.1。没有其他引擎测试同时运行。harness 与 capture 均退出 0；验收记录 2,032 次 owner GPU 时间戳、4,109 次自然绘制与 Main Update 交叠；嵌套消费异常检查要求四次时间戳（两个开始、两个结束），实际通过。

采集 Build/JobsValidation/tracy-game-thread-release.tracy，日志为同名前缀的 -harness.log、-capture.log、-result.json。tracy-csvexport 成功解析 Game Update、Draw、Collect；199 个 Game Update CPU zone 与 223 个 Collect CPU zone 有效，源路径对应当前 Game.cpp。该采集包含初始化、图像读回、故障注入、门闩和应用重建，不能用于整帧收益或输入延迟结论。CSV 汇总也不是 GPU 耗时统计；最终性能验收需要独立稳定负载与时序分析。

## 剩余工作

阶段 2、3 整体目标尚未完成。下一增量接入 Editor Context 和多帧 interaction/completion 集合，验证旧帧结果、视图/场景替换与实际 UI 操作；随后完成窗口 resize/最小化、退出压力、默认配置决策及整帧 CPU/GPU、队列/输入延迟验收。当前只证明 Game 实际双线程协议、拥有型图像等价与可观测交叠，不声称完整产品验收或性能提升。
