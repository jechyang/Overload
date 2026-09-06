# 阶段 3 第五增量：ImGui GPU 代际与 loader 归属

日期：2026-09-06。前置提交：b6ca2b9（纹理注册表与主线程面板）。

## 实现

引擎定制 ImGui OpenGL 后端改为复用 baregl/GLAD dispatch table，Driver 在使用后端前完成唯一 GL 上下文的初始化。移除后端独立 imgl3w 初始化/关闭以及 g_BackendInstances/g_DeviceGenerations 共享计数；CPU 后端或旧快照析构不再修改 loader 状态。Premake 显式加入 ImGui 对 baregl 的 include/link 依赖。这是当前引擎 Windows/OpenGL 后端集成，不声称保留独立于引擎的通用 GLES/WebGL 构建支持。

DeviceGeneration 的 program、vertex/index buffer 各自持有 ResourceLifetime；创建前准备回收记录，获得 GL 名称后立即 Adopt。后端实际 atlas 纹理在 BackendUserData 中持有自己的回收记录，销毁时只释放该记录并清除标识。记录由 managed owner 创建时，主线程/backend/frame 的最后释放可在没有 GL 上下文时进行，删除由已有 DeletionQueue 路由到 owner。逻辑字体 ID 没有原生记录，不再被当作 GLuint 误删。

临时 vertex/fragment shader 使用同样的 RAII 记录；编译/链接失败回收已创建的对象，成功的新设备代际才发布。失败前后恢复 texture、array buffer、unpack buffer 与 VAO 状态，保留此前已发布的设备代际。没有启用 DeletionQueue 的 legacy 创建仍要求原 GL owner 最后释放。

CaptureRenderState 拒绝已关闭的 managed 代际；DrawData::Execute 在任何惰性字体上传之前检查代际，retained 后端入口也检查。资源域关闭后的旧 UI 帧被拒绝，后续 CPU 释放不会删除新域中的同名对象。此检查依赖已有“停止生产、排空 GPU、关闭域”的协议，不支持边执行边关闭上下文。

## 验证

重新生成 Premake 工程并完整构建 Debug/Release，均零警告、零错误。运行入口（按需追加 --vs）：

~~~text
node Tests/Skinning/run-validation.mjs Debug --imgui-lifetime
node Tests/Skinning/run-validation.mjs Release --imgui-lifetime
~~~

两配置均通过：

- 真实 RenderThread 创建后端/纹理及拥有型 UI 快照；主线程无当前 GL 上下文地执行 backend/ImGui context 销毁，再由 owner 回放。32 组大网格、裁剪、文本及图像与即时输出逐字节一致，之后主线程最后释放快照并由 owner 实际回收。
- 直接后端 atlas 纹理、program 和两个 buffer 的主线程最后释放；GL 创建/删除 hook 验证 owner 归属，glIs* 检查最终没有活名称。
- owner 创建/渲染新 ImGui backend 时，另一线程最后释放已执行的旧快照；没有旧后端计数或 loader 状态需要跨线程修改。
- 注入 vertex 编译及 program 链接失败，检查返回失败、旧代际仍可捕获和精确回放、PBO unpack 绑定恢复及临时 shader/program 无泄漏。日志中的预期编译/链接错误来自此注入。
- 在外部保留尚未执行的 DrawData 时关闭资源域，之后无上下文 Execute 被拒绝；直接拦截 glCreateTextures，确认没有先执行惰性字体上传。跨新域晚释放旧 CPU 快照不触发原生删除，新图像仍精确一致。

两配置 --ui-commands、--render-thread、--editor --command-list --editor-ui --product-loop、--game-frame --product-loop 均通过。保留 UI 字体代际/源销毁、Render 背压/GPU 在途/失败、18 组 Editor UI、场景/拾取/动态姿态/退出，以及实际 Game 产品回归。

证据：Build/JobsValidation/msbuild-{debug,release}-imgui-lifetime.log；Skinning/<配置>-imgui-lifetime/result.json 与 current/compile.log、run.log。其他模式使用各自 result.json/current 日志，批次摘要为 Build/JobsValidation/imgui-lifetime-<配置小写>--<模式>.log。所有测试使用隐藏窗口；没有新增性能收益、Tracy Game/Render 交叠或形式化无竞争结论。

## 仍需完成

这一步闭合了 managed ImGui GPU 最后释放和重复 loader 生命周期，并没有把所有 ImGui API 变成并发 API。Init/NewFrame/CreateDeviceObjects 的 GL 操作仍须安排在 owner；读取/写入同一 ImGui context 的调用仍应串行且遵守平台主线程要求，retained 回放独立于源 context。产品资源创建/上传、CPU 录制期间 GL 查询、窗口平台操作和 SwapBuffers 仍需统一接入。

TracyOpenGL.hpp 的 GpuCtx 当前为每个 Driver 分配 64×1024 查询对象，GpuCtx/TLS 没有显式销毁路径。下一步在 GL owner、所有 GPU zone 结束后收集并释放查询与 TLS，验证重复初始化/退出；随后完成生产 GL 调用归属和 Editor/Game 多帧队列/输入结果，验收实际双线程交叠与性能。阶段 2/3 整体目标尚未完成。
