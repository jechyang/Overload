# 阶段 2 第十一增量：ImGui 拥有型帧快照基础设施

日期：2026-09-06。前置提交：9736ad9（编辑器调试与拾取命令）。

## 实现范围

新增 OvUI::Rendering::DrawDataRecorder 和 DrawData。捕获复制顶点、索引、绘制命令、DisplayPos、DisplaySize、FramebufferScale；每条命令解析成拥有型纹理绑定。执行端不访问原 ImDrawData、ImDrawList shared data、ImTextureData、ImGuiContext、Canvas 或 widget。保留 VtxOffset/IdxOffset、裁剪、顺序和 ResetRenderState；未知回调、没有所有者的非零纹理 ID、非法索引范围和非有限尺寸/裁剪会拒绝。

既有 OpenGL3 后端抽出接收显式设备状态的消费函数，即时与拥有型入口共用原绘制算法。shader/VBO/EBO 由独立代际持有，源 backend/context 销毁后仍可使用；最后一个代际释放前不卸载 GL loader。执行使用 glBufferData，不借用 ImGui 的临时上传缓存。最终资源释放仍须发生在有效 GL context 的 owner 上。

动态字体图集以 CPU 逻辑 ID 确认请求，RGBA32 或 Alpha8 像素复制为不可变 RGBA 代际，在首次消费时上传。后续更新或图集重建不会改变先前帧的像素；一次捕获全部成功后才确认请求。尚未消费就取消的帧不丢失字体数据。被替换的旧 GPU 字体代际移入新帧的回收持有，避免 Capture 释放最后一个 GPU 所有者。上传隔离并恢复 unpack PBO、alignment、row length、skip rows/pixels。

UIManager 增加显式模式切换和 RecordRenderCommands；返回封口的 RenderCommandList，捕获可选目标快照。Canvas 将 CPU 帧构建与 OpenGL backend NewFrame 分开。模式启用/关闭及最终资源释放仍在 GL owner 上，切回即时路径前清除字体逻辑 ID。空 Canvas 不重复提交旧 DrawData。

Image、ButtonImage 增加 shared_ptr<baregl::Texture> 构造重载；TextureID 可携带真实所有者。构建 UI 时用作用域收集被绘制纹理，捕获后命令自行持有。原裸 ID API 仍供即时路径使用；录制时必须通过 owning widget 或额外 owners 参数提供匹配的资源。shared_ptr 只保证对象存活，不冻结外部纹理内容或使任意并发写入安全。

## 入口与限制

~~~cpp
uiManager.SetCommandRecordingEnabled(true); // GL owner 初始化与模式切换
// 图片控件应使用 owning texture 构造，或通过第二个参数提供 owners。
auto commands = uiManager.RecordRenderCommands();
commands.Execute(driver);                   // 当前同线程 GL owner 消费
~~~

本增量是 ImGui 帧快照基础设施及 Image/ButtonImage 的入口验收，**不是整个编辑器 UI 迁移完成**。工具栏、资产浏览器、树节点、搜索/对象字段、HeaderAction、TextureDebugger 和 AView 等现有调用仍含裸纹理 ID，需逐个迁移；TextureDebugger 的任意借用纹理需要真实底层存储持有，不能用空析构 shared_ptr 假装拥有。

产品默认仍走即时 UIManager::Render。本轮真实 Editor 回归验证既有 UI 路径和新的 3D/debug/picking 命令；没有把全体 Editor panel 开启 UI 命令录制后宣称通过。UI 构建期间的用户回调、资源加载、视图 resize 等也仍遵守原主线程/GL owner 约束。

当前面向产品已有的单主窗口与独占字体图集。多平台窗口创建/呈现、共享字体图集模式切换、GPU fence/统一 frame completion、有界队列、跨线程 GL 资源创建/释放尚未闭合。没有启用 Render 线程，没有全帧并行或帧率收益结论。内部共享状态要求独占生产、单消费者；不能并发执行同一份 DrawData。

## 验收

完整构建 Debug/Release 后运行：

~~~text
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --ui-commands --vs <VS2022目录>
F:/npm/node.exe Tests/Skinning/run-validation.mjs Release --ui-commands --vs <VS2022目录>
~~~

Tests/RenderingCommands/ui_command_validation.cpp 使用隐藏 OpenGL 4.5 窗口，本机 RTX 3070 Ti。每配置覆盖：

- 8 组即时/拥有型逐字节图像对拍，每组超过 65K 顶点，包含图标、文本、裁剪与 ResetRenderState；4 组使用非零 DisplayPos 和 2 倍 FramebufferScale。捕获期间解绑当前 GL context，消费前破坏源顶点并销毁原 ImGui context/backend/图标持有；验证单次消费、图标最终回收。
- 8 帧真实字体创建、更新、重建（断言命中 WantCreate/WantUpdates），全部捕获后销毁源图集并逆序消费，与顺序即时基准精确一致。污染 unpack PBO 和布局后消费，图像一致且五项状态恢复。
- 无所有者纹理、未知回调、非法索引/顶点偏移、非有限裁剪/尺寸均拒绝，失败捕获不确认字体；放弃已捕获帧后可恢复。后继捕获不删除旧 GPU 字体代际。
- 真实 UIManager/Canvas/Image/ButtonImage 的即时/拥有型图像一致；命令取消不发布成功；manager/widget/canvas 销毁后图像一致；最终纹理回收、空 Canvas 与模式切换通过。

两配置 --editor --command-list 回归通过：各 6 组 debug/picking 精确对拍，390 个 Actor 与 196 个 Gizmo 像素解析；真实 Editor Edit/Play/Pause/8 次单步/恢复/Stop，24 组视图顺序对拍、11 次动画变化、8 次切场景/resize 和三个保存项目统计通过。

Debug/Release 全解决方案构建均退出 0，0 警告、0 错误；10 项程序、模型、shader 复制 SHA-256 核对一致。日志在 Build/JobsValidation/：msbuild-{debug,release}-ui-owned.log、ui-owned-{debug,release}.log、ui-owned-editor-{debug,release}.log、ui-owned-build-artifacts.json。独立测试的 compile.log、run.log、result.json 在 Build/JobsValidation/Skinning/<配置>-ui-commands/。初轮 ui-owned-initial 构建记录包含已修复的 Canvas 声明遗漏，不代表最终结果。

本轮未新增 ASan、Tracy 或独占性能测量。阶段 2/3 均未全部完成；下一步迁移上述编辑器纹理入口并做全 UI 录制验收，然后闭合 frame completion，进入固定 Render owner、有界队列和真实 Game/Render 交叠。
