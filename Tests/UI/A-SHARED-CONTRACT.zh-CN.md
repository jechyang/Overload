# A 主线公共接口交接

当前为已验证的集成里程碑，U1 整体仍未完成。所有构建与证据存放于 H 盘。

## 字段与持久引用

- 固定 48 字节字段视图和 16 字节 scalar union 保持不变。
- tag 19/20/21/22/23 分别为 Vector2、Vector4、Rect、RectOffset、Color；
  浮点必须有限，Rect 为 min.x/min.y/size.x/size.y，RectOffset 为 left/right/top/bottom。
- tag 24 为 `ComponentRef<T>`：scalar 保存 Actor GUID，UTF-8 保存声明类型与实际组件类型。
  保存稳定身份，不持有组件或程序集对象；按调用者场景解析，失效组件返回 null。
  实际托管类型优先使用 ScriptId，否则使用程序集短名和完整类型名；接口/基类引用保留实际类型。
  重名身份有多个匹配组件时拒绝猜测。
- 枚举校验声明值或 Flags 位集合；文本限制 64 KiB。失败写入不得部分修改字段。
- 数组/List 的内部 tag 25 codec 已经过独立契约测试，限制每层 1024 项、嵌套 8 层、
  总编码 64 KiB。**生产字段发现暂不暴露 tag 25**，待原生 XML、Inspector、引用重映射
  验收完成后再启用；未知字段原始记录继续保留。
- ColorBlock、SpriteState、Navigation 和安全 Actor 克隆仍属于 A 后续 U1 工作。

## Actor 与生命周期

- `GetSiblingIndex/SetSiblingIndex/SetAsFirstSibling/SetAsLastSibling` 支持根节点和子节点。
  正索引越界夹至末尾，负索引拒绝；原生同步 flat scene actor list 与 child list。
- `GetComponentsInChildren<T>` 返回先自身、后有序深度优先的快照；默认跳过 inactive 分支。
  `GetComponentsInParent<T>` 从自身逐级向上，按 includeInactive 过滤。
- `Actor.Active` 延用引擎的 dispatch 边界：回调内部变更在当前分发结束后生效。
- `Actor.Destroy()` 标记销毁，帧末收集；`Actor.IsAlive` 对 pending-destroy/失效句柄返回 false。
  IsAlive 仍检查主线程、有效会话和构造期，只有原生 INVALID_HANDLE 转为 false。
- 行为销毁即使 OnDestroy 和 native UI release 同时抛异常，也释放全部订阅、清除 owner。
- 无 RectTransform 的 EventSystem/InputModule/Raycaster 可参加 UI 生命周期；
  NotifyNativeChanges 与 SetDirty 不为其强制创建布局组件。

## 并行模块桥接

| 范围 | 接入与所有权 |
| --- | --- |
| B：40–59 | Native UI、网格、遮罩与只读命中；完整网格编码由 B 的 UIMeshEncoding 负责 |
| D：60–62 | driven acquire/release/query；Host 自动加入 managed session ID，禁止把长期存活的 R6 context 当作 generation |
| F：80–88 | 输入快照、剪贴板、2D/Bullet 查询和 camera projection；详见 U7-PROGRESS |
| E：100–103 | 可变二进制 font batch/kerning/release/bind-page；Host 校验 64 B–1 MiB 并传入 session ID |

- closing 只放行已有只读查询与精确清理：driven 61/62、font 102、
  group `(50, argument=0, text=release)`、rect mask `(52, argument=0)`。
- font `(owner=0, receipt=0, op102)` 在 session 关闭时释放该代全部 native font 状态；
  发生清理错误仍先清 metadata/registry 并退休项目程序集，再报告错误。
- Scene.LateUpdate 在 Actor 回调后每场景推进一次 UI Tick，避免多视图渲染重复推进 tween。
  当前原生入口的 scaled/unscaled delta 都来自同一 scene delta；引擎尚无独立 time-scale 时钟。
- FontManager 的引擎/项目资源根由 Editor 与 Game Context 统一初始化。

## 提交与验证

- A 公共工具：`f6ac031`；D 布局：`223ae46`、`fee3e11`；F 输入：`aa6467e`；B 图形：`d8acb1f`。
- 第 4 轮完整 UI/managed/foundations/S3 回归：`Build/UIValidation/Debug-VSMChf/result.json`。
- Actor 层级专项：`Build/UIValidation/u1-hierarchy-7xstZl/result.json`。
- codec/异常清理/发现边界：`Build/UIValidation/field-codec-7kNael/result.json`，588 断言。
- 当前 Inspector 类型化控件、字段显示名与顺序包含并行编辑器任务的共同实现。
  A 集成提交只接收共同字段文件；其余 SceneView/Gizmo/CameraController 文件仍由编辑器任务管理。

U1 未完成项、原始失败与后续修复证据详见 U1-PROGRESS.zh-CN.md。
