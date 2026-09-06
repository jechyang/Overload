# 阶段 2 第十三增量：统一帧提交与 GPU 回收

日期：2026-09-06。前置提交：ec4e393（编辑器 UI 纹理所有权迁移）。

## 实现与状态协议

RenderCommandList 可以转入另一个正在录制的列表。按顺序合并操作、历史依赖和原列表的完成标记；嵌套列表及父列表追加的历史操作识别已经排在前面的内部依赖。所有操作和最终提交步骤成功后才发布子列表 CPU 成功；后段失败不会留下前段成功标记。多个原子标记依次发布，不声称它们构成一次原子事务。

新增可移动、不可复制的 RenderFrame，拥有聚合命令与独立完成记录。RECORDING/READY 可取消，未提交析构也取消；Submit 在 GL owner 执行全部操作、可选呈现回调和 Driver::OnFrameCompleted，然后插入 GPU fence 并 flush。CPU 提交成功后状态为 SUBMITTED，只有观察到 fence 完成并释放捕获后才发布 COMPLETED/retired。帧 ID 用于诊断，完成记录身份用于匹配，不能仅凭数字相同认定同一帧。

列表 Completion 仍只表示 CPU 执行成功；帧 Completion::Succeeded 同时要求 GPU 已安全回收且整帧成功。PresentationRequested 仅表示同步 Swap 回调返回，不代表屏幕已物理显示。错误保存在独立完成记录内，RethrowIfFailed 必须在 IsRetired acquire 后读取。后来的 GPU 等待失败使整帧失败并失效历史，不改写已经发布的子列表 CPU 结果。

Poll 使用零 flags、零超时的 glClientWaitSync，未完成时不释放任何捕获。Wait 用于显式背压/退出；提交后未主动回收的帧析构会 drain。部分命令或呈现失败可能已产生 GPU 工作，仍插入 fence 并保留整个序列至回收。fence 创建失败或等待返回失败时，使用 glFinish 收尾并报告 FAILED。正常成功路径不调用 glFinish。该协议要求有效的 GL context，不能恢复设备/上下文丢失。

Editor::RecordRenderFrame 聚合 Asset/Game/Scene 视图、面板更新与 UI 命令；CompleteRenderFrame 只对成功回收的最新记录处理 Scene View 主线程拾取，并拒绝取消、失败、过期和重复结果。当前接口按同线程录制/消费使用；多帧在途时每帧输入及拾取结果的独立传递仍待实现。

Game::RecordRenderFrame 捕获已更新的场景、最终 backbuffer blit 与可选调试 UI。blit 持有目标快照和录制时尺寸，源场景/原 framebuffer 销毁后仍可执行。此接口不重复推进模拟或 FPS/UI 统计，默认 Update/PostUpdate 产品循环仍走原路径。

## 验证方法与结果

先完整构建对应配置，再运行以下入口；可追加 --vs 指定 VS2022。

~~~text
node Tests/Skinning/run-validation.mjs Debug --render-frame
node Tests/Skinning/run-validation.mjs Release --render-frame
node Tests/Skinning/run-validation.mjs Debug --game-frame
node Tests/Skinning/run-validation.mjs Release --game-frame
node Tests/Skinning/run-validation.mjs Debug --editor --command-list --editor-ui
node Tests/Skinning/run-validation.mjs Release --editor --command-list --editor-ui
~~~

render_frame_validation.cpp 使用真实隐藏 OpenGL context；可控拦截 fence/等待/删除/finish 后转发真实 GL，分别注入尾操作失败、呈现失败、fence 创建失败、Poll 等待失败和 Wait 等待失败。验证子完成发布时机、零超时轮询、真实纹理弱引用与 GL ID 的回收、移动/取消/析构/重复提交、嵌套历史顺序与失败恢复。后继可以在前帧尚未 GPU 回收时提交；逆序历史必须在任何 GPU 操作之前拒绝。真实 UIManager/context/控件在提交前销毁，聚合背景+UI 与即时结果精确一致，纹理到 GPU 回收才释放。两配置通过。

Editor 测试链接实际 Editor 对象，隐藏窗口、独立 AppData 和新建保存的 SkinningProject。三个视图在固定布局中可见，保留真实场景、预览后处理和 UI；先让 Asset View 聚焦运动结束，再用零更新时间比较相同输入。每配置三组各视图及最终 UI 合成逐字节一致，另覆盖取消、呈现失败、最新/过期/重复完成与一次真实 Swap。相机仍在聚焦时的初次差异是输入不一致，未放宽像素容差。继续运行原来的 18 组完整/逐面板/Picker/TextureDebugger UI 对拍和 Editor 析构订阅检查，均通过。

Game 测试链接实际 Game 对象，Context 副本仅隐藏窗口并把用户项目路径隔离到测试目录。新建并保存包含八个 Mannequin、相机与灯光的项目；有/无调试 UI 各三组即时/聚合帧精确一致，并要求画面非空。取消后恢复呈现、录制后卸载场景并销毁/替换原 framebuffer，仍与原尺寸参考图像精确一致。Debug/Release 各六组通过。

既有 --command-list 两配置的资源/目标/上传、动态反射/曝光取消与失败恢复、普通蒙皮及动态图像对拍通过。Editor 两配置继续通过真实 Edit/Play/Pause/单步/恢复/Stop、24 组视图顺序对拍、11 次动画变化和八次切场景/resize。--ui-commands 两配置继续通过源 context 销毁、大网格/DPI、字体逆序更新/重建及 unpack 状态恢复。

最终完整 Debug/Release 构建退出 0，零警告、零错误；十项 exe/模型/shader 复制 SHA-256 一致。证据位于 Build/JobsValidation：

- msbuild-{debug,release}-render-frame-final.log
- render-frame-{debug,release}-final.log
- render-frame-editor-{debug,release}-final.log
- render-frame-game-{debug,release}-final.log
- render-frame-core-{debug,release}-final.log
- render-frame-ui-{debug,release}-final.log
- render-frame-build-artifacts.json

各模式的 compile.log、run.log 与 result.json 位于 Skinning/<配置>-render-frame、<配置>-game-frame、<配置>-command-list-editor-ui 等目录；图像为 editor-render-frame.bin、game-render-frame.bin。调试驱动的同步像素传输性能提示不当作 GL 错误。测试耗时包含编译及其他并行验收，不用于性能结论。

## 后续边界

统一帧与 GPU 回收协议已落地。完整产品循环的可切换帧提交模式、逐帧输入/拾取所有权，以及录制期间的 GL 创建/resize/元数据访问仍需收尾；阶段 2、3 不标为全部完成。

下一增量先把 Editor/Game 的模拟更新、CPU 录制和帧消费接到可验证的产品循环，再实现阶段 3 固定 Render owner、资源创建/销毁与主线程通知、有界队列及停机排空。当前移动帧只转移所有权，不提供线程同步；资源最终释放、等待和析构仍必须在 GL owner 且早于 context 销毁。不能直接把整个现有录制入口放入工作线程。

GPU fence 增加一次同步对象/flush，并把命令载荷与捕获资源保留更久；有界队列必须计入这些内存和输入延迟成本。本轮没有新增 ASan、Tracy 交叠或整帧性能收益结论。真正完成阶段 3 仍需创建/上传/draw/readback/destruction 的线程归属检查，窗口/场景/输入/退出压力回归，以及 Game/Render 交叠、CPU/GPU/队列/延迟测量。
