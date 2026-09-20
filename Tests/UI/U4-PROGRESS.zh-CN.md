# D / U4 布局工作包

状态：D 实现与专项验收、标准 Debug、UI/managed/scripting 总回归及示例 host generation 均已通过。完整 U4 阶段门禁暂保留 partial：editor-camera 被并行编辑器导航命中区域回归阻塞，已交 A/编辑器负责人处理。

## 生产实现

- GridLayoutGroup：四 corner、两 axis、三 constraint、constraintCount、cellSize、spacing、九种 alignment、padding、空集合和非正步长保护。
- AspectRatioFitter：全部五种模式；stretch anchor、任意 pivot、父尺寸变化；正且有限的 aspectRatio 校验。
- Horizontal/Vertical：保留固定参考的 control / force-expand / alignment 行为；不控制 size 时不持有 size driven claim。固定提交没有 child scale 或 reverse arrangement，未添加后续版本选项。
- ContentSizeFitter：min/preferred、自身横纵顺序及驱动轴恢复。
- DrivenRectTransformTracker：组件 epoch + session-owned 弱注册表和 Behaviour subscription，禁用/销毁/重载释放；重复布局保留 authored 值，按属性轴增量持有与释放，冲突明确报错。
- LayoutChangeObserver：会话内观察几何、层级、成员、激活与布局输入变化，只重建变化的根；已去掉每帧无条件 MarkLayoutForRebuild。当前仍读取变化快照，尚未用原生 revision 消除所有查询。
- 重建期组件参数变更延迟到下一帧；正常 RectTransform 布局写回不产生循环。递归 ForceRebuildLayoutImmediate、父布局与 fitter 重复驱动同一属性均报错并由宿主记录。
- 保留 A 的 UI.Rebuild sessionResources/GetRegistry/Clear；B 的 ClipperRegistry.Cull 已接在布局/Notify 后、graphic PreRender 前。Tween 真实 delta 由 A 的 TickUI 每场景帧推进；E 的 FontUpdateTracker.Update 在 Notify/布局前检查字体代际。

## 原生接入契约

D 专属：

- Sources/OvCore/include/OvCore/UI/DrivenProperties.h
- Sources/OvCore/include/OvCore/Scripting/CSharp/DrivenBridge.h
- Sources/OvCore/src/OvCore/Scripting/CSharp/DrivenBridge.cpp

A 集成：GameplayBridge 60–62 分发；Access.Query 注入宿主分配的 managed session ID（R6 context 地址不是托管 session ID），closing 允许 61/62。项目代码不能指定 session ID。

- 60：component=RectTransform epoch handle；argument=新增 mask；text=空（创建）或既有 token 的十进制；返回不复用的 token。
- 61：argument=token；text=释放 mask 的十进制，空表示全部；验证 session/token 后仅恢复持有字段；允许 closing。
- 62：查询当前 mask。
- mask：anchoredPosition X/Y 为 bit0/1；anchorMin bit2/3；anchorMax bit4/5；sizeDelta bit6/7；pivot bit8/9。
- 所有操作仅主线程，通过 R6 State.Get 验证 component lifetime。跨 session 续约/释放、未知 token、非法 mask 拒绝。

B 集成：CRectTransform 持有 DrivenProperties，公开 GetDrivenProperties；OnSerialize 使用 Authored(m_layout)，Inspector 标示 driven mask；恢复使用现有五个属性 setter。

与 Unity 的适配：tracker 为引用类型，防止 struct 复制产生双重所有权；仅提供引擎已有的 2D RectTransform 可驱动属性，不伪造 Z/rotation/scale 状态。

## 独立验证入口

```text
node Tests/UI/run-layout-validation.mjs
node Tests/UI/run-layout-validation.mjs --runtime
```

所有产物在 Build/UGUI-D/layout-*，每次独立目录。普通入口编译完整生产 ScriptCore 和确定性 native-access double，再编译真实 DrivenProperties 头测试；不把 double 当 GPU 验收。

已通过专项 runtime evidence：Build/UGUI-D/layout-QOxghO/result.json（9144 条断言及全部 runtime 检查）；最后补充已销毁旧父节点的异常边界后，Build/UGUI-D/layout-kT49C1/result.json 的 9145 条托管断言与原生属性测试通过。

- 9145 条生产 C# 行为断言，包括全部 24 个 Grid corner/axis/constraint × 9 alignment、非中心 pivot/stretch、全部 Aspect 模式、父 resize、HV control/expand、ContentSizeFitter、动态非 UI ILayoutElement 输入、重建期 dirty、静止帧不重算、原生几何编辑、成员变化、Inspector 直接禁用的 driven 清理和循环拒绝。
- 原生 DrivenProperties authored 值、各轴、部分释放、冲突、session 隔离测试通过。
- 真实引擎 24 种 Grid corner/axis/constraint + 2 种 Aspect 场景（stretch、非中心 pivot、父 resize）共 26 帧，300×200 RGBA，普通 / RenderThread 逐字节一致。像素 SHA-256：478d28c87cea52e8dca33b1579e9d3b7149e1ae1477dbb2f22096e33c9d470d4。
- 真实原生几何、像素、Inspector 禁用/启用、XML authored 值、热重载、组件删除、共享 R6 context 的跨 managed session token 拒绝、session closing 恢复、ContentSizeFitter 和两代 collectible context 回收均通过。
- 标准 Debug：B 的 Build/UGUI-B/debug-build-3.log 成功退出 0。两份 OvEditor.exe 大小 43059200、时间 2026-09-20T17:34:38.899Z、SHA-256 eec1bfae9540b6042faac9a0d4bd3518bbec9cf28aaa01e5fe6d034e70b85cd3 完全一致；证据 Build/UGUI-D/editor-binary-consistency.json。
- 控制模式切换先释放旧驱动轴，避免 Inspector 切换 Aspect 模式使用旧驱动尺寸，以及关闭 childControl 后覆盖用户新输入。
- 尺寸写回通知不再次排队；真实参数变化保留延迟 dirty。销毁恢复对宿主自有 BridgeException 类型正确判定 native epoch 失效，活句柄恢复错误仍会报告。重设父节点后旧父节点已销毁的只读查询同样按宿主异常类型安全退出，并有独立回归。
- 标准总回归：Build/UIValidation/Debug-9ajrk6/result.json，native-ui、managed-ui（含首个静止帧不重算断言）、managed-demo、S3 default / render-thread 全通过。
- 示例 host generation：A 执行成功，Examples/ManagedUI/.overload/csharp/build.xml generation 5d6f08a6-7705-4226-8ee7-070da003331d；日志 Build/UIValidation/u1-example-generation.log。该 generation 在最后调度修复前生成，A 汇总时须再核对/重建至最终源码。

## 尚待全阶段门禁与交接

- editor-camera：Build/JobsValidation/Skinning/Debug-editor-camera-editor/current/run.log，失败项 navigation overlay hit region follows Scene viewport；不属于 D 布局文件，未覆盖其他负责人改动。修复后由 A 重新确认门禁。
- 最终 ScriptCore 调度修复后的 Examples/ManagedUI host generation 由 A 串行刷新（既有成功 generation 如上）。
- A 已授权 D 的实现里程碑整体提交 UI.Rebuild.cs（包含 A 的 registry 前置 hunks）；其余 A/B/E 共享集成由原负责人提交。实现里程碑 commit：223ae46；其后模式切换修复与 runtime 验收收尾另行提交。不整体暂存其他任务改动。

工作起点 HEAD：870c494538629e8c06e825297a8387cf293f284e；U1/A、U2/B、U6/E、U7/F 接口为共享工作区正在验证的生产改动，不把它们描述为已发布提交。

参考：Build/Reference/uGUI，2018.4 / 7ae6166c9b673a966f62905953f4bc6b3153a675。实现按参考行为重写；不声明 UnityEngine 二进制兼容。

## A 最终共享验收交接

- D 第四轮最新完整证据：`Build/UGUI-D/layout-osZabR/result.json`，9145 断言，26 帧逐字节一致。
- A 最终标准 UI/managed/foundations/S3：`Build/UIValidation/Debug-GN2U0m/result.json` 成功。
- 最终示例 generation：`0c94c9a2-b47a-442d-b68f-30d86ce401af`，见
  `Build/UIValidation/u1-final-example-generation.json`。
- 第四轮两份 OvEditor 完全一致：`Build/UIValidation/u1-final-editor-binaries.json`。
- editor-camera 最终通过 8 组渲染组合：`Build/JobsValidation/Skinning/Debug-editor-camera-editor/result.json`；
  仅隐藏窗口的 OS cursor warp 检查明确 SKIP，实际 ImGui 导航点击已另行回放验证。
- D 实现提交为 `223ae46`、`fee3e11`。工作包验收通过；整个 uGUI 类型级矩阵、
  U1 剩余依赖及总阶段 flag 仍由 A 继续收口，不借此宣称 U0–U13 全部完成。
