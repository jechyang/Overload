# 阶段 3 第一增量：固定 Render owner 与有界帧队列

日期：2026-09-06。前置提交：c449c81（产品帧循环与逐帧交互）。

## 实现范围

新增 OvRendering::Threading::RenderThread，内部持久 std::thread 创建并拥有 Driver。初始化回调在该线程附着此前已从主线程解除的 GL 上下文；Driver、Tracy GPU 上下文也在该线程初始化。所有接受的帧、资源任务和清理在这个固定线程执行。Driver 销毁后，finalize 回调解除上下文，再 join；窗口和平台事件仍由调用方在主线程管理。

这是可直接运行真实 GL 工作的基础设施，尚未替换 Editor/Game 产品循环。产品仍使用阶段 2 的同线程 Update/PostUpdate/Wait；不能据此宣称整个阶段 3 完成。

## 队列与资源协议

- 默认最多两个接受的帧，计数包含待执行、CPU 执行和 GPU 在途，直到捕获释放并退役才释放容量。Submit 只等待容量，不等待该帧完成。正常消费用零超时 glClientWaitSync Poll；无队列工作但存在在途 GPU 时通过条件变量短时等待。
- 资源 Invoke 使用独立容量（默认 64，包含排队和执行中的任务），与帧进入同一个 FIFO。资源任务可继续在帧入口关闭后使用；owner 内嵌 Invoke 直接执行，owner 调用 Submit/WaitIdle/Shutdown 拒绝，以避免等待自身。
- Invoke 的 callable 不保留在 future 的共享状态里，避免主线程销毁 future 时成为 GPU 捕获的最后持有者。返回值必须是拥有型值，调用方仍负责其最终释放线程；引用返回在编译期拒绝。非可移动赋值的 RenderFrame 通过独立传输容器支持。
- Submit 先分配队列节点，再移动帧；关闭/失败拒绝时帧参数保持原状。拒绝后的拥有型帧通过 Discard 在 owner 取消。关闭帧入口后先结束生产者、释放外部资源，再 Shutdown。Shutdown 关闭剩余任务入口、唤醒等待容量的调用者、排空已接受工作并 join；对象析构前必须结束所有外部调用，不支持两个并发 Shutdown。
- 用户回调不得等待依赖被阻塞主线程才能完成的操作。队列不自动把任意 baregl、Logger、ResourceTracking 或 UI 调用变成线程安全接口，也不允许多 RenderThread 并行使用当前全局 GL/debug 状态。

## 失败与发布

启动异常经过 owner 清理并 join 后传回构造方；普通资源任务异常经 future 传回调用者。帧执行/呈现失败关闭帧入口，取消尚未执行的帧；已经提交 GPU 的帧仍由 owner 排空。资源清理任务继续可用。Shutdown 后 RethrowIfFailed 仍可查询帧或收尾错误，析构不吞掉线程而脱离运行。

GPU 退役按提交顺序观察：前帧的延迟错误出现之前不会发布后继成功。新私有 RenderFrame::InvalidateSubmitted 将未退役后继标记失败并失效其 speculative history，同时保留 fence 和捕获到 GPU 退役。已经发布的子命令 CPU completion 不重写，调用方必须以整帧 GPU completion 作为最终成功依据。队列失败采用保守策略，已经在途的其他未退役帧也失效；恢复需要排空并重新建立队列/帧数据。

## 验证入口和结果

先重新生成工程并完整构建 Debug/Release，再运行，可通过 --vs 指定 VS2022：

~~~text
node Tests/Skinning/run-validation.mjs Debug --render-thread
node Tests/Skinning/run-validation.mjs Release --render-thread
~~~

输出：Build/JobsValidation/Skinning/<配置>-render-thread/result.json 及 current/compile.log、run.log。隐藏窗口由主线程创建，测试线程附着真实 OpenGL 4.5 上下文；未操作桌面输入。

专项覆盖：

- 八组真实 ImGui 大网格图像，即时与队列回放 RGBA 逐字节一致；源 ImGui context/backend 销毁后消费，纹理创建/上传、绘制、读回、交换与销毁在固定 owner。检查主线程无当前 GL context，并拦截纹理删除及 fence 等待的线程与零超时参数。
- 两帧 CPU 提交后同时保留在 GPU 在途状态；第三个生产者阻塞到 GPU 退役释放容量。受控条件变量验证主线程在 Render 执行期间可继续提交；Close 唤醒阻塞生产者，拒绝不消费其帧，再由 owner Discard。
- 命令失败、呈现失败、GPU 延迟失败，分别检查尚未消费后继取消、已提交后继失败和历史失效、入口关闭与后续资源清理。
- 初始化失败清理、资源任务异常、owner 嵌套 Invoke、自等待拒绝、仅一个资源任务容量下的 Shutdown 唤醒及排空、上下文解除和退出后拒绝。

Debug/Release 专项、原 --render-frame 回归，以及 --editor --command-list --editor-ui --product-loop 实际应用/三视图/拾取/Gizmo 回归通过。最终完整解决方案日志为 Build/JobsValidation/msbuild-{debug,release}-render-thread-final.log。所有时间仅用于测试超时或确定性阻塞观察，没有用固定等待推算帧率。

## 下一增量

1. 产品资源调用边界：ResourceTracking 当前是未同步的裸纹理指针注册表，Logger 同步触发 Console/UI；必须先改为安全的主线程通知/元数据协议，才能在产品里开启 Render owner。
2. 收尾 GL 依赖的录制/resize/Shader/ImGui 初始化；Window::SwapBuffers 仍混合光标更新和交换，需要拆分平台与 GL 工作。核查 Tracy GPU TLS 的长期生命周期，当前 vendored Tracy 不自动释放 GpuCtxWrapper 指针。
3. 将产品单个 pending frame / latest Interaction 改为有界的逐帧输入与结果集合，接入队列并保留即时对照，不能仅删除保护或每帧 Invoke/Wait 整帧。
4. 完成真实 Game 模拟/Render 交叠、窗口/场景/退出压力，采集 Tracy CPU/GPU/队列/输入延迟。当前测试是独立真实 GL 消费者；没有实际产品双线程性能或全引擎无竞争证明。
