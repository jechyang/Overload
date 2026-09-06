# 阶段 2 第十二增量：编辑器 UI 纹理所有权迁移

日期：2026-09-06。前置提交：78393af（ImGui 拥有型帧快照）。

## 实现

Image/ButtonImage、InputText、ObjectField 的图标与预览、TreeNode 和 GroupCollapsable::HeaderAction 统一使用可携带所有者的 TextureID。GUIHelpers 的图标提供者、Actor 图标和 PickerItem，以及 GUIDrawer 的搜索/纹理预览接口一并迁移。录制期间按实际绘制控件收集绑定，DrawData 拥有这些绑定；源控件和临时集合不参与执行。

编辑器 AView 在创建与 resize 后保留新的 framebuffer 附件；Toolbar、AssetBrowser、Inspector、Hierarchy、Console、ItemPicker、PickerHelpers 和 Editor 的图标提供者都返回 owning texture。原生 GLenum/GLuint 只在 UI 消费边界使用。源资源重载或视图附件更换不会销毁已经录制帧持有的纹理，但持有不等于冻结外部可变纹理像素。

baregl::Texture 新增 Snapshot/RetainStorage，底层 GL ID 由独立 Storage 拥有，最后一个 owner 在 GL context 所在线程删除。Texture 包装对象的创建/销毁通知仍描述包装对象本身；Snapshot 不向资源跟踪表注册假对象。因此 TextureDebugger 可以在源 Texture 为栈对象时仍让已录制帧保留真实 GL 存储，当前面板则在源包装对象销毁后清除选择。没有空 deleter 或借用指针冒充所有者。默认选择改为实际最大有效纹理键，适用于非连续 ID。

TextureID 同时支持共享 Texture 包装对象与独立 Storage 快照，捕获校验 ID 与存储一致。现有 DrawDataRecorder::Capture(span<shared_ptr<Texture>>) 保留，新增 CaptureBindings 供 UI 控件的完整绑定使用。修改涉及 baregl::Texture 和若干 widget 的布局，需要完整重建各模块。

Editor 在析构时清空全局 Actor 图标持有与图标提供者，避免资源被静态 UI 状态拖到 GL context 销毁之后。验证中还触发了 Console 的既有退出问题：它销毁后仍订阅静态 Logger，后续 GPU 资源释放的驱动日志会写入已释放的日志 vector。Console 现在记录并移除该订阅。

## 验证方法

新增独立模式（可与 --command-list 合用）：

~~~text
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --editor --command-list --editor-ui --vs <VS2022目录>
F:/npm/node.exe Tests/Skinning/run-validation.mjs Release --editor --command-list --editor-ui --vs <VS2022目录>
~~~

测试使用实际 Editor、生产 panel 和保存的 SkinningProject。按 Main.cpp 的真实启动顺序在 Context 创建前启用 ResourceTracking；窗口隐藏、AppData 独立，没有桌面输入操作。

完成原模式与 3D 回归后，选择真实 Actor，给 Material Editor 设置 Default 材质，给 Asset Properties 设置 Mannequin 模型。先对拍完整 Canvas，再把全部 15 个实际面板分别放入单独 Canvas，避免面板重叠遮住未迁移入口。窗口显示区域稳定后，对即时和 owning 路径逐字节比较，没有像素容差，并要求图像含实际内容。

另测试 ItemPicker 的 owning Actor 图标。TextureDebugger 显式选择栈上 Texture，录制后销毁源对象，验证跟踪选择清空而 GL 存储仍存在、消费图像精确一致、最后一个命令持有释放后 GL ID 回收。合计每配置 18 组 UI 图像对拍。测试结束销毁 Editor，再检查静态 Logger 订阅数回到创建前，防止退出时遗留 Console 回调。

图像证据为 Build/JobsValidation/Skinning/<配置>-command-list-editor-ui/current/editor-ui-frames.bin。编译、运行与结果清单位于同级 compile.log、run.log 和父目录 result.json。

## 验收结果

- Debug/Release 最终全解决方案构建均退出 0，0 警告、0 错误；10 项 exe/模型/shader 复制 SHA-256 一致。
- 两配置各 18 组完整/逐面板/Picker/TextureDebugger UI 精确对拍通过；源栈纹理销毁后回放、GL 存储最终回收和 Editor 析构后的 Logger 订阅计数检查通过。
- 两配置原 3D/debug/picking 验收与 Edit/Play/Pause/单步/恢复/Stop、24 组视图顺序图像、11 次动画变化、8 次切场景/resize 回归通过。
- 两配置 --ui-commands 的源 ImGui context 销毁、大网格、高 DPI、字体更新/重建逆序回放、失败/取消与 unpack 状态恢复通过。
- 两配置 --command-list 的目标/上传/资源寿命、普通蒙皮、动态阴影/反射/曝光/Bloom/Tonemapping/FXAA 与取消/部分执行失败/历史恢复回归通过。

证据位于 Build/JobsValidation/：msbuild-{debug,release}-ui-editor-final.log、ui-editor-full-{debug,release}.log、ui-editor-snapshot-{debug,release}.log、ui-editor-core-{debug,release}.log、ui-editor-build-artifacts.json。早期构建与测试失败不替代这些最终结果；资源跟踪启动遗漏和 Console 析构后订阅均已处理。

## 边界与后续

这是现有主窗口 UI 的同线程 owning 消费验收。默认产品仍使用即时 UIManager::Render；未知自定义控件/回调没有自动获得跨线程许可。UI 回调可能加载资源、修改场景或 resize，这些操作仍遵守原 GL owner 与主线程约束。

ResourceTracking/Logger/Event 当前仍不是跨线程发布协议，Texture::Storage 的最终释放仍直接调用 GL。阶段 3 必须把 GPU 操作和最终回收固定到 Render owner，将 UI 相关通知转到主线程，并处理有界在途帧、窗口 resize/最小化、输入与退出。不能直接在线程池执行现有 RecordRenderCommands 整个调用。

阶段 2 的正式帧完成与统一交接仍需闭合；阶段 3 的固定 Render 线程、队列背压、真实 Game/Render 交叠和 Tracy/CPU/GPU/输入延迟验收尚未完成。本轮没有新增 ASan 或性能收益结论。
