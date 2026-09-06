# 阶段 2 第十增量：编辑器调试与拾取命令

日期：2026-09-06。前置提交：e14df59（动态历史失效与重建）。

## 实现范围

Grid、Debug Cameras、Debug Reflection Probes、Debug Lights、Debug Actor 和 Picking pass 显式支持整帧命令录制。DebugShape 的帧参数准备只修改 CPU 材质；线段、边界、相机视锥、碰撞体、描边和 Gizmo 的绘制继续通过拥有资源的 draw packet 收集。未知派生 pass/feature 不自动获得许可。

调试反射的显式 PrepareProbe/BindProbe 在整帧模式下不再上传或绑定组件 UBO。绑定命令捕获共享 buffer 和反射常量值；DebugModel 新增携带额外 owning texture 的辅助函数，将探针 cubemap 纳入每个材质快照。调试假灯光的 fallback bind 也通过序列提交。

Picking pass 在录制模式下生成独立的 Readback 对象：包含像素存储、尺寸、场景运行期身份、Actor ID 到 GUID 的快照、Gizmo 所属 Actor/操作类型，以及对应列表的完成标记。GPU 命令只持有 readback 存储和 framebuffer 快照，不读取 Scene、Actor 或 PickingRenderPass。读回发生在 picking draw 之后。结果只有整份命令序列成功结束才可解析；读回完成后其他命令抛错、取消和未执行即析构都不会发布有效结果。

解析在主线程查询当前场景，拒绝不同场景身份、已删除/替换 Actor、越界坐标，以及失去所属 Actor 的 Gizmo。Scene 的身份不随内存地址或序列化 ID 复用，反序列化也更新身份；禁止浅拷贝拥有 Actor 的 Scene。SceneView 还检查当前视图尺寸及所选 Gizmo Actor/操作，避免把旧结果应用于新选择。

Framebuffer::Snapshot::ReadRGBA8 读取紧密排列的完整 RGBA8 图像，持有原附件存储；它隔离并恢复 GL_READ_FRAMEBUFFER、PIXEL_PACK_BUFFER、pack alignment/row length/skip 状态。此处使用同步 GPU 读回，尚未引入 PBO 异步下载。

AView::RecordRenderCommands 收集一个视图并返回可移动列表；异常清理帧状态并取消未交接的列表。SceneView 的拾取交互分离到主线程 CompleteRender；即时 Render 保留原交互调用位置，录制时由调用方在成功消费后调用。Driver::OnFrameCompleted 随 GPU 命令序列执行，不提前在生产端执行。该名称仍不代表 GPU fence 或窗口呈现完成。

## 入口与边界

~~~cpp
auto commands = view.RecordRenderCommands();
commands.Execute(driver); // 当前仍在同一个 GL owner 上消费
view.CompleteRender();    // 主/UI 线程，仅在成功消费后调用
~~~

默认产品仍使用即时 Render；新 API 是阶段 3 帧交接的准备入口。资源创建/resize、GPU profiling 与部分 GPU 元数据访问仍需 GL owner，不能据此把全部准备放到任意无上下文线程。

完整拾取图像只在启用 picking pass 的录制帧生成，使用 width × height × 4 字节及 Actor 映射。结果发布是 acquire/release 的 CPU 完成观测，不是可无限排队的帧队列。调用方须对在途帧和读回内存设界；阶段 3 仍需量化此处的下载成本并评估 PBO/按需读回。没有声称帧率收益。

## 验收

先完整构建 Debug/Release，运行 Tests/Skinning/run-validation.mjs <配置> --editor --command-list，可追加 --vs。输出位于 Build/JobsValidation/Skinning/<配置>-command-list-editor/current/。

新增测试使用真实 DebugSceneRenderer 和编辑器资源，覆盖蒙皮物体、camera、light、reflection probe、碰撞体、描边、Grid 和三个 Gizmo 操作。6 组即时/整帧命令的颜色与拾取图像逐字节比较，其中包含 renderer、pass、feature、材质和场景销毁后消费。解析实际 actor/gizmo 像素，并验证场景替换、Actor 删除和边界坐标。

另检查取消、读回后执行异常、未消费析构不发布结果；拦截 glReadPixels，确认录制期间没有 GPU 读回；污染 PBO/pack 布局/读目标后读取 owning 快照，验证像素正确且状态恢复。

真实 Scene/Game/Asset View 每次对比即时与延迟整帧图像，执行既有 Edit/Play/Pause/单步/恢复/Stop、视图顺序、切场景与 resize 测试。为精确控制时间输入，Asset View 对拍关闭渐变曝光并每次捕获完整六面反射；不是宽泛像素容差。

## 验收结果

- 最终 Debug/Release 全解决方案构建均退出 0，0 警告、0 错误；10 项程序/模型/shader 复制核对一致。
- 两配置 --editor --command-list 均通过：各 6 组调试/拾取精确对拍，实际解析 390 个 Actor 像素与 196 个 Gizmo 像素；取消、读回后异常、析构放弃、源对象销毁、替换场景、删除 Actor、越界和 GL pack 状态用例通过。
- 两配置真实 Scene/Game/Asset 的逐次即时/延迟图像比较与原模式测试通过；24 组视图顺序对拍、11 次动画变化、8 次延迟切场景/resize，项目批次统计一致。隐藏窗口，没有桌面输入操作；本轮没有模拟真实鼠标点击/拖动的跨帧输入延迟，仍属阶段 3 验收范围。
- 两配置 --command-list 的原场景、目标、frame input、packet、源对象寿命和动态历史失败恢复回归通过。
- 两配置 --draw-inputs 旧源码/当前材质状态、GL 上传调用与几何/蒙皮图像逐字节一致；--editor --draw-packets 的即时编辑器模式/视图/场景/尺寸回归通过。
- git diff --check 通过；本轮未新增 ASan、Tracy 采集或独占性能结论。

证据位于 Build/JobsValidation/：msbuild-{debug,release}-editor-commands.log（初始构建）、msbuild-{debug,release}-editor-commands-final.log（最终构建）；editor-commands-final-editor-{debug,release}.log、editor-commands-final-scene-{debug,release}.log、editor-commands-legacy-{debug,release}.log、editor-commands-immediate-editor-{debug,release}.log、editor-commands-build-artifacts.json。初轮 editor-commands-debug.log 不替代最终回归。像素证据包含 <配置>-command-list-editor/current/editor-command-frames.bin 及原场景命令测试图像。

## 下一步

ImGui draw data/纹理拥有关系与正式 frame completion 尚未完成。随后阶段 3 统一资源创建、上传、读回、销毁到固定 Render owner，接入有界队列和 Game/Main 与 Render 的实际交叠；继续验证窗口、交互、取消/退出及 Tracy、GPU/CPU/队列/输入延迟。阶段 2、3 均未全部完成。
