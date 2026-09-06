# 阶段 3 第九增量：Editor 产品双线程与在途交互

日期：2026-09-07。前置提交：c4d569c（Game 产品双线程接入）。

## 产品协议

项目 .ovproject 中的 render_thread=true 现在也用于 Editor，要求 render_commands=true。暂默认 false；已有同线程命令/即时回退保留，独立 Render Context 内拒绝运行时切到即时模式。该配置在 Context 构建时读取，需要重建应用生效。

Editor Context 与 Game 使用相同的资源生产者协议：窗口、平台/UI 与场景更新留在 Main；固定 RenderThread 创建并持有唯一 Driver/GL 上下文，ResourceExecutor 覆盖 UI 和资源的生产期。资源/UI 先释放，随后撤销作用域并关闭 Render、销毁 Driver/上下文，最后释放窗口/设备。

实际 Editor Update 录制 Asset/Game/Scene 与 UI，PostUpdate 向两帧容量的 RenderThread 提交；正常逐帧不 Wait。每个提交保存独立 completion 与 SceneView::Interaction，Main 按提交顺序收集 GPU 已退役帧，只有成功结果进入 CompleteInteraction。新帧不再覆盖旧产品帧的拾取、鼠标/相机、GUID 与拖放数据。手动 RecordRenderFrame/CompleteRenderFrame 仍保留原来的最新帧规则，产品队列使用独立集合。

延迟 UI 动作按 Main 的 PostUpdate 次数推进，成功入队后执行一次，不随积压 completion 的数量多次推进；交互结果则在 PreUpdate/PostUpdate 收集或显式 Drain 时应用。源场景删除依赖命令的资源所有权，交互应用检查对应场景 token、Actor GUID、选择与操作状态。取消/失败帧不发布拾取。

Application.Run 结束时排空。Editor 析构先取消准备但未提交帧、等待已提交工作并处理完成/错误，再卸载场景和销毁面板。Main 清理输入、处理游标；Render 仅负责呈现。没有引入 Render 等待 Main 的任务循环。

## 复现与夹具

完整构建 Debug/Release 后独立运行（可追加 --vs）：

~~~text
node Tests/Skinning/run-validation.mjs Debug --editor-thread
node Tests/Skinning/run-validation.mjs Release --editor-thread
~~~

输出 Build/JobsValidation/Skinning/<配置>-editor-thread-command-list-editor-ui/current。新测试先创建保存项目并运行既有 Editor 场景/视图/UI 回归，再以实际 Application 分别构建同线程和 Render Context。测试覆盖头只放宽访问权限；Context 副本隔离窗口/AppData，Window 副本仅在交换缓冲前插入读回入口。

三视图画布固定窗口与面板尺寸，关闭曝光渐变并完成 Asset 相机聚焦。初次夹具未先初始化 ImGui 布局，导致 framebuffer resize 失败；补齐真实 UI 录制/消费的布局预热后通过。图像对照阶段显式排空并清理默认目标，以获得确定的 oracle；自然交叠和背压使用独立运行阶段，没有用图像对拍中的等待证明并行收益。

## 验收结果

Debug/Release 均通过：

- 16 帧实际产品姿态、三个视图附件及整个测试画布逐字节一致，Game 视图相邻帧出现 15 次图像变化。
- 暂时阻塞 Render 时，Main 仍完成两个实际 Editor Update 并提交第一帧，两份 Interaction 不同且未提前消费；释放后两份结果依次消费，队列完全收集。
- 产品拾取与材质拖放保留对应帧 readback，源视图又录制并取消后仍正确选中目标并应用材质；重复结果和替换场景的旧 token 被拒绝。
- 64 帧自然运行期间观察到实际 draw 与 Main Update 交叠；一次最终未连接采集的 Debug 为 51,156 次、Release 为 46,335 次，数量随调度变化。
- 实际 Application.Run 通过 Main 日志事件关闭并排空；4 次完整应用重建保留提交成功、取消准备帧、使旧纹理 storage ID 失效。呈现失败在退役后由 Main 观测，失败交互未消费，owner 清理仍可执行。
- 同一专项运行原保存项目蒙皮批次、Edit/Play/Pause/Step/Stop、视图顺序与动态场景、拾取/Gizmo/拖放、聚合帧和 18 组完整/逐面板 UI 对拍。
- 另行运行两配置 --editor --command-list --editor-ui --product-loop，实际旧 Application.Run、即时配置回退、未提交退出、Console 主线程日志与 TextureDebugger 归属全部通过。

完整解决方案 Debug/Release 均为 0 警告、0 错误；日志 Build/JobsValidation/msbuild-{debug,release}-editor-thread.log。各专项 result.json、current/compile.log 与 run.log 保存逐项证据。Editor.cpp 的既有两处用户注释改动在工作区保留，提交时排除。

## 独占连接 Tracy

Release 可执行文件接收输出目录及 --capture，等待本机 Tracy 连接。最终运行没有其他引擎测试同时启动，harness/capture 均退出 0；报告 8,184 次 GPU 时间戳调用通过当前 Context 的 owner 检查（同线程基准的 owner 为 Main，双线程配置为 Render），双线程 Main 没有当前 GL 上下文。自然交叠观察为 47,507 次 draw。

采集 Build/JobsValidation/tracy-editor-thread-release.tracy，相关 -harness.log、-capture.log、-result.json 和导出的 -Editor-Update.csv、-Draw.csv、-Collect.csv 均可用。解析得到 211 个 Editor Update CPU zone、1,061 个 Tracy GPU Collect CPU zone，源路径对应当前 Editor.cpp。采集包含初始化、旧路径对照、读回、故障注入和应用重建，不能作为稳定整帧 GPU 耗时或性能收益样本。

## 下一步

Game 和 Editor 均已可选真实双线程，但阶段 2、3 的整体目标尚未全部完成。下一步集中验收完整产品 UI 下的多帧输入/Gizmo、窗口 resize/最小化与恢复、视图/场景切换和退出压力；随后在独立稳定负载下量化整帧 CPU/GPU、队列长度和输入到呈现/交互应用延迟，并据此决定默认配置。当前完整图像对照使用固定三视图画布，完整编辑器面板的跨线程交互及最终性能结论仍待补齐。
