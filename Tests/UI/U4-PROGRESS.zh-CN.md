# D / U4 布局工作包

状态：实现与独立行为测试已落盘；真实原生 / GPU / 热重载及标准构建验收正在进行，尚未宣称完整 U4。

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

已通过 evidence：Build/UGUI-D/layout-pEwOQW/result.json。

- 9140 条生产 C# 行为断言，包括全部 24 个 Grid corner/axis/constraint × 9 alignment、非中心 pivot/stretch、全部 Aspect 模式、父 resize、HV control/expand、ContentSizeFitter、动态非 UI ILayoutElement 输入、重建期 dirty、静止帧不重算、原生几何编辑、成员变化、Inspector 直接禁用的 driven 清理和循环拒绝。
- 原生 DrivenProperties authored 值、各轴、部分释放、冲突、session 隔离测试通过。
- 真实引擎测试源码与独立 ManagedHost 已编译；首次链接因并行 F QueryInput 未入库失败（Build/UGUI-D/layout-rJ7WzW/runtime-link.log），等待共享库构建后重跑。B 第二轮构建因 Frame.cpp 的 DotProduct 名称失败，已由 B 改为 Dot，待下一轮统一构建。

## 尚待最终验收

- --runtime 的生产 frame/像素、RenderThread、XML、热重载、删除和 closing 全通过。
- 标准 Debug 构建、UI/managed/scripting 回归、editor-camera；Build/Debug 与 Bin/Debug 的 OvEditor.exe 大小/时间/SHA256 一致。
- 修改托管层后的 Examples/ManagedUI host generation 由共享输出负责人串行重建。
- A 已授权 D 的实现里程碑整体提交 UI.Rebuild.cs（包含 A 的 registry 前置 hunks）；其余 A/B/E 共享集成由原负责人提交。里程碑提交不代表完整 U4 验收通过；不整体暂存其他任务改动。

工作起点 HEAD：870c494538629e8c06e825297a8387cf293f284e；U1/A、U2/B、U6/E、U7/F 接口为共享工作区正在验证的生产改动，不把它们描述为已发布提交。

参考：Build/Reference/uGUI，2018.4 / 7ae6166c9b673a966f62905953f4bc6b3153a675。实现按参考行为重写；不声明 UnityEngine 二进制兼容。
