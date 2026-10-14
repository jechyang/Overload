# 编辑器 Unity 风格外壳改造与验收

日期：2026-10-13。Debug/x64，MSVC 14.42、Windows SDK 10.0.22621.0，默认渲染和 RenderThread。

## 本轮改动

- **Unity Dark 主题**（新默认）：#383838 面板、#282828 标签栏、#191919 间隙、钢蓝色选中/焦点；
  Inspector 属性标签改为中性灰，组件标题为独立灰色条，标签页关闭按钮仅悬停显示。
  旧配置若仍是原默认主题（Alternative Dark）会一次性迁移，主动选择过的主题保持不变。
- **主工具栏**固定在菜单栏下方：左侧 W/E/R 工具和 Pivot/Center、Local/Global；
  中间 Play/Pause/Step（Play 再点即停止）；右侧脚本重载与 Layout 下拉。点击工具栏不抢走 Scene/Hierarchy 的键盘焦点。
- **状态栏**：最新一条 Console 日志（点击打开 Console），C# 构建状态、播放状态、场景名与未保存标记。
- **菜单**改为 File / Edit / Assets / GameObject / Component / Window / Help；原 Settings 移入
  Edit > Preferences；Window 菜单分为 General / Rendering / Analysis / Scripting；
  Component > Add... 复用 Inspector 的 Add Component 选择器。
- **布局**：代码生成的 Default（Hierarchy | Scene/Game | Inspector，下方 Project/Console）和 2 by 3 预设，
  随分辨率自适应。面板使用稳定 ImGui ID（`###<面板键>`），布局文件改为 `editor_layout.ini`；
  旧 `layout.ini` 引用的是按创建顺序生成的 ID，已失效，因此首次启动会使用新默认布局。
  `Sources/OvEditor/layout.ini` 及其 premake 复制步骤已不再被代码使用。
- **未保存跟踪**：基于撤销历史的版本号，撤销回保存点即恢复为已保存；标题与状态栏显示 `*`；
  新建/打开场景、拖入/双击场景、关闭编辑器时提示保存（是/否/取消）。新增 Ctrl+O、Ctrl+B、
  Ctrl+P、Ctrl+Shift+P、Ctrl+Alt+P。
- **播放模式着色**（可在 Preferences > Appearance 关闭或改色）。
- Project 窗口按钮改为中性样式；Frame Info 默认关闭；Scene 视图浮层去掉与主工具栏重复的工具行。
- 修复：Project 窗口 *Create > Scene* 会把当前打开场景的保存路径改成新建的场景文件，之后 Ctrl+S 会覆盖它。

## 实际验证

```powershell
node Tests/Editor/run-shell-validation.mjs
node Tests/Editor/run-panel-validation.mjs
```

- `run-shell-validation.mjs`：隔离 AppData、隐藏窗口的真实编辑器，36 项通过，覆盖默认主题、
  稳定 ID、两种布局的停靠位置、工具栏/状态栏工作区、修改/撤销/重做/保存点、
  保存其他场景不改变当前路径、播放着色与暂停/停止切换。截图在 `Build/EditorUnity/ShellLabOutput`。
- `run-panel-validation.mjs`：默认渲染与 RenderThread 各 574 项通过（原 573 项 + 工具栏检查）；
  Scene 浮层的 Rotate/Local/Pivot 真实点击改为点击主工具栏。

关闭编辑器时的保存提示是系统模态框，未做自动化，需要手动确认一次。

## 第二轮（同日）

- **Hierarchy**：顶部改为 Unity 式单行 `+ ▾` 创建下拉（与 GameObject 菜单同源的创建项）+ 搜索框；
  新增场景根行（场景图标、名称、未保存 `*`，可折叠，右键 Save Scene / Expand All / Collapse All / Create，
  拖入即移到根级）；实体计数仅在搜索时显示（"2 of 100 entities"），空场景给出提示；
  预制体实例名称显示为蓝色，未激活实体变灰。原 Expand All / Collapse All 按钮移入场景行右键菜单。
- **Console**：工具栏改为 Clear / Collapse / Clear on Play / Error Pause 切换按钮，右侧搜索与
  消息 / 警告 / 错误三个带计数的筛选开关（消息包含 Default 与 Info 两级）。Error Pause 通过
  EditorActions 的延迟动作在下一帧边界暂停，Console 标签页隐藏时同样生效。
  日志行时间戳改为与消息同行，多行消息悬挂缩进。
- **Game 视图**：工具栏右侧新增 Stats 开关，在画面右上角显示 FPS、帧时间和渲染分辨率。
- **修复**：Project 窗口文件夹树宽度在应用窄布局（2 by 3）后会被永久夹到 100px，
  切回 Default 也无法恢复；现在只夹紧绘制宽度，保留用户偏好宽度。

### 验证

- `node Tests/Editor/run-shell-validation.mjs`：48 项通过（新增 Error Pause 三项、文件夹树宽度恢复、
  Console / Hierarchy 搜索 / Game Stats 截图 `7-console`、`8-hierarchy-search`、`9-game-stats`）。
- `node Tests/Editor/run-panel-validation.mjs`：默认渲染与 RenderThread 各 574 项通过。

## 第三轮（2026-10-14）

- **Inspector 顶部**：改为 Unity 式“激活勾选框 + 名称”同一行，Tag 在下一行。行内保留隐藏的
  Active / Name 标题，多选批量编辑、(Mixed) 标记、属性右键菜单（复制/粘贴/重置）照常工作；
  默认值键改为 `0/1/Active`、`1/1/Tag`。OvUI 横向 `Group` 现在不为禁用的子控件分配列。
- **Hierarchy 原地重命名**：F2 或右键 *Rename* 在行内显示输入框，Enter 或点击别处提交、Esc 取消；
  提交是可撤销的场景编辑。取代原来的 “Rename to...” 子菜单。实现位于 OvUI `TreeNode::BeginRename()`。
- **Console 详情区**：点击日志行即选中（高亮），列表下方显示完整消息（可选中复制），
  中间分隔条可拖动调整高度。

### 验证

- `node Tests/Editor/run-shell-validation.mjs`：59 项通过。新增真实键盘输入的 F2 重命名
  （提交、Esc 取消、撤销恢复）与 Console 详情区检查，截图 `10-hierarchy-rename`、`11-console-detail`。
- `node Tests/Editor/run-panel-validation.mjs`：默认渲染与 RenderThread 各 574 项通过。
