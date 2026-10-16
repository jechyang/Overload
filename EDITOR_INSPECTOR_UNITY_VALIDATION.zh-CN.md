# Inspector 面板对标 Unity 改造与验收

日期：2026-10-16。Debug/x64，MSVC 14.42、Windows SDK 10.0.22621.0，覆盖默认渲染与 RenderThread 模式。

## 本轮完成项（I1–I7）

### 1. 组件上下文与 `...` 菜单补全（I1 & I2）
- **新增动作**：在 `Inspector::_AttachComponentMenu()` 中补齐 **Paste component as new** 与 **Remove component**，与原有的 **Copy component values** / **Paste component values** / **Reset component** 组成完整的 Unity 风格组件菜单。
- **行为对齐**：
  - `CanPasteComponentAsNew()` / `PasteComponentAsNew()`：仅当剪贴板非空且所有选中目标实体均允许挂载该组件类型时启用，执行时批量新增组件并反序列化剪贴板属性，合并为单步 Undo/Redo。
  - `RemoveComponents(type)`：组件头部的 `X` 关闭按钮与菜单项 **Remove component** 复用同一移除实现，支持多选批量移除与单步 Undo/Redo。

### 2. Unity 风格 Tag 下拉选择器与项目自定义 Tag 管理（I3）
- **项目级 Tag 数据层（`Context`）**：
  - 内置 Unity 标准 Tag（`Respawn`、`Finish`、`EditorOnly`、`MainCamera`、`Player`、`GameController`）与 `Untagged` 空值映射。
  - 支持在 `.ovproject`（`[tags]` 节，`count` + `tag_0..N`）中持久化自定义 Tag，提供 `GetBuiltInTags()`、`GetCustomTags()`、`GetAvailableTags()`、`AddCustomTag()`、`RemoveCustomTag()`。
- **Inspector `TagSelector` 下拉控件**：
  - 取代原有的自由文本输入框，支持多选 `-- (Mixed)` 状态显示、保留场景中未注册的历史 Tag、以及底部的 `Add Tag...` 快捷跳转项。
- **Project Settings -> Tags 管理分区**：
  - 新增 `Tags` 折叠组与 `ProjectSettings::FocusTags()`，支持查看内置只读 Tag、新增与删除项目自定义 Tag。

### 3. Inspector 选择历史回退与前进（I4）
- **基于 GUID 的选择历史栈**：
  - 在 `Inspector` 中记录最多 64 条实体单选/多选 GUID 快照，提供 `CanSelectBack()`、`CanSelectForward()`、`SelectBack()`、`SelectForward()`、`ClearSelectionHistory()`。
  - 自动跳过已被销毁的实体，清空当前选择后仍可回退。
- **入口**：
  - `Inspector` 顶部工具栏最左侧新增 `<` / `>` 按钮；
  - 主菜单 `Edit` 新增 `Previous Selection`（`Alt+Left`）与 `Next Selection`（`Alt+Right`）。

### 4. 统一资源检视（Asset Inspector）与内嵌预览区（I5）
- **Project 面板单击即检视**：
  - 在 `AssetBrowser`（`Project` 窗口）单击任意资源文件或右键选择 `Properties`，直接调用 `Inspector::FocusAsset()` 在同一个 `Inspector` 面板中展示资源详情，告别独立弹窗式的割裂体验。
- **三段式 Unity 风格布局**：
  - **资源头部**：显示文件图标、资源名称、类型及只读标记，右侧提供 `Asset View`（在 3D/2D 资产视图中打开）与 `Open` 按钮。
  - **Import Settings**：针对 `Model` 与 `Texture` 资源直接读写 `.meta` 导入配置，支持 `Revert`、`Reset`、`Apply`（保存并热重载资源）及搜索框过滤。
  - **Asset Info & Preview**：展示资源路径、磁盘大小、`.meta` 状态、纹理分辨率、模型网格/材质槽数、材质 Shader、预制体实体数量，并在底部 `Preview` 折叠区提供纹理缩略图、模型网格材质槽摘要、材质快捷编辑按钮及预制体一键实例化按钮（`Instantiate in Scene`）。
- **锁定（Lock）与搜索联动**：
  - 锁定 `Inspector` 时可固定检视某个资源或实体，互不干扰；解锁后自动同步至最后选中的场景实体或资源。

### 5. Prefab 实例顶栏动作（Select / Revert / Apply）（I6）
- **Prefab 实例自动识别**：
  - `Inspector::CanEditPrefabInstance()` 自动向上追溯当前单选实体或其父级是否具有 `PrefabSource`。
- **Unity 风格 `Prefab` 动作行**：
  - 当检视 Prefab 实例（或其子实体）时，在 `Tag` 行下方渲染对齐的 `Prefab` 行，提供三个等宽按钮：
    - `Select`（`SelectPrefabAsset()`）：在 `Inspector` 中直接定位并检视源 `.ovprefab` 资产；
    - `Revert`（`RevertPrefabInstance()`）：将当前 Prefab 实例回滚至 `.ovprefab` 资产状态并立即刷新 UI，支持 Undo/Redo；
    - `Apply`（`ApplyPrefabInstance()`）：将当前实例上的修改写回源 `.ovprefab` 资产。
  - `Identity & Prefab` 中的 `Prefab Source` 字段同步支持子实体向上解析源预制体路径。

### 6. Unity 风格组件独立启用勾选框（I8）
- **核心数据与生命周期联动（`AComponent` & `Behaviour`）**：
  - `AComponent` 引入 `m_enabled`、`IsSelfEnabled()`、`IsEnabled()`（综合自身启用与属主 `Actor::IsActive()`）、`SetEnabled(bool)`。
  - 特殊保护：`CTransform` 作为坐标基准不可被禁用（`SetEnabled(false)` 静默忽略）。
  - 生命周期分发：组件启用/禁用状态变更时触发 `OnEnable()` / `OnDisable()`，并正确受 `Scripting::DispatchScope` 保护。
- **运行时子系统剔除与生效**：
  - `Scene`：主相机查询 `FindMainCamera()` 自动忽略禁用的 `CCamera`；蒙皮骨骼动画批处理忽略禁用的网格渲染器。
  - `SceneRenderExtractor` & `SceneFrameExtractor`：灯光（`CLight`）、模型渲染（`CModelRenderer`）、材质渲染（`CMaterialRenderer`）、反射探针（`CReflectionProbe`）、后处理（`CPostProcessStack`）及阴影投射均遵循 `IsEnabled()`。
  - 音频与物理：`CAudioSource` 在禁用时静音/暂停，启用时恢复；网格与音频不生成多余的抽取开销。
- **序列化向后兼容**：
  - 采用增量式 XML 序列化：组件启用时省略 `<enabled>` 标签，仅在禁用时写入属性或标签 `<enabled>false</enabled>`，实现与现有所有场景、预制体及单元测试 100% 字节级兼容。
- **Inspector UI 与多选编辑**：
  - `GroupCollapsable` 头部支持 `checkable` 复选框，位置避开折叠箭头点击热区，支持多选混合态 `(Mixed)`。
  - 勾选变更高频手势自动接入 `EditorActions` 场景历史事务，支持完整的 Undo/Redo。

### 7. Hierarchy 与 Viewport Unity 工作流对齐
- **从 Project / 资源浏览器向 Hierarchy 拖拽资产（Model / Prefab / Material / Sound / Scene）**：
  - 拖至 Hierarchy 空白处或场景根节点行：在场景根级别实例化，3D 模型以文件名自动命名并装配渲染组件，Prefab 自动绑定预制体引用并高亮；Scene 文件直接加载。
  - 拖至 Hierarchy 现有实体节点上：自动建立父子层级关系并选中新实例。
  - 拖拽 Material 至包含 `CMaterialRenderer` 的实体节点：自动为该实体赋予材质。
  - 拖拽 Sound 音频至实体节点：自动配置或添加 `CAudioSource` 并赋值音效资源。
- **Hierarchy 实体右键上下文菜单（Unity 布局对齐）**：
  - 新增顶级高频菜单项 `Create Empty Child`（快捷键 `Alt+Shift+N`），在选中实体下立即创建空子物体并进入重命名。
  - 右键空白处提供 `Create Empty`（快捷键 `Ctrl+Shift+N`）。
  - 右键菜单增加与 Unity 完全一致的分隔线与快捷键提示（`Ctrl+C`、`Ctrl+V`、`Ctrl+D`、`F2`、`Delete`）。
- **全局快捷键与聚焦联动**：
  - 当 Hierarchy 或 SceneView 获得焦点时，按键盘 `F` 键即可在 SceneView 视口中将摄像机平滑居中聚焦于选中的实体（无需鼠标悬停在视口上）。
  - 支持 `Alt+Shift+N`（创建子物体）与 `Ctrl+Shift+N`（创建根物体）。
  - 修复 `MenuBar` 快捷键判断，避免 `Ctrl+Shift+N` 误触 `New Scene`。

### 8. UGUI 原生创作工作流与编辑器深度集成对标 Unity
- **Hierarchy / MenuBar 完整 UI 类别菜单**：
  - 在 Hierarchy 右键上下文菜单、Hierarchy 顶部 `+` 创建下拉菜单、主菜单栏 `GameObject` 菜单中新增 **`UI`** 分类，包含 9 项标准控件：
    - `Canvas`
    - `Panel`
    - `Image`
    - `Text`
    - `Button`
    - `Toggle`
    - `Slider`
    - `Scroll View`
    - `Event System`
- **智能 Canvas 自动挂载与 EventSystem 伴生机制（Unity 行为对齐）**：
  - 创建任何 UI 元素（Panel、Image、Text、Button 等）时，系统自动检查当前选中实体是否在 Canvas 层级下。
  - 若未选中实体或选中非 UI 实体，系统自动寻找场景中现有的根 Canvas；若场景中无 Canvas，则自动创建带有 `CCanvas` 与 `CRectTransform` 的 `Canvas` 实体及伴生 `EventSystem` 实体，并将新 UI 元素置于其下。
  - UI 元素本地空间坐标强制校准为 `(0, 0, 0)` 并配置预设 Anchor、Pivot 与 SizeDelta，避免因 3D 相机生成偏移飞出画板。
- **Inspector RectTransform 保护机制（Unity 行为对齐）**：
  - `CRectTransform` 作为 2D UI 空间变换核心基准，在 Inspector 中与 `CTransform` 享受同等保护：不显示启用勾选框（non-checkable），不允许单独关闭/移除（non-closable），右键菜单不显示 `Remove component`。
- **2D UI 视口平滑聚焦（`F` / FrameSelection）**：
  - 扩展 `SelectionBounds::From`，支持提取 `CRectTransform::GetLocalCorners()` 并经由世界矩阵变换投影至世界包围球/包围盒。
  - 选中 Canvas 或任意 2D UI 元素后按 `F` 键，视口相机平滑聚焦至该 UI 矩形的中心与实际尺寸，告别空点聚焦。

## 实际验证

```powershell
node Tests/Editor/run-shell-validation.mjs
node Tests/Editor/run-panel-validation.mjs
```

- `node Tests/Editor/run-shell-validation.mjs`：**93 项检查全部通过**，在真实 OpenGL 上下文中生成实机截图（位于 `Build/EditorUnity/ShellLabOutput`）：
  - `12-inspector-tag-dropdown.png`：Inspector 顶部 `<` / `>` 选择历史按钮与展开的 Tag 下拉列表（含 `Untagged`、内置 Tag、自定义 Tag 与 `Add Tag...`）；
  - `13-project-settings-tags.png`：`Project Settings -> Tags` 自定义 Tag 管理界面；
  - `14-inspector-component-menu.png`：组件右键/`...` 菜单（含 `Paste component as new` 与 `Remove component`）；
  - `15-inspector-asset-preview.png`：单击 `Project` 纹理资源后，`Inspector` 展示头部、`Import Settings`、`Asset Info` 与底部 `Preview` 纹理预览；
  - `16-inspector-prefab-row.png`：选中 Prefab 实例时，`Inspector` 头部展示 `Prefab`（`Select` | `Revert` | `Apply`）操作栏与 `Identity & Prefab` 源引用；
  - `17-inspector-component-checkbox.png`：Inspector 组件头部独立启用复选框（`Transform` 保护不提供复选框，`Point Light` 显示禁用 `[ ]`，`Camera` 显示启用 `[✓]`，画面联动更新）；
  - `18-hierarchy-workflow.png`：Hierarchy 拖拽资源实例化（根级 Prefab、模型自动命名、拖到父实体生成子级模型与子级 Prefab、Create Empty Child 及全局 `F` 视口聚焦）；
  - `19-ugui-editor-workflow.png`：UGUI 自动 Canvas 挂载与全套 UI 控件创建（Canvas、Panel、Button、Text、Toggle、Slider、Scroll View）、Inspector 中 `Rect Transform` 保护（无勾选框/不可移除）以及视口 `F` 键 2D UI 矩形自适应聚焦。
- `node Tests/Editor/run-panel-validation.mjs`：`default` 与 `render-thread` 双模式全部通过。

