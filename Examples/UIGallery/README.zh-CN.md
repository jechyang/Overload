# UIGallery — 已实现 UGUI 功能测试工程

日期：2026-09-21。本工程**只覆盖当前已落地**的 Overload uGUI 能力，不包含
U3 完整 Image 模式、U8–U12 交互控件（Button/Toggle/Slider/ScrollRect/InputField/Dropdown）
或 U13 收口内容。完整边界见 `Tests/UI/UGUI-COMPATIBILITY.zh-CN.md` 与各 `*-PROGRESS.zh-CN.md`。

## 覆盖范围

| 分区 | 场景对象 | 对应能力 |
| --- | --- | --- |
| 顶栏 | `Header` / `Title` | `HorizontalLayoutGroup`、`Text` + `Outline` |
| 左列 | `LayoutColumn` | `VerticalLayoutGroup`、`LayoutElement`、`ContentSizeFitter` 嵌套 |
| 中列 | `GridPanel` | `GridLayoutGroup` 3×3 色块（FixedColumnCount） |
| 中列 | `AspectSquare` | `AspectRatioFitter` FitInParent + 自定义 `Graphic` 棋盘 |
| 右列 | `ClipPanel` | `RectMask2D` + 长 `Text` 换行溢出裁剪 |
| 右列 | `MaskPanel` | 嵌套 `Mask`（stencil，showMaskGraphic）+ 色块 |
| 右列 | `GroupPanel` | `CanvasGroup` alpha / blocksRaycasts |
| 底栏 | `OutlineBox` / `ShadowBox` | `Outline` / `Shadow` mesh modifier |
| 底栏 | `RichLine` / `CjkLine` / `BestFitBox` | 富文本、中文/emoji、best-fit |
| 交互 | `EventSystem` + `PulseTarget` | EventSystem / StandaloneInputModule / GraphicRaycaster 指针事件 |
| 画布 | `Canvas` | `CanvasScaler` ScaleWithScreenSize 960×540 |

脚本在 `Assets/Scripts/`：

- `GalleryLayout.cs` — CanvasScaler、布局组、LayoutElement、AspectRatioFitter
- `GalleryGraphics.cs` — Image 色块、自定义 CheckerGraphic
- `GalleryText.cs` — Text 变体、Outline/Shadow、Mask/RectMask2D/CanvasGroup、EventSystem 探针

全部是生产 `Overload.UI` / `Overload.EventSystems` 类型的薄子类，构造函数写入默认值，
字段仍走共享 Inspector ABI。

## 打开与构建

1. 先构建引擎 Debug 产物：

```text
Scripts\Windows\Build.bat Debug -skip-project-generation
```

2. 用 OvEditor 打开 `Examples/UIGallery/UIGallery.ovproject`。
3. 首次打开或改脚本后，生成托管宿主与工程程序集（与 ManagedUI 相同通道）：

```text
node Examples/ManagedUI/build.mjs <Python3 绝对路径>
```

或在编辑器内打开并保存工程，触发自动 C# 构建。本机路径写在
`UIGallery.local.ovproject`（`csharp_engine_root` / `csharp_node` / `csharp_python` /
`csharp_runtime_root`），请按机器修改。`csharp_managed_root` 与
`csharp_project_assembly` 留空，由生成流程填充。

4. 打开 `Assets/Scenes/UIGallery.ovscene`。编辑态首帧应直接看到 UI；进入 Play 后
   悬停/点击蓝色 `PulseTarget`，观察 EventSystem 指针事件变色。

## 期望现象

- 顶栏标题描边，三栏布局随窗口缩放（CanvasScaler）。
- 左列卡片按 LayoutElement 排布，嵌套列由 ContentSizeFitter 收缩。
- 中部 3×3 网格由 GridLayoutGroup 驱动；下方棋盘方块由 AspectRatioFitter 保持 1:1。
- 右上长文本被 RectMask2D 裁切；右中色块只在 Mask 可见区内绘制。
- 右下 CanvasGroup 半透明压暗子树。
- 底栏可见 Shadow/Outline、富文本、中文/emoji、best-fit。
- 蓝色 `PulseTarget`：悬停变绿、按下变橙、点击变黄。

## 场景生成

场景由 `tools/generate_scene.py` 生成（48 个 Actor），避免手写 XML 层级错误。
修改布局后重新生成：

```text
& $env:MIMO_PYTHON tools/generate_scene.py
```

## 像素自测（锚点断言 + 预览）

```text
node Tests/UI/run-gallery-validation.mjs
```

流程：

1. 编译 ManagedHost + `GameScripts.dll`（Gallery 脚本）
2. 编译并运行 `Tests/UI/gallery_validation.cpp`（default / render-thread）
3. 加载 `UIGallery.ovscene`，首帧准备 UI，多趟嵌套布局收敛
4. GPU 读回 960×540 RGBA，对 Grid 色块 / PulseTarget / Mask / 裁剪 / 文本做锚点色值断言
5. 校验 default 与 RenderThread **逐字节一致**
6. 导出 `preview.png`（以及 raw `gallery.rgba`）便于目检

证据目录：`Build/UGUI-Gallery/gallery-*/result.json`。

注意：同一 Actor 上不要同时放 `ContentSizeFitter` 与布局组（会触发
“multiple controllers drive the same RectTransform property”）。

## 明确未包含

- Image Sliced / Tiled / Filled 与 Sprite border（U3，工作包 C）
- Button / Toggle / Slider / Scrollbar / ScrollRect（U8–U10，工作包 G）
- InputField / Dropdown / DefaultControls（U11–U12，工作包 H）
- Camera / World Space Canvas 画廊（主路径是 Overlay，可后续加场景）

## 验证定位

本工程是**可视化 / 手工回归**入口，不替代 `Tests/UI/run-*.mjs` 自动化验收。
自动化矩阵与阶段 flag 见 `Tests/UI/README.md`。
