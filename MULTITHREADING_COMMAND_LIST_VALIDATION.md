# 阶段 2 第八增量：拥有资源的帧命令序列

日期：2026-09-06。前置提交：c82d4fa（目标配置、viewport、clear）。

后续：本文记录第八增量验收时的状态。动态反射/曝光历史的取消、失败和后继失效重建已在第九增量补充，见 MULTITHREADING_TEMPORAL_HISTORY_VALIDATION.md。

## 实现范围

RenderCommandList 是可移动、不可复制的命令容器。生产者按调用顺序追加拥有资源的操作，Seal 后只能消费一次；状态为 RECORDING、READY、EXECUTING、COMPLETED、FAILED 或 CANCELLED。执行成功后释放操作，执行异常停止后续操作、记录成功执行的前缀数量并进入 FAILED，不能重试同一列表。取消未执行列表释放捕获。Append 分配失败会使录制失效。

ABaseRenderer 提供 BeginCommandRecording、FinishCommandRecording、CancelCommandRecording、SubmitRenderOperation。Begin 在帧外调用，Finish 在 EndFrame 后转移序列所有权。CompositeRenderer 要求开启 retained draw packets，并在 BeginFrame 前检查启用的 feature/pass 明确 opt-in；即时基类 DrawEntity 不允许绕过序列。Begin/Draw/End 准备失败会使列表失效，EndFrame 可清理 CPU 帧状态，失效列表不能封口。

已纳入顺序的操作包括 target/viewport、clear、引擎相机/模型/用户矩阵、灯光分配/上传/覆盖/恢复、蒙皮 identity/矩阵上传、反射常量、完整材质/几何 draw 及帧末缓冲/目标恢复。EngineBuffer、Skinning、Reflection 的录制操作不再捕获 feature 的 this，改为持有 GPU buffer 和值载荷；draw 操作也不捕获 renderer，通过消费时传入的 Driver 执行。

SceneRenderer 的阴影、反射、Opaque、Transparent、UI geometry 和固定后处理 pass 已声明支持。反射完成面的 mipmap、自动曝光的亮度 mipmap 都在对应 draw 后提交到同一序列。反射的 CPU 捕获迭代状态仍在准备时推进，GPU mipmap 操作持有实际 cubemap 对象。

新增动态场景验收发现 CPostProcessStack::SetAutoExposureSettings 使用 Settings 类型作为键，而消费按 Effect 类型查找，导致设置没有生效。已改为 AutoExposureEffect 键并加入读取校验。此前即时重复运行也因渐变曝光的墙钟时间不同产生少量像素差异；修复后保持严格逐字节对拍，没有放宽阈值。

## 使用及边界

~~~cpp
renderer.SetDrawRecordingEnabled(true);
renderer.BeginCommandRecording();
renderer.BeginFrame(frame);
renderer.DrawFrame();
renderer.EndFrame();
auto commands = renderer.FinishCommandRecording();
// 已捕获的 GPU 操作不再读取上面的场景、renderer、feature、材质或目标包装对象。
commands.Execute(driver);
~~~

- 上例仍在同一 GL owner 上准备、消费和释放。资源创建/resize、部分生产端 GPU 元数据访问、Tracy GPU 埋点尚未统一迁移；不是任意场景都可以在无 GL 上下文的 worker 上执行完整 Begin/Draw/End。
- COMPLETED 表示 CPU 已完成命令调用，不表示 GPU fence 已完成。列表无跨线程同步、背压或窗口/上下文持有；必须按既定提交顺序消费涉及同一动态资源的列表。
- 命令拥有存储及数值，动态纹理内容依靠列表内的写入→mipmap→读取顺序；没有为所有 GPU 内容建立不可变多帧副本。
- Cancel/录制失败保证不提交剩余 GPU 操作，不回滚已经发生的 CPU material singleUse、反射捕获迭代、曝光历史选择等准备状态。整帧取消/失败后的这些生产状态恢复协议还需在正式帧交接前闭合，不能把单个 pass 的恢复用例推广为所有动态 pass 的事务回滚。
- 编辑器 DebugShape 的 frame events、调试反射显式 UBO/纹理覆盖、部分 debug pass、picking/readback 和 ImGui draw data 尚未 opt-in。--command-list 不接受 --editor，编辑器本轮仅验证原同次 packet 路径。
- 自定义 pass/feature 必须显式 opt-in，并保证 GPU 操作通过 SubmitRenderOperation 提交且捕获拥有的数据；不能借此捕获不受保护的场景对象。
- 默认产品仍使用原即时模式；帧序列通过上述 API 显式启用。阶段 2 和阶段 3 尚未全部完成。

## 验收

先完整构建 Debug/Release，再运行（可追加 --vs 指定 VS2022）：

~~~bat
node Tests\Skinning\run-validation.mjs Debug --command-list
node Tests\Skinning\run-validation.mjs Release --command-list
node Tests\Skinning\run-validation.mjs Debug --draw-inputs
node Tests\Skinning\run-validation.mjs Release --draw-inputs
node Tests\Skinning\run-validation.mjs Debug --editor --draw-packets
node Tests\Skinning\run-validation.mjs Release --editor --draw-packets
~~~

输出位于 Build/JobsValidation/Skinning/<配置>-command-list/current/。新入口复用 frame-inputs、target-state、packet 和 Mannequin 验证，旧基准副本追加当前命令成员以保持 ABaseRenderer 实例布局，旧绘制逻辑与基准提交不变。

- 每配置 8 组独立 renderer/scene/feature/material/geometry/target 全部销毁后消费图像与同次 packet 精确一致；GL 拦截验证收集期间没有 draw、buffer allocation/upload、clear 或 framebuffer bind（用例资源事先创建）。
- 列表异常停止后缀、前缀计数、重复执行拒绝、取消释放、移动后源不可执行通过。未迁移 pass 在帧开始前拒绝；录制失败不可封口，丢弃后下一帧可恢复；Append 指定分配失败导致 FAILED。
- 普通真实 Mannequin/Standard/SceneRenderer 的 24 帧即时/延迟图像精确一致。
- 额外真实动态场景：128 分辨率方向光阴影、16 分辨率实时六面反射及 mipmap、自动曝光（关闭渐变以固定时间输入）、三层 Bloom、Tonemapping、FXAA。即时重复、同次 packet 与延迟整帧的 24 帧输出逐字节一致。此处没有把重复即时运行的差异作为误差容限。
- 原目标/帧常量/灯光/packet/资源寿命回归通过。旧 --draw-inputs 的材质状态、缓冲调用顺序、几何和蒙皮图像精确一致。
- Debug/Release 原 Editor 三视图同次 packet 模式、Edit/Play/Pause/8 次单步/恢复/Stop、24 组视图顺序及 8 次切场景/resize 回归通过；不声称已验收延迟 Editor 全帧。
- 完整 Debug/Release 构建 0 警告、0 错误；产物复制按程序、模型、shader 共 10 项核对。构建包含用户既有工作区修改，本阶段仅提交本轮文件。

证据：Build/JobsValidation/msbuild-{debug,release}-command-list.log（全布局重建）、msbuild-{debug,release}-command-list-final.log（最终增量），command-list-{debug,release}.log、command-list-regression-{debug,release}.log、command-list-editor-{debug,release}.log、command-list-build-artifacts.json，以及 command-lifetime-frames.bin、immediate/delayed、dynamic-immediate/dynamic-repeat/dynamic-packet/dynamic-delayed 的图像。诊断阶段 command-list-diagnostic-debug.log 和早期失败运行不作为最终通过证据。

本轮未新增 ASan、Tracy 采集或独占性能测量；验证中 NVIDIA buffer migration 性能提示不等于 GL 错误。列表增加值复制、函数对象及资源持有开销，未声称整帧收益。

## 继续工作

先闭合动态 pass 的取消/失败准备状态，再迁移 Editor debug/picking/readback、ImGui 稳定 draw data 与 frame completion。随后阶段 3 引入固定 Render owner、有界帧队列、资源创建/上传/销毁归属以及 Game/Render 交叠；不得用整帧等待代替真实交叠，仍需场景/窗口/退出压力与 Tracy、延迟数据验收。
