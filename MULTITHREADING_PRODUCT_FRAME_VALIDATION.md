# 阶段 2 第十四增量：产品帧循环与逐帧交互

日期：2026-09-06。前置提交：c39589e（统一帧提交与 GPU 回收）。

## 产品路径

Editor 和 Game 的 Application 默认启用拥有型命令路径。对应项目配置或 Game.ini 中设置 render_commands=false 可回到即时路径；SetFrameSubmissionEnabled 支持帧间切换，有未提交帧时拒绝切换或重复 Update。原即时入口保留作对照。

Update 负责模拟更新与帧录制，PostUpdate 提交当前帧、交换窗口、等待 GPU 回收，再清理输入事件。新路径每个应用帧只有一个 RenderFrame fence；既有每视图 Driver 状态清理命令仍保留。Game 的场景命令在 SceneManager::Update 之前捕获，因此延迟切场景仍在原来的边界处理，不会把当前帧的旧场景替换为切换后的场景。调试 UI 更新与捕获仍在场景管理之后。

Editor 在成功完成当前帧后应用该帧拾取，再执行延迟 UI 操作；不在录制与 PostUpdate 各推进一次模拟。准备后未提交的帧在 Application/Editor/Game 析构时取消，捕获资源在 Context 销毁前释放。提交错误仍是可观测错误，不把等待结束当作成功。

这是阶段 2 的同线程产品接入，当前每帧显式 Wait。它没有伪装成 Game/Render 交叠，也不是最终背压策略。交互结果在本帧回收后应用，已经生成的 UI 会在下一帧反映选中状态；阶段 3 需要量化队列与输入延迟。

## 对应帧的交互数据

SceneView::Interaction 持有本帧 readback、命令完成标记、场景运行身份、鼠标局部坐标、视图尺寸、相机位置/矩阵、悬停/resize/鼠标按钮状态、选中 Actor GUID、Gizmo 操作与吸附参数。消费使用这些捕获值，不再改读最新 picking pass、UI 位置或相机。

Scene View 的命令入口清除旧交互记录，只有实际绘制产生新记录；禁用 picking pass 不借用上一张 readback。结果在 CPU 命令成功后可消费，产品入口还要求整个 RenderFrame GPU 回收成功。重复、取消、未完成和场景身份不匹配的记录拒绝；Actor/Gizmo 按仍存活的 GUID 解析。UI 改变选中对象或操作后，不用旧帧点击覆盖新的选择。Gizmo 释放、目标删除或身份失配会停止拖拽。

材质拖放通过实际 Image 的 DDTarget 事件，将资源路径保留在对应交互记录中，帧完成后用这张 readback 的目标应用材质。即时路径继续同步处理。无效坐标在转换为无符号整数前拒绝；非悬停的即时视图不额外做 GPU picking 读回。Gizmo 吸附开关和三个步长作为值传入操作，退出操作时清除临时覆盖，不污染随后即时调用。

Editor 当前只允许一个待提交产品帧，并匹配最新帧完成记录。Interaction 已拥有所需输入，但多帧在途的结果集合、排队/合并策略及失败传播仍属于阶段 3，不能直接去掉单帧保护。

## 应用生命周期修复

Scene View 现在移除自身的全局 Actor 销毁订阅。重复创建 Editor 的实际 Application 测试还发现 Hierarchy 在析构后继续接收 Actor 创建事件；原生调用栈显示新 Asset View 创建灯光时调用了已销毁的 Hierarchy::AddActorByInstance。

Hierarchy 现在记录并移除 Actor Created/Destroyed/Attach/Dettach 和 SceneUnload 的外部订阅。EditorActions 自己拥有的选择事件随其销毁，面板析构不访问已经销毁的 EditorActions。层级搜索的折叠缓存改为面板成员，搜索结果为局部变量，清空场景或删除节点时清除相应记录。临时异常诊断代码已移除。

## 验证

先完整构建对应配置，再运行（可追加 --vs 指定 VS2022）：

~~~text
node Tests/Skinning/run-validation.mjs Debug --editor --command-list --editor-ui --product-loop
node Tests/Skinning/run-validation.mjs Release --editor --command-list --editor-ui --product-loop
node Tests/Skinning/run-validation.mjs Debug --game-frame --product-loop
node Tests/Skinning/run-validation.mjs Release --game-frame --product-loop
~~~

测试继续使用隐藏窗口、新建并保存的项目和隔离 AppData，没有桌面鼠标键盘操作。Application 的窗口可见性与用户项目路径仅在测试 Context 副本中替换，Application/Editor/Game 的生产对象文件直接链接。输入事件通过窗口公开事件发送给真实 InputManager；定点拾取使用实际 GPU 生成的 Actor/Gizmo 像素，测试按帧载荷设置鼠标坐标，不移动桌面鼠标。

Debug/Release 结果：

- Editor 和 Game 的实际 Update/PostUpdate 各八帧即时/拥有型姿态和视图图像逐字节一致，并要求首末姿态和画面发生变化。验证新路径一帧一次 fence、PostUpdate 清输入、重复 Update/帧内模式切换拒绝；Editor 延迟操作每帧只执行一次。
- 真实 Scene View 的捕获鼠标/悬停/readback 在更新 UI 位置、生成另一个 pass 后仍选中原 Actor；真实 DDTarget 材质拖放在对应帧完成后生效。Gizmo 按下/拖动/释放使用捕获相机，与同输入的即时 Gizmo 操作比较；删除目标会停止操作，替换场景拒绝旧结果。
- 两个实际 Application::Run 默认新模式，各运行四帧后通过测试 fence 回调请求关闭；Editor 加载保存的 crowd 场景并进入 Play，Game 加载保存的八实例场景。随后帧间切回即时运行、再次启用新模式，准备一帧后析构，检查取消与外部订阅数量。另创建 render_commands=false 的应用，确认配置回退并正常退出。
- 保留三组 Editor Asset/Game/Scene+UI 整帧精确对拍、六组 Game 场景/blit/调试 UI 对拍、18 组完整/逐面板/Picker/TextureDebugger UI 对拍和源场景/目标/UI 生命周期检查。
- 保留 Editor 原项目蒙皮批次、Edit/Play/Pause/八次单步/恢复/Stop、24 组视图顺序对拍、11 次动画变化、八次切场景/resize，以及六组真实 debug/picking 图像与 Actor/Gizmo 解析检查。
- 最终 Debug/Release 全解决方案构建退出 0，零警告、零错误；十项 exe/模型/shader 复制 SHA-256 一致。

证据位于 Build/JobsValidation：msbuild-{debug,release}-product-frame-final.log、product-frame-{editor,game}-{debug,release}-final.log、product-frame-build-artifacts.json。各模式的结果清单位于 Skinning/<配置>-command-list-editor-ui-product-loop 与 <配置>-game-frame-product-loop/result.json；编译和运行日志在 current 目录。

本轮没有新增 ASan 或性能收益结论。测试时间包含并行构建/验收，不当作帧率；驱动同步像素传输性能提示也不当作 GL API 错误。

## 接下来完成阶段 3

阶段 2 的内置渲染、UI、debug/picking、资源捕获和帧完成已接入同线程产品路径。整个阶段 2/3 目标仍未完成，尤其不能把当前逐帧 Wait 当作双线程结果。

1. 固定 Render owner，迁移资源创建/上传/draw/readback/销毁；梳理录制期间 GL 查询、Framebuffers resize、Shader/Texture/ImGui 初始化与 Tracy GPU 埋点。
2. 将跨线程 ResourceTracking/Logger/UI 通知交给主线程消费，明确资源代际及最后释放的线程，禁止资源包装和场景被并发借用。
3. 实现有界帧交接与显式背压、逐帧输入/拾取结果传递、失败后停止/排空/重建协议。正常循环不能靠整帧锁或每帧 Wait 排空伪造并行。
4. 完成 resize/最小化/场景切换/退出压力和图像/交互回归；验证所有 GL 操作的线程归属，Tracy 显示真实 Game/Render 交叠，报告 CPU/GPU/队列/输入延迟。
