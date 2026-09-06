# 阶段 3 第二增量：后台日志与主线程 UI 通知

日期：2026-09-06。前置提交：974b767（固定 Render 线程与有界帧队列）。

## 已接入的产品行为

Logger::Log 在任意生产线程同步写入指定输出目的地，但不再从后台线程直接执行 Console/UI 回调。消息按值进入 LogEvent；Editor 与 Game 的 PreUpdate 调用 DispatchPendingEvents，在主线程分发后台消息。主线程直接记录日志仍会立即分发当时的消息批次。真实 RenderThread 初始化 Driver 和执行资源任务时产生的日志遵循同一协议。

Console 原订阅代码与析构退订接口保留。Logger::LogEvent 改用 MainThreadEvent<LogData>，旧的通用 Event 类型没有修改。这个变化不会使 ResourceTracking、Actor 事件或任意 UI 方法自动线程安全。

## 主线程事件协议

MainThreadEvent 在构造线程绑定订阅和分发，Logger 的静态事件在应用主线程初始化。Post 可以由任意线程调用，只复制拥有型 CPU 数据；Invoke 在 owner 调用时顺便分发，在后台调用时仅排队。后台订阅/退订/分发会抛出逻辑错误。回调不在事件互斥锁内执行，也不在 Logger 输出锁内执行。

每条消息记录入队时的最高订阅序号：取消订阅后尚未分发的消息不会访问旧对象，新订阅者也不会收到旧订阅时期的消息。回调可以删除自身/其他回调；回调异常被保留，在当前批次其余可用回调处理后抛回主线程。每次泵处理进入时已有的有限批次，嵌套消息留给下一次泵，避免回调再次记录日志造成无限递归。回调捕获的析构在 owner 且在锁外执行。

默认积压最多 4096 条 UI 通知，超过时省略最旧通知并计数；主线程报告省略数量。Console/file/history 目的地的实际日志不因 UI 积压被省略。此队列仅用于 CPU 数据，不允许直接放入要求 GL-owner 最后释放的纹理快照。

## 输出同步

Logger 对 handler 注册表、输出顺序和事件入队做串行保护；Console、File、History handler 的共享输出状态另有各自互斥锁，直接使用 handler 时也得到同步。FileHandler::GetLogFilePath 与 HistoryHandler::GetLogQueue 返回拥有型副本，避免把可变全局状态的引用交给并发读者。文件目的地切换在锁内关闭旧流并重新打开新路径；路径解析支持本机路径分隔符，不再从字符串尾部无界向前查找斜杠。

这些锁不包围 UI 回调，也没有给整个 Game/Render 帧加锁。没有修改场景更新、帧提交或 GPU fence 时序。

## 验证

先完整构建，再运行（按需追加 --vs）：

~~~text
node Tests/Skinning/run-validation.mjs Debug --log-dispatch
node Tests/Skinning/run-validation.mjs Release --log-dispatch
node Tests/Skinning/run-validation.mjs Debug --editor --command-list --editor-ui --product-loop
node Tests/Skinning/run-validation.mjs Release --editor --command-list --editor-ui --product-loop
node Tests/Skinning/run-validation.mjs Debug --game-frame --product-loop
node Tests/Skinning/run-validation.mjs Release --game-frame --product-loop
~~~

Debug/Release 结果均通过：

- 六个并发生产者共 1536 条 history 消息，与主线程事件逐条顺序一致，无丢失/重复，保持各生产者的程序顺序；同时读取拥有型历史副本。
- 5000 条突发消息保持 4096 条通知容量，主线程准确报告 904 条省略通知；没有递归警告风暴。另验证 192 条多线程 file 记录完整写入。
- 通用事件拥有源字符串、限定线程、订阅代际、回调自移除/移除他人、异常恢复和有限嵌套分发。
- 真实隐藏 GL 窗口与固定 RenderThread：Driver 初始化及资源任务日志在后台不运行订阅者；主线程泵后准确分发，排队期间退订后不触及旧对象或新订阅者。
- 实际 Editor Console 在 worker 记录消息后保持不变，只在 Editor::PreUpdate 后增加对应消息；实际 Game::PreUpdate 也将 worker 日志交给主线程订阅者。
- 保留实际 Editor/Game Application.Run、模式/配置切换、准备后退出；Editor 三视图、八帧动态姿态/图像、拾取/Gizmo/材质拖放和 18 组 UI 对拍；Game 八帧动态与六组整帧对拍。
- 两配置全解决方案构建零警告、零错误；专项与应用测试均为隐藏窗口，没有桌面输入操作。

证据：Build/JobsValidation/msbuild-{debug,release}-log-dispatch-final.log；Skinning/<配置>-log-dispatch/result.json，以及 <配置>-command-list-editor-ui-product-loop / <配置>-game-frame-product-loop/result.json。各 current 目录包含编译和运行日志。本增量没有新的性能收益或形式化无竞争证明。

## 下一步与剩余阶段 3

ResourceTracking 仍持有裸 Texture 指针，创建/销毁事件仍即时触发。纹理创建事件早于 Allocate，元数据会继续变化；TextureDebugger 还直接借用包装对象。不能只将这些裸指针事件放进新队列。需要稳定的元数据/代际协议，并明确 UI 保留的纹理 storage 最后释放如何回到 Render owner；当前 baregl storage 的最后析构直接删除 GL 对象。

接下来先闭合 GPU 资源最后释放与纹理注册表的协议，再把产品资源创建/录制中的 GL 操作路由到固定 owner，拆分窗口光标更新与交换。随后接入有界逐帧输入/结果，完成真实 Game/Render 交叠、场景/窗口/退出压力和 Tracy CPU/GPU/队列/输入延迟验收。产品目前仍采用同线程帧循环，整个阶段 2/3 目标保持未完成。
