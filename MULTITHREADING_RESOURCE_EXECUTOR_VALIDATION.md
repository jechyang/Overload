# 阶段 3 第七增量：资源调用与 UI 后端分派

日期：2026-09-06。前置提交：232cb5b（Tracy GPU 查询/TLS 收尾）。

## 执行协议

baregl::ResourceExecutor 是资源生产线程的显式作用域。调用方提供同步 dispatcher；正式 RenderThread::Invoke 执行并完成整个操作后，才返回主线程，包括异常传递和拥有型返回值。源字节、字符串、span 和参数引用因此只借用到调用返回。未绑定作用域时保持原来在当前线程执行的路径；Render owner 没有生产者绑定，嵌套资源操作直接执行。

ResourceLifetime 在 owner 上捕获当前回收域并预登记记录，随后 native 构造在相同 owner 创建名称并 Adopt。主线程构造的包装对象因此也能在无上下文时最后释放，关闭域后失效规则与既有资源生命周期一致。作用域按线程和栈顺序销毁，dispatcher 及 RenderThread 必须覆盖全部生产者调用；关闭后新资源调用由 RenderThread 拒绝。

Buffer、Texture、Renderbuffer、VertexArray、ShaderStage、ShaderProgram、Framebuffer 的创建、分配、上传、绑定、查询与读回进入该执行路径。FBO 快照仍在 CPU 捕获，GetID/Bind/ReadRGBA8 的惰性实现通过 owner 执行。GL 状态相关的 bind/draw/unbind 序列应整体 Invoke，不能假定两次同步调用之间没有其他已提交帧。

ShaderProgram 的 uniform 写入改用 glProgramUniform，直接指定自身 program。即使上一帧改变当前绑定，也不会把参数误写到另一 program。查询仍按目标 program 执行。

UIManager 的 OpenGL 初始化、录制模式准备和即时绘制，以及 Canvas 的 OpenGL NewFrame 通过同一作用域执行。ImGui/GLFW 的平台初始化、窗口事件和 CPU 布局留在 Main；资源回收沿用上一增量的 GPU 代际持有与回收域。同步后端调用期间 Main 等待，拥有型 UI 帧执行不借用其 ImGui 上下文。

这不是任意并发修改资源的许可：CPU 描述符/附件引用仍需生产者顺序访问，旧域对象不能带到新域继续操作，未绑定的其他工作线程不能调用 GL。资源同步入口也不是每帧等待 GPU 的新机制；正式帧仍通过 RenderThread 的有界提交和 GPU 回收协议处理。

## 验证入口

先重新生成项目并完整构建 Debug/Release，再运行（按需追加 --vs）：

~~~text
node Tests/Skinning/run-validation.mjs Debug --resource-executor
node Tests/Skinning/run-validation.mjs Release --resource-executor
~~~

新专项链接真实引擎库，主线程创建隐藏窗口后解除 GL 上下文，固定 RenderThread 持有唯一 Driver。GLAD hooks 检查实际资源调用线程及当前上下文，Main 通过正常资源类和真实 UIManager/Canvas 发起操作。

Debug/Release 均通过以下检查：

- 每配置 16 轮主线程七类资源操作；buffer 上传字节和两种 FBO 读回逐字节相同，resize 后旧目标快照保留尺寸和像素。
- 2D/image binding、cubemap、mipmap、可变纹理分配/resize/上传；shader 编译诊断与成功重编译、program 链接/查询，以及已提交帧绑定另一 program 后的定向 uniform 写入。
- 真正的 Main UIManager/Canvas/Image/ButtonImage：即时与拥有型执行 8 组精确像素一致；销毁全部主线程源 UI 后回放仍一致并释放纹理。
- 每配置 3,825 次受监测的 GL 调用全部位于固定 owner，1,462 次生产者提交；嵌套 owner 不二次派发，move-only 返回值与右值 callable、异常、空 dispatcher 拒绝、嵌套作用域恢复、关闭后句柄失效及新建拒绝均通过。
- 原 --resource-lifetime、--render-thread、--imgui-lifetime、--editor --command-list --editor-ui --product-loop、--game-frame --product-loop 均通过，保留后台最后释放/在途背压/失败、32 组 ImGui 图像、实际编辑器动态视图与交互、18 组编辑器 UI、游戏帧循环及应用退出。

两配置完整解决方案均为 0 警告、0 错误。最终构建日志：Build/JobsValidation/msbuild-{debug,release}-resource-executor-final.log。各模式证据：Build/JobsValidation/Skinning/<配置>-<模式>/result.json 与 current/compile.log、run.log。

初次专项的测试 blit 将 8×4 附件缩放到多采样默认目标，触发尺寸限制；夹具改为同尺寸 blit 后，扩展专项与上述最终回归均通过。本阶段不使用并发构建/回归期间的耗时作为性能数据。

## 下一步

本增量没有修改产品 Context 或 Application 的线程配置。Editor/Game 仍是同线程拥有型帧循环；阶段 2、3 尚未全部完成。下一步迁移录制期间的 Tracy GPU scope、产品 Context/Driver 所有权及窗口呈现，接入多帧完成/交互集合，并验证实际 Game/Render 交叠、资源更新顺序、输入延迟和退出。不能据此宣称已有整帧性能收益。
