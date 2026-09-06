# 阶段 2 第五增量：同次 draw packet 与 feature 资源持有

日期：2026-09-06。前置提交：abf6b13（几何持有与模型/palette 上传快照）。

## 已实现范围

CompositeRenderer 增加 SetDrawRecordingEnabled，默认关闭，须在帧外由 GL owner 切换。开启后，真实 DrawEntity 先录制 feature 操作、完整材质快照和几何命令，录制全部成功后，在同次调用中按原 feature 顺序执行，再执行材质和几何。OnAfterDraw 仍同步读取存活的 Drawable。原即时路径继续作为默认和对照。

EngineBufferRenderFeature 录制模型/用户矩阵值；SkinningRenderFeature 录制独占 palette；ReflectionRenderFeature 录制 probe 的 124 字节常量页并持有 cubemap，执行到 feature 自己的 UBO；ShadowRenderFeature 持有所选定向光的深度纹理，lightSpaceMatrix 由材质快照捕获。反射选择逻辑与矩阵布局不变，probe 属性及外部帧状态在 BeginFrame 至本次 draw 期间须稳定。

Framebuffer::RetainTextureAttachment 返回已有纹理的 owning 句柄，缺失或 renderbuffer 附件返回空。Blit 自动持有 _InputTexture；BlitWithRetainedTextures 接收额外 sampler 输入。自动曝光的 luminance/exposure、Bloom 的 bloom 纹理均已接入。原 Blit 签名和虚函数槽保留，新方法不增加基类对象字段。

每个启用的 draw feature 必须显式 SupportsDrawRecording。内置 EngineBuffer、Skinning、Reflection、Shadow、Lighting、FrameInfo 已核实；内置类的派生类型不会隐式继承许可。未知 feature、自定义 IMesh/派生 Mesh 在录制前拒绝。仅处理帧事件或对当前 pass 禁用的 feature 不受 draw 录制检查影响。批准接口要求 OnBeforeDraw 只记录值和资源持有，不调用 GL、不在回放闭包中借用源对象。

## 材质缓存与失败协议

- RecordSnapshot 保留原签名和全量录制语义；新增 RecordCachedSnapshot 供生产聚合路径使用。上传分组由原 PrepareBinding/signature 决定。若一组未变，复制上次已捕获的 GPU 值和对应纹理持有。
- 这保留原 singleUse 行为：首次上传后 CPU 值恢复默认，签名不变时 GPU 继续使用上次上传值；不能直接把已恢复的 CPU 默认值录成下一包。shader/pass/feature/材质变化仍使原签名失效。
- 包回放每次绑定 program 并上传全部已捕获 uniform，palette 也独立上传；它不依赖以前的 GPU 内容，但仍依赖外层 view/pass/target 等状态。录制成功才消费本次需要上传的 singleUse 属性，缓存复用组不重复消费。
- CPU 录制失败不执行已排入的 feature GL 操作，并清除录制指针及材质缓存。pass 抛出后重置当前 pass 游标，调用方正常 EndFrame 后可开始下一帧。已发生的 feature SetProperty 等 CPU 副作用不整体回滚，执行阶段已发出的 GL 写入也不回滚。
- BeginFrame、EndFrame 和模式切换清除 packet 材质缓存。只为仍需复用的纹理保留持有，不累计历代缓存。获取/执行/最终释放都必须早于 GL 上下文销毁。

## 所有权边界

| 数据 | 本增量状态 |
|---|---|
| PSO、primitive、instances | 包内值 |
| 标准 Mesh 的 VAO/VBO/EBO | 独立共享持有 |
| shader 代际、uniform 值、资产/fallback/raw sampler | 材质快照持有；raw 必须提供匹配 owning 句柄 |
| model/user、palette、probe 常量页 | 独占值；回放不读取 Drawable/组件数组 |
| 反射/阴影/后处理纹理对象 | 持有对象生存期；不冻结内容或尺寸 |
| 执行 feature 及其 UBO/SSBO | renderer 拥有并在同步调用期间存活 |
| camera/view/projection/time、lighting buffer、viewport、target、pass 前后操作 | 外部同步链路，尚未迁移为帧包 |
| OnAfterDraw、UI/debug/picking 和任意自定义扩展 | 保持原同步消费，未获得通用延迟执行许可 |

本增量没有公开延迟 packet 队列。闭包可以引用存活的执行 feature，但不能带走到下一帧；动态 cubemap 双缓冲、阴影重画、framebuffer resize 和后处理 ping-pong 仍按原生产/消费顺序运行。shared_ptr 不提供内容版本、并发修改安全、GL 线程切换或 GPU fence。阶段 2 整体未完成，尚未引入 Render 线程。

## 可复现入口

先完整构建 Debug/Release，CompositeRenderer 布局和 ARenderFeature 虚表已改变，不得混入旧对象文件。VS 路径发现失败时添加 --vs 参数。

~~~bat
node Tests\Skinning\run-validation.mjs Debug --draw-packets --vs "C:/Program Files/Microsoft Visual Studio/2022/Community"
node Tests\Skinning\run-validation.mjs Release --draw-packets --vs "C:/Program Files/Microsoft Visual Studio/2022/Community"
node Tests\Skinning\run-validation.mjs Debug --editor --draw-packets --vs "C:/Program Files/Microsoft Visual Studio/2022/Community"
node Tests\Skinning\run-validation.mjs Release --editor --draw-packets --vs "C:/Program Files/Microsoft Visual Studio/2022/Community"
~~~

输出分别为 Build/JobsValidation/Skinning/<配置>-draw-packets/current/ 和 <配置>-draw-packets-editor/current/。专用程序在同一当前实现中切换即时/录制模式；不是检出旧 Editor 逐帧对拍。反射 fixture 只用访问权限副本填充生产 probe 的纹理，生产代码没有测试分支。

## 本机验收结果

- Debug/Release 各 12 组即时/录制场景，共 120 次实际 draw：反射 cubemap/亮度、定向光深度/矩阵、模型/palette、材质交替及 singleUse 缓存。RGBA 逐字节一致；另按实际 GL draw 记录顺序、几何状态、模型 UBO 和 GPU tint，逐字节一致。对输入计算的预期颜色仅允许 1 个 8-bit 量化单位，确保反射和变化的阴影确实被采样。
- 各 24 帧真实 Mannequin/Standard/SceneRenderer，即时与录制图像完全一致；22 次动画变化、最终 933 个非背景像素，绘制不推进姿态。
- 未适配 feature、内置 feature 派生类、自定义 Mesh 拒绝；未持有裸纹理的失败录制不上传 feature 缓冲，EndFrame 后下一帧可恢复。framebuffer/原纹理引用销毁后附件仍存活，最后持有释放后弱引用失效且 GL texture 被回收。
- 各配置真实 Editor 的 Scene/Game/Asset 三个 renderer 全部开启录制：Edit/Play/Pause/8 次单步/恢复/Stop，通过 24 组同视图消费顺序图像比较和 8 轮延迟切场景/resize。保留默认后处理，验收中补齐了自动曝光和 Bloom 的额外 sampler 所有权。
- 生成项目仍包含 crowd/fragmented/rate_limited 三场景。96 次 Update 的并行评估数分别为 6144、0、1536；批次数 96、0、24；两配置一致。这只验证既有蒙皮调度未回归，不代表 draw packet 带来 CPU 并行。
- Debug/Release --draw-inputs 旧 b1128a7 对照全部通过：16860 字节缓冲调用、98304 字节输入图像、45003/11186 字节材质状态/调用、4194304 字节几何图像、19456 字节 draw 记录、7372800 字节蒙皮图像精确一致；32 全类型材质快照、8 轮资源重载/销毁、64 代际帧、32 独立几何帧等旧生命周期测试通过。
- Debug/Release --material 旧 50194bf 对照通过。测试兼容层只补充旧副本缺失的新辅助函数及非虚声明，以链接当前库；保留旧 Bind/UploadProperties/DrawEntity 和原基准 hash seed 归零处理。没有替换旧即时对照逻辑。
- 全解决方案 Debug/Release 均退出 0、0 警告、0 错误，10 项程序/资源复制逐字节一致。构建包含工作区原有用户改动，提交排除这些改动。

证据：Build/JobsValidation/msbuild-{debug,release}-draw-packets-final.log；packet-{debug,release}.log；packet-editor-{debug,release}.log；packet-regression-{debug,release}.log；packet-material-{debug,release}.log；draw-packets-build-artifacts.json；各测试目录 result.json、run.log、packet-frames.bin、packet-draw-trace.bin、immediate/frames.bin 和 recorded/frames.bin。日志及生成项目被 Git 忽略，可通过跟踪的测试入口重新生成。

本轮没有新增 ASan/Tracy 采集或独占性能测量。录制模式增加 shared_ptr、std::function、uniform/palette 复制及完整上传成本，仍保持默认关闭；不能用并发验收时输出的 SetTime 耗时声称整帧性能收益。

## 下一个独立增量

先将 view/projection/camera/time、lighting 常量和 pass/viewport/target 状态迁移为同线程可回放载荷，逐步扩大 packet 的稳定边界；随后处理反射/阴影/后处理生产者顺序及动态纹理内容版本、UI/debug/picking、多视图缓存和帧完成。闭合资源及失败/退出协议后再进入阶段 3，不直接将当前闭包提交给 JobSystem 或 Render 线程。
