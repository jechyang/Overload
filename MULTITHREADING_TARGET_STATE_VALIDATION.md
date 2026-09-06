# 阶段 2 第七增量：目标配置、viewport 与 clear 快照

日期：2026-09-06。前置提交：24cabec（相机/灯光输入）。

## 生产路径

Framebuffer::RecordTarget 在 CPU 上捕获附件 owning 句柄、层索引、尺寸及读写颜色附件选择，不保留源 framebuffer 包装对象。Snapshot::Bind/GetID 在 GL owner 上首次使用时创建独立 FBO；同一配置复用这份 FBO。附件或读写选择改变后，新的捕获使用新配置，旧快照继续有效。最终释放仍须发生在 GL owner 且早于上下文销毁。

Framebuffer::Resize 对尺寸变化创建空的新纹理/renderbuffer 存储，保留格式、采样设置和 border color；同一附件挂到多个位置时只创建一份替代存储，保留 cubemap 六个面及深度/stencil 共用 renderbuffer。候选附件集合验证通过后才发布，源 FBO 的 native ID 不变。尺寸不变时不换代，零尺寸拒绝。此前取得的 GetAttachment 借用/纹理 ID 不再代表 resize 后的新附件，调用者必须重新获取；RetainTextureAttachment 和目标快照能继续持有旧存储。旧像素不复制到新存储。

ABaseRenderer 增加 PassStateCommand（目标快照、viewport）和 ClearCommand（pass 状态、三种 clear 标志、颜色）。RecordPassState/PrepareClear 只复制 CPU 状态；ExecutePassState/ExecuteClear 恢复捕获的 GPU 状态，不改变后续录制使用的 CPU 选择。Clear 继续使用 Driver 原有默认管线、关闭 scissor、写掩码及 clear 值语义。

真实 BeginFrame/EndFrame、每个 CompositeRenderer pass 的初始目标、Blit、反射、阴影和拾取 pass 已使用 BindTarget。反射每切换 cubemap 面后重新选择目标以捕获该面。录制 DrawPacket 执行 feature/material/geometry 前先恢复目标和 viewport；独立 clear 可在源目标销毁或 resize 后回放。编辑器 AView 在 resize 后刷新 Image 的纹理 ID。

## 协议与未完成范围

- BindTarget 负责选定后续录制的 CPU 目标；在绘制期间直接调用 Framebuffer::Bind 或更改读写目标不会自动更新 renderer 选择。自定义 pass 应使用 BindTarget，并在目标配置改变后重新调用。
- Snapshot 持有对象和目标配置，**不冻结同一附件的像素内容**。直接修改持有的 Texture/Renderbuffer 仍需要外部排序；不要绕过 Framebuffer::Resize 改动旧快照的存储。默认 framebuffer 只有目标选择值，没有窗口/交换链所有权。
- 本次 draw 仍是可选同线程录制、同次消费。源 Camera/Scene/Light 的上游遍历、feature 消费者寿命、动态 GPU buffer 重写、反射/阴影/后处理生产顺序、UI/debug/picking/readback 和整帧完成尚需继续改造。
- Snapshot 的缓存及首次 GL 实现不支持多线程同时修改/消费。尚无 Render 线程、帧队列、GPU fence 或跨线程资源回收。阶段 2 和阶段 3 均未全部完成。
- Framebuffer/Texture/ABaseRenderer 布局变化，必须完整重建使用方。旧源码回归覆盖中追加当前 pass 状态成员及缺失的非虚辅助函数以保持 ABI；旧 DrawEntity/Bind/Upload 行为及基准提交不变。

## 验收

先完整构建对应配置，然后执行（可追加 --vs 指定 VS2022）：

~~~bat
node Tests\Skinning\run-validation.mjs Debug --target-state
node Tests\Skinning\run-validation.mjs Release --target-state
node Tests\Skinning\run-validation.mjs Debug --editor --target-state
node Tests\Skinning\run-validation.mjs Release --editor --target-state
node Tests\Skinning\run-validation.mjs Debug --draw-inputs
node Tests\Skinning\run-validation.mjs Release --draw-inputs
~~~

输出位于 Build/JobsValidation/Skinning/<配置>-target-state/current/ 和 <配置>-target-state-editor/current/。target-state 模式包含之前 frame-inputs/packet/Mannequin 验证；仅可与 --editor 组合，不与旧基准/parallel/ASan 混用。

- 每配置 16 轮旧目标 resize、源对象销毁后 clear 回放；检查全部颜色像素、depth/stencil、viewport、目标选择不受独立回放改变。捕获在无当前 GL 上下文时执行；零尺寸 resize 拒绝并保留旧配置。
- cubemap 六面独立读写选择、旧新尺寸、附件共享关系、border color、默认 framebuffer 和最后一份快照释放后的 FBO/纹理回收通过。
- 每配置 16 组真实 CompositeRenderer 即时/录制目标与 viewport 图像精确一致；录制分支在 draw 前故意绑定无关 FBO 和 1×1 viewport，确认 draw 恢复正确状态。目标在每轮改变尺寸。
- 原相机/时间/灯光字节检查、24 组相机切换图像、12 组反射/阴影/蒙皮/缓存状态，以及 24 帧真实 Mannequin/Standard 图像对拍通过。
- Debug/Release 真实 Editor 三视图开启录制：Edit/Play/Pause/8 次单步/恢复/Stop、24 组同视图顺序对拍、11 次动画变化、8 次切场景/resize 通过；新增每次 ViewPixels 核对 Image 的纹理 ID 与当前颜色附件一致。
- Debug/Release 旧 --draw-inputs 基准及材质/几何/资源寿命回归通过；缓冲 trace、材质状态、draw 顺序和图像逐字节一致。
- 完整 Debug/Release 构建 0 警告、0 错误，10 项程序/模型/shader 复制一致。未操作桌面输入，未推送远端；构建包含工作区已有用户改动，阶段提交不包含这些改动。

证据：Build/JobsValidation/msbuild-debug-target-state.log（完整布局重建）、msbuild-debug-target-state-final.log（包含 AView 修复）、msbuild-release-target-state.log、target-state-{debug,release}.log、target-state-editor-{debug,release}.log、target-state-regression-{debug,release}.log、target-state-build-artifacts.json，以及输出目录下 target-packet-frames.bin 和之前的 frame/packet/Mannequin 文件。日志、产物及测试项目均被 Git 忽略，跟踪的测试入口可重新生成。

本轮没有新增 ASan/Tracy 或独占性能测量。快照增加配置持有、首次 FBO 实现及状态恢复成本；resize 暂时保留旧附件时会增加 GPU 存储。带 GPU 读回的验证不用于性能结论，NVIDIA buffer migration 性能提示与 glGetError 成功检查应区分。

## 下一增量

建立完整帧内命令序列和明确完成边界，把动态 buffer 上传、反射/阴影/后处理写入与读取按原 pass/view 顺序纳入序列，去除执行端对 feature/scene 的借用。再迁移 UI、debug 和 picking/readback，形成阶段 3 有界队列能消费的稳定帧数据。
