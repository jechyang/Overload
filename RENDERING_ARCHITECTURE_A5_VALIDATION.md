# A5 编辑器工具、拾取与后处理迁移

日期：2026-09-10。基线：f9ab4cb（A4）。状态：A5 实现与 Debug/Release 验收全部完成。

## 数据与线程归属

- Asset/Game/Scene 三视口与独立 Game 使用 RenderFrontend 的 SceneFrame。每个视口拥有前端、输出表面、后处理资源/历史和帧内命令；产品 RenderFrame 汇合各视口及最终 ImGui。AssetView 保留原有预览 Scene/ECS。
- PostProcessSnapshot 在 Main 的稳定场景读取阶段复制第一组有效设置。PostProcessPipeline 独占 Effect、ping-pong 及曝光历史；FullscreenCommandResources 持久保存 quad/fallback 资源。全屏命令显式指定输入、输出、viewport、状态、纹理代际和材质快照，Record 不读取 CPostProcessStack，也不使用旧 Feature 链。
- EditorOverlayCollector 集中读取组件，Grid、图标、递归调试形状、Outline/Gizmo 生成拥有型工具载荷。ToolDrawCollector 在提取边界复制 Engine/Skinning 描述数据；延迟编码不回查场景、面板或 Feature 服务。Outline/Picking 对源材质的逐物体参数使用 overrides。
- Picking 使用独立 View/Stage 和共同的网格筛选、排序、蒙皮链。PickingReadback 拥有像素、场景令牌、Actor ID/GUID 与 Gizmo 身份；仅成功父帧可在 Main 解析并应用交互。
- AView/SceneView 只提供视口、工具和拾取请求，不按名称修改内部 Pass。Game 同线程与 RenderThread 共享新准备实现。FrameInfo 来自准备/录制数据，不能当作 GPU 完成。
- RenderFrontend 独占程序布局验证缓存，以弱程序代际识别有效结果；首次使用/新代际在 Main/ResourceExecutor 完整验证，稳态提取不重新查询 GL，不延寿已删除资源。后处理目标以 CPU 快照代际身份比较，不为检查目标而实例化 GL 帧缓冲。

## 任务依赖与失败

场景准备、绑定和录制复用 RenderTaskCoordinator/JobSystem；各视口的工具 deferred payload 进入产品帧统一编码。后处理轻量编排保留在 Main，编码及 GPU 操作只捕获拥有型输入。GPU 固定顺序为辅助生产视图、主场景、显式后处理、SceneUI、编辑器工具/拾取及最终 ImGui。

子命令历史和 completion 并入父帧；取消、Prepare/Record 失败、部分执行或后续呈现失败都不能发布成功历史或交互。跨帧曝光/反射仍依赖实际执行结果；材质拖放与 Gizmo 使用捕获输入，删除对象、替换场景、重复/过期结果不会应用旧交互。

## 构建与验证证据

Debug/Release 解决方案及受最后接口修改影响的 OvRendering/OvCore/OvEditor/OvGame 均已编译、链接。Debug 的完整构建在移除旧类后补齐了编辑器目标；最后程序缓存和 CPU 目标比较修改随后重建全部四个相关模块，运行目录程序同步更新。证据：

- Build/a5-final-sequential-build-evidence.json、Build/a5-debug-full-before-oracle-cleanup.log
- Build/a5-cache-build-evidence.json（最终四模块，两个配置各退出码 0）
- Build/a5-cache-tests.json（最终测试状态；失败或未完成不算通过）

最终双配置各 8 项、共 16 项验收全部通过（退出码 0）：

| 验收入口 | Debug | Release |
| --- | --- | --- |
| --command-list | 通过 | 通过 |
| --auxiliary | 通过 | 通过 |
| --bindings | 通过 | 通过 |
| --effects-oracle | 通过 | 通过 |
| --postprocess | 通过 | 通过 |
| --editor --command-list --editor-ui --product-loop | 通过 | 通过 |
| --editor-stress | 通过 | 通过 |
| --game-thread | 通过 | 通过 |

- 原版 A4 Effect 与新显式全屏管线的 7 组各 24 帧图像全部一致；动态曝光/反射历史在取消、部分执行失败与放弃后恢复一致。
- 后处理专项的旧命令、旧即时、新串行、两 Worker、单 Worker 共 5 种模式各 14 帧，主画面、阴影与全部 cube mip 一致；设置/源对象销毁、目标 resize、失败/取消覆盖通过。
- 编辑器工具旧即时/新命令 6 组图像一致，390 次 Actor 与 196 次 Gizmo 解析，以及场景替换、删除和边界拒绝通过；24 组视图顺序、3 组三视口加 UI、18 组完整/分面板 UI 对照一致。
- 实际编辑器压力包含 16 组姿态/三视口/UI 对照、Render 闩锁暂停时连续两帧录制、96 帧完整界面、5 组极小/窄视口（含 1×1）、在途 Gizmo、6 次最小化恢复和 4 次应用重启/呈现失败。
- 实际 Game 的公开 Update 同线程基线与 Render owner 对照通过：32 组姿态/场景/UI、在途两帧、队列/帧栅栏、6 次最小化恢复、4 次重启及呈现失败停机。
- Worker 绑定/录制、强制乱序、污染恢复和源/目标生命周期回归通过。重叠计数仅证明工作实际交错，本阶段没有性能收益结论。

核心验证入口：--command-list、--auxiliary、--bindings、--effects-oracle、--postprocess；产品入口：--editor --command-list --editor-ui --product-loop、--editor-stress、--game-thread。运行形式与目录见 Tests/RenderingCommands/README.md。所有确定性图像与必要绘制数据按字节比较，不使用像素容差。A4 原始命令图像封存在 Build/a5-a4-oracle/；原版 Effect 从 f9ab4cb 独立编译，--effects-oracle 强制比较当前 7 组各 24 帧序列。

## 验证中修复的问题

1. 全屏录制器每帧创建 GPU 资源、重复程序布局 GL 查询、Snapshot::GetID 目标检查都会使稳态 Main 等待 Render owner。改为每视口资源、弱程序代际缓存与纯 CPU 目标身份检查；5 秒闩锁验证已确认可以连续录制两个在途帧。
2. UI 对照自身的 glReadPixels 性能回调会在两次录制之间给 Console 添加日志。仅在此测试对照期间过滤 GL_DEBUG_TYPE_PERFORMANCE，其他诊断继续转发，GL 错误与逐字节比较保留。依据：Build/a5-ui-before-diagnostic-freeze.log。
3. 完整压力序列在通过在途/线程图像检查后、继续重建多个上下文时触及 120 秒总时限。--editor-stress 总时限改为 300 秒，各闩锁的 5 秒失败限时保持不变。依据：Build/a5-stress-before-total-timeout.log；最终完整序列通过后才记录成功。
4. Game 图像测试使用公开的同线程 Update(0) 作为基线，不通过访问修饰符 overlay 调用新的私有录制方法。

## 职责迁移及剩余边界

DebugSceneRenderer、PickingRenderPass 已从产品源码/构建移除，面板和 Game 无旧 Renderer/Pass 控制。临时编辑器 oracle 从 A4 历史提交读取；旧 Picking 仅把结果类型接到独立 PickingReadback，绘制逻辑保持原样。

旧 SceneRenderer/CompositeRenderer/ABaseRenderer、部分旧工具 Feature 及 PostProcessRenderPass 适配仍服务历史对照测试。A6 必须迁移最后测试引用并彻底删除这些旧链、适配器和构建引用，完成总体并发、模块依赖及管线扩展示例验收。本次不把这些剩余项算作已完成。
