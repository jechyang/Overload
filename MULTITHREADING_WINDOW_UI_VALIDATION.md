# 阶段 3 第十增量：完整 UI、窗口与在途输入验收

日期：2026-09-07。前置提交：71cb6cb（Editor 双线程与逐帧交互）。

## 修复的产品问题

完整编辑器第一次布局时，Asset View 可以产生窄小目标。Bloom 原来按较长轴决定降采样层数，却直接把两个轴都右移，实际触发 BloomSamplingBuffer2 4x0 并中止帧。现在两个轴均至少保留一个 texel，1x1 输入仍有一个合法采样 pass；配置 pass 数先限制到支持范围，避免零层和不合法 clamp 区间。普通有效尺寸的既有动态图像回归保持一致。

Window::IsMinimized 原来把“没有最大化”当作最小化；改为查询 GLFW_ICONIFIED。Framebuffer resize 异常增加目标名和请求尺寸，保留原来的无效尺寸拒绝规则，不允许创建零尺寸附件。

## 验证入口

完整构建 Debug/Release 后运行，可按需添加 --vs：

~~~text
node Tests/Skinning/run-validation.mjs Debug --editor-stress
node Tests/Skinning/run-validation.mjs Release --editor-stress
node Tests/Skinning/run-validation.mjs Debug --game-thread
node Tests/Skinning/run-validation.mjs Release --game-thread
~~~

Editor 压力输出位于 Build/JobsValidation/Skinning/<配置>-editor-stress-command-list-editor-ui/current；Game 输出仍为 <配置>-game-thread/current。压力测试复用已有保存项目和 Editor 双线程图像/交互验收，再运行新增完整产品画布，不替换为测试专用三视图 Canvas。

## Debug/Release 结果

- 两配置各 96 帧完整编辑器画布，覆盖实际菜单、工具栏和全部窗口面板。循环聚焦面板得到 95 次可见访问；12 次窗口/视图尺寸及场景变更，Play/Pause/Step/Resume、视图关闭重开和延迟 Main 动作均通过。所有提交帧成功退役，输入事件按帧清理。
- 五种真实 Asset View 目标（1x1、1x64、64x1、4x3、32x5）在同线程和 Render Context 下逐字节一致。该用例显式检查实际 framebuffer 尺寸并保留 Bloom，没有通过跳过渲染掩盖小目标问题。
- 实际产品的鼠标按下与释放事件经 Window/InputManager 捕获。阻塞 Render 时分别准备 press 和 drag，确认两份输入均未提前消费；后续 release 按序应用。坐标由夹具指定，以避免移动用户桌面鼠标；采集相机之后故意更改当前相机，最终 Actor 位置、旋转、缩放与对应输入的 Gizmo 串行 oracle 精确一致。
- Game、Editor 每配置分别进行六次真正的窗口 Minimize/Restore，前后读取实际 GLFW iconified 状态；最小化和恢复期间继续提交在途帧，最后全部排空。测试窗口设为离屏、无激活/任务栏样式，恢复后立即保持隐藏，并检查前台窗口没有改变。
- 产品 owner 检查包含 glDrawElements、glDrawElementsInstanced 和 ImGui 使用的 glDrawElementsBaseVertex，以及连接采集时的时间戳调用。完整 UI 的 BaseVertex 绘制现在也直接接受线程/当前上下文断言。
- 复用的 Editor 16 帧三视图/画布/姿态与 15 次图像变化、拾取/材质拖放、场景替换、四次应用重建及呈现失败保持通过；旧完整/逐面板 UI、蒙皮批次、编辑器模式和视图回归也通过。
- Game 32 帧姿态/场景/UI 与 15 次图像变化、队列容量、实际 Run 退出、四次应用重建、准备帧取消及呈现失败通过。
- 两配置另行运行 --command-list：真实动态阴影、反射/mipmap、渐变曝光、Bloom/Tonemapping/FXAA、取消/失败历史恢复，以及源对象销毁回放均通过，普通尺寸图像保持精确对拍。

最终完整构建日志为 Build/JobsValidation/msbuild-{debug,release}-editor-stress-bloom.log，均为 0 警告、0 错误。各测试目录的 result.json、compile.log、run.log 保留结果。窗口/UI 压力与编译有并行运行，不把这里的 wall time、draw 交叠次数或固定门闩时长作为性能结果。

## 后续验收

阶段 2/3 整体目标仍未完成。接下来独占运行稳定的真实 Game/Editor 工作负载，对照即时、同线程命令和双线程命令路径，报告 Main 帧耗时、Render 消费/呈现耗时、GPU 时间、在途/排队数量及输入到呈现请求/交互应用的延迟；明确统计口径、预热、负载与 Tracy 连接开销。呈现回调完成不等于屏幕物理显示，需要按该边界表述延迟。

随后按阶段 2、3 原始要求核对所有权、顺序、共享资源、GPU 回收、退出和对应验证证据，决定默认线程配置并提交最终验收结论。当前 render_thread 仍为显式启用，尚无整帧性能收益结论。
