# UGUI 并行工作包与会话启动指令

日期：2026-09-21。本文补充 `UGUI_IMPLEMENTATION_PLAN.zh-CN.md`，不缩减其实现及验收范围。
U0 已提交为 `2c44e0e`；本文的“13 个阶段”指 U1–U13。

## 当前基线

- U0 已完成。U1 正在主会话实现，尚未完成或提交。
- U1 工作区已有集合、事件、会话注册工具，以及正在验收的 Vector2/Vector4/Rect/RectOffset/Color 字段扩展。
- 新 worktree 从提交创建时不会包含这些未提交改动。不要假定 `UIEvent`、`UISessionRegistry` 或新字段 ABI 已在分支基线中。
- 工作区同时有反射、Inspector、编辑器操作和渲染相关改动。`git status` 和逐文件 diff 才是当前事实；不得整体暂存、回滚或覆盖。
- 主会话此前已启动一次标准 Debug 构建。共享目录下启动第二次构建前必须检查该进程是否结束；日志本身不能当作进程仍存活的证明。

## 第一批建议开哪些会话

保留 A 主会话，先开 B、D、E、F 四个实现会话：

| 工作包 | 阶段 | 可以立即做的部分 | 完整接入条件 |
|---|---|---|---|
| A 基础与集成 | U1，最后 U13 | 字段/引用/列表、Actor API、共享协议、集成 | U13 等所有阶段真实验收 |
| B 图形管线与遮罩 | U2 → U5 | UIVertex、VertexHelper、modifier、材质槽和快照 | U5 在 U2 完成后接入；使用 A 的字段/生命周期接口 |
| C Sprite 与 Image | U3 | Sprite 元数据、各种几何模式的独立算法测试 | 在 B 的顶点/批次契约固定后启动完整实现 |
| D 布局 | U4 | Grid、AspectRatioFitter、布局算法、循环检查 | driven 原生桥接由 A 接入；与 B 一起验收脏标记 |
| E 字体与文本 | U6a → U6b，合计 U6 | U6a：原生动态 SDF、批量字形、指标与图集生命周期 | U6b：C# Text；GPU 多页和 mask 验收依赖 B |
| F 输入和事件 | U7 | 输入快照、事件数据、分发、输入模块、物理查询 | GraphicRaycaster/遮罩过滤依赖 B，引用/会话能力依赖 A |
| G 基础交互控件 | U8 → U9 → U10 | 前置完成后实施 Selectable、基础控件、ScrollRect | A/B/C/D/F，以及 E 提供的文字用于综合示例 |
| H 高级控件和工厂 | U11 → U12 | 前置完成后实施 InputField、Dropdown、DefaultControls | A 的安全克隆，E 的 Text，F 的文本输入，G 的控件 |

B 和 F 可先建立原生/托管数据契约并开发自己模块；A 的接口尚未提交时，不复制一份
临时 UIEvent、字段系统或 registry。明确记录待接入依赖，避免最终出现两套生命周期。
C 若需要立即开会话，应先限制在 Sprite 元数据和几何算法，暂不同时改 B 的 Graphic 文件。

## 13 个阶段逐项依赖

| 阶段 | 负责人 | 主要前置 | 并行与顺序 |
|---|---|---|---|
| U1 | A | U0 | 现在进行；通用字段和 Actor 能力唯一负责人 |
| U2 | B | U0、U1 的集合/生命周期及通用值类型契约 | 现在进行，接口接入跟随 A |
| U3 | C | U2 的顶点/材质/资源槽、U1 Sprite 引用协议 | 资源与算法可提前，完整阶段在 U2 后 |
| U4 | D | 现有 RectTransform/layout、U1 driven 与生命周期接口 | 现在进行 |
| U5 | B | U2，U1 的生命周期 | 与 U3/U4/U6/U7 后半并行 |
| U6 | E | 原生 SDF 可独立；Text 依赖 U2，完整 mask 验收依赖 U5 | 分 U6a 原生与 U6b 托管/GPU 接入 |
| U7 | F | 快照/分发可独立；射线最终接入依赖 U2/U5 | 现在进行输入和事件骨架的实际行为实现 |
| U8 | G | U1、U2、U3、U7 | U5 CanvasGroup/遮罩语义纳入最终验收 |
| U9 | G | U8；slider/scrollbar 驱动依赖 U4 | 与 Text 后半可并行 |
| U10 | G | U4、U7、U9 | 在基础控件后实施 |
| U11 | H | U6、U7、U8；相关滚动/布局接口 | 等真实文本与输入能力具备后实施 |
| U12 | H | U1 安全克隆、U6、U9、U10、U11 | DefaultControls 完整验收须所有被创建控件可用 |
| U13 | A | U1–U12 | 局部 profiler/压力用例可提前；完整收口最后执行 |

这里的箭头表示 API/集成依赖，不要求所有算法研发都等待整个上游阶段完成。
接口“固定”至少要有生产类型定义、所有权/线程约束和一个可运行的消费者测试，不能只靠口头约定。

## SDF 的拆分边界

SDF 是适合立即独立开工的部分。E 会话拥有整个 U6，并分两个可验收的里程碑：

### U6a 原生字体后端

- 字体文件/FreeType face 生命周期，字体资源代际，glyph id、metrics、kerning 批量查询。
- 从 outline 生成单通道 signed distance field；固定 em 尺寸和 spread，不按每个 fontSize 缓存 alpha 位图。
- 批量、去重的 EnsureGlyphs 请求及缓存 key。返回 plane bounds、advance、bearing、page、UV、scale/bias/spread、代际。
- 有界、多页、append-only 图集；旧代际在在途帧仍持有它时有效。
- CPU 字形和距离场测试：内部/轮廓/外部距离、缺字、Unicode 标量、批量去重、换字体、page 增长和销毁。
- 可先实现独立的 GPU 图集资源及 SDF shader 测试，但最终 Overlay/Camera/World Space、多材质和 mask 集成必须使用 B 的正式管线。

建议新文件放在 `Sources/OvCore/include/OvCore/UI/Fonts/` 与对应 `src/OvCore/UI/Fonts/`；
测试使用 `Tests/UI/font_backend_validation.cpp`、独立 runner 和 fixtures。
文件名是归属建议，不是要求建立与现有资源管理器隔离的第二套资源系统。

### U6b C# Text 与生产渲染接入

- FontData、FontUpdateTracker、Text、richText、fallback、换行、对齐、best-fit、overflow。
- 排版和四边形生成全部在 C#，按图集页构造 B 定义的材质/纹理批次。
- shader 接入正式 UI pipeline；多尺寸/DPI/三种 Canvas/遮罩/RenderThread 的真实像素验证。
- 编辑首帧、脚本热重载、销毁/切场景、资源卸载及在途帧代际测试。

U6a 完成可以提交它自己的里程碑，但 U6 保持 partial。不能把 CPU SDF 或独立 shader 测试
当作整个 Text/U6 完成。E 不实现输入编辑策略、caret、selection 或 InputField；它们属于 H。

## 文件归属与必须交接的接口

| 模块/文件 | 唯一集成人 | 其他会话的做法 |
|---|---|---|
| ScriptCore `Behaviour.cs`、`Fields.cs`、`MathTypes.cs`、公共事件/集合/注册表 | A | 使用正式公共类型，不重复定义 Vector4/Color/引用/event |
| ManagedHost `Fields*.cs`、`HostAbi.cs`、`HostABI.h`、通用字段/Inspector/XML | A | 提供新类型和 schema 需求；由 A 接入通用序列化 |
| `GameplayBridge.cpp`、宿主 `Instances.cs` 的总分发 | A | F/E/D 分别新增专属 Input/Font/Driven bridge；A 合入分发入口并分配 operation/tag，避免编号碰撞 |
| `UI.Graphic.cs`、`UI.Native.cs`、`CUI.h/cpp`、`UIBridge.h/cpp` | B | C 提供 Image/Sprite 变更；D 提供 driven 要求；E 提供字体材质/纹理需求；协调后由 B 接入共享文件 |
| UI `Frame.h/cpp`、`RenderResources.h/cpp` 与现有 UI collector | B | E 提供图集不可变资源租约，F 只读命中信息；不得各自扩展不同快照格式 |
| `UI.Rebuild.cs` | A 的 U1 registry 修改完成后交 D | B 提供 graphic registry/rebuild 的修改点，由 D 合入调度；不同时编辑同一文件 |
| layout 相关 C#、Grid/Aspect/Driven 新文件 | D | B 不实现布局算法 |
| Sprite/Image/RawImage 专属新文件 | C | 现有 Image/RawImage 位于 `UI.Graphic.cs`，先由 B 提取到专属文件后交接给 C |
| Font 专属新文件、`UI.Text*.cs`、SDF 专属 shader | E | 不把排版移到原生，不改 shared Frame 的私有分支副本 |
| Window/InputManager、Input 快照、EventSystems 专属新文件 | F | H 使用输入/字符/IME/剪贴板接口，不另建输入采集器 |
| `ColorBlock`、`SpriteState`、`Navigation` 等基础数据类型 | A | A 定义 U1 序列化类型，G 实现 U8 行为和默认值需求；SpriteRef 由 C 提供资源语义 |
| `Tests/UI/run-validation.mjs`、完整矩阵、兼容总表、最终示例 | A | 各包先维护独立 runner/测试和进度文件，交 A 合入阶段 flag 与矩阵 |

B 不得顺带改动其他渲染/FrameGraph 会话的 renderer 基础架构。若 UI 新需求必须扩展
RenderFrame、低层 GPU 命令或资源接口，先提交明确的接口补丁供该模块负责人接入。
所有会话保留 `AView.cpp` 使用主视图 `request.viewId` 的回归保护。

### 三份先固定的数据契约

1. **B → C/E/F：图形契约**：完整 UIVertex 布局、索引限制、材质/纹理槽、clip/stencil 状态、
   immutable frame 的资源所有权、raycast 可读几何与深度/排序。E 的 glyph page/material slot 对齐此契约。
2. **F → G/H：输入契约**：Game viewport 坐标、DPI/Y 翻转、时间/delta、按下/释放/重复、
   多指 ID、字符、composition、焦点和剪贴板；附确定性注入入口与示例消费者。
3. **A → 所有包：生命周期与引用契约**：session 所有权、组件引用身份/失效规则、UIEvent 清理、
   Actor 层级/克隆引用重映射、字段字节/长度上限；声明关闭 session 后允许的只读查询。

## 工作区、构建与提交

- 推荐每个实现会话使用 H 盘的独立 git worktree，例如 `H:/overload/UGUIWorktrees/<工作包>`，
  分支名使用 `codex/ugui-<工作包>`。不要让默认 worktree 位置把内容放到 C 盘。
- 本文没有创建 worktree 或新会话。创建时必须检查分支、目录是否已存在，不覆盖已有目录。
- 从 `2c44e0e` 创建的工作区没有主会话未提交的 U1。独立部分可以开工；依赖部分等 A 的已验证提交，
  不用复制全部脏工作区来获得依赖。新提交集成时也检查共享文件是否已有其他会话改动。
- 原始实施计划目前是主工作区未跟踪文件；新 worktree 缺少计划时，直接读取
  `H:/overload/Overload/UGUI_IMPLEMENTATION_PLAN.zh-CN.md` 和本拆分文档，不把未提交代码当作已交付依赖。
- Build/Bin/Bin-Int、generated reflection、managed artifacts、host generation、测试 evidence 均留在 H 盘，
  每个工作区/工作包使用独立输出。不要用一个工作区的旧库验证另一个工作区的新头文件。
- 不并行向同一 `Bin`/`Bin-Int`/`Build/Generated` 写入。若仍共用主工作区，完整构建、项目生成、
  host generation 和 Git 暂存/提交按负责人串行执行；源码只有在文件归属明确时才并行改。
- 各会话可以在自己的工作区提交已验收里程碑。主会话负责按阶段集成、总矩阵状态和最终提交；
  U6a 等子目标提交不得把上层 U6 标为完成。
- 完整阶段仍执行原计划要求的 Debug 构建、UI/managed/scripting 回归、editor-camera、
  两份 OvEditor.exe 的大小/时间/SHA-256 一致性，以及修改托管层后的示例 host generation 重建。
- Git 提交通道使用 `shell: C:\Windows\System32\cmd.exe`、`login: false`、
  `sandbox_permissions: require_escalated`；message 写到 H 盘 Build 临时文件，以 `git commit --file=...` 提交。
  提交前检查 `git diff --cached --check` 和文件清单，不用 `git add .`。

## 可以直接复制的新会话启动指令

以下公共部分加一个对应工作包段落，即为完整启动指令。路径改成该会话实际 H 盘工作区。

### 公共部分

```text
请阅读 H:/overload/Overload/UGUI_IMPLEMENTATION_PLAN.zh-CN.md 和
H:/overload/Overload/UGUI_PARALLEL_WORK_PACKAGES.zh-CN.md，
按下面指定工作包直接实现、构建和验证，不缩减原计划的最终范围。
先检查 git status、HEAD、AGENTS.md 和实际源码，保护其他会话改动。
U0 的提交为 2c44e0e；U1 仍在主会话开发，不能假定未提交的公共类型已经存在。
所有工作区、构建和验证产物放在 H 盘；阶段/里程碑完成后单独 commit，只暂存自己的文件。
C# 负责 UI 高层逻辑，C++ 负责原生资源、输入、快照与 GPU 底层。
参考固定 uGUI 2018.4 / 7ae6166c9b673a966f62905953f4bc6b3153a675。
遵守并行文档的文件归属；共享文件的修改集中交给指定负责人。
为工作包建立独立测试入口和进度记录，写明依赖契约、真实通过用例与未完成项。
不要抢改总测试入口/总矩阵，不创建同名空壳或以 stub 代替验收。
```

### B 图形与遮罩

```text
负责工作包 B：先完成 U2，再完成 U5。先固定顶点、材质/纹理槽和不可变快照契约，
通知主会话它们的类型定义及消费者测试位置。负责 Graphic/modifier/CanvasRenderer/遮罩通路。
优先将现有 UI.Graphic.cs 中的 Image/RawImage 提取为独立文件并交 C，保持行为不变。
不实现布局、字体排版或交互控件，不覆盖其他 FrameGraph/渲染会话的底层改动。
```

### D 布局

```text
负责工作包 D：完整 U4。实现 GridLayoutGroup、AspectRatioFitter、DrivenRectTransformTracker、
布局组参考行为、循环检测与精确 dirty。纯布局算法先做，可并行于 U2 和 SDF。
先与主会话确认 UI.Rebuild.cs 的 U1 registry 变更交接；原生 driven 和通用桥接改动提交给 A/B 接入。
提供全部 Grid 组合、非中心 pivot/stretch、父尺寸变化和 driven 恢复的几何及真实渲染验收。
```

### E SDF 与文字

```text
负责工作包 E：完整 U6，先实现独立的 U6a 原生动态单通道 SDF 后端，再接入 U6b C# Text。
重点是 FreeType outline 距离场、批量 EnsureGlyphs、metrics/kerning、缓存 key、
多页 append-only 图集及在途帧资源代际；不能做按字号缓存的 alpha 位图替代。
先交付有测试的 glyph 请求/记录和 page 资源契约，与 B 对齐材质槽/快照接口。
排版、fallback、richText、换行、对齐、best-fit 和顶点生成只能在 C#。
U6a 可独立提交但 U6 保持 partial；U6 完成必须有生产 UI GPU 多尺寸/DPI/遮罩/RenderThread 验收。
```

### F 输入与事件

```text
负责工作包 F：完整 U7。先做原生输入快照和确定性注入，再做 C# EventSystem/ExecuteEvents/
InputModules/Raycasters。字符、repeat、IME、焦点及剪贴板底层提前为 H 预留并实现，
编辑策略仍留给 InputField。GraphicRaycaster 最终使用 B 的图形/遮罩契约。
2D 物理查询需要可运行的最小 backend，不能空壳。不要提前实现 Button 或 InputField 状态机。
```

### C 图像（第二批，或第一批仅资源/算法）

```text
负责工作包 C：完整 U3。先做 Sprite 元数据和 Image Simple/Sliced/Tiled/Filled 几何及测试，
与 A 确认 SpriteRef 序列化、与 B 确认图形/材质槽契约和 Image/RawImage 文件交接。
覆盖全部 fill origin/clockwise、九宫格、alpha hit、packed/rotated 适配及资源热换。
C++ 不生成 Image 网格，完成阶段前接入真实 GPU 和 RenderThread 验收。
```

### G 基础交互（前置接口完成后）

```text
负责工作包 G，依次完成 U8、U9、U10，每阶段独立验收和提交。
使用 A 的 ColorBlock/SpriteState/Navigation 与 UIEvent，不重复定义字段协议。
实现 Selectable/导航/transition/tween、Button/Toggle/Slider/Scrollbar，再实现 ScrollRect。
依赖 F 的真实 EventSystem 输入和 C 的 Image；不自建简化点击系统。
Animation transition 要有可替换且可运行的默认驱动，不能 no-op。
```

### H 高级控件（前置接口完成后）

```text
负责工作包 H，依次完成 U11、U12，每阶段独立验收和提交。
使用 E 的 Text、F 的字符/IME/剪贴板、G 的控件和 A 的安全克隆。
完整实现 InputField 的 Unicode 编辑/caret/selection，再实现 Dropdown 和 DefaultControls。
不复制临时文本后端、输入系统或 Actor clone；软键盘后端缺失时据实保留 blocked。
最终 DefaultControls 每种产物都必须可交互并正确序列化。
```

## 每次交接提供什么

工作包负责人提供：commit、相对文件清单、接口契约位置、独立验证命令、H 盘 evidence、
所用依赖提交、共享文件待集成补丁、真实未完成项。主会话验证后更新阶段状态与总矩阵。
不以“文件已写好”或“独立小测试通过”替代原计划的完整阶段验收。
