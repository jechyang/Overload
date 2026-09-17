# 原生 UI 基础示例

这是 C++ Screen Space Overlay 底层绘制示例。脚本布局示例已迁到 Examples/ManagedUI。
源码基准、对应关系和兼容范围见 `Tests/UI/UGUI-COMPATIBILITY.zh-CN.md`。

用重新编译的 OvEditor 打开 `NativeUI.ovproject`，在 Asset Browser 中打开
`Scenes/NativeUI.ovscene`。Game 视口显示红色背景矩形、被矩形遮罩裁剪的半透明
绿色矩形和蓝色前景矩形。Canvas 使用固定的 2 倍像素缩放。

在 Inspector 中修改 Rect Transform 的 Anchor Min/Max、Pivot、Anchored Position、
Size Delta，或修改 UI Image 的 Color、Texture；调整 Canvas 的缩放因子和排序；
调整 Canvas Group 的 Alpha。组件支持原生场景保存与重新加载。
把项目设置中的 `render_thread` 改为 `true` 并重新打开项目，可使用渲染线程模式。

## C++ 接口

```cpp
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/UI/Frame.h>

using namespace OvCore::ECS::Components;
auto& canvasActor = scene.CreateActor("Canvas");
canvasActor.AddComponent<CCanvas>();
auto& imageActor = scene.CreateActor("Image");
imageActor.SetParent(canvasActor);
imageActor.AddComponent<CUIImage>().SetColor({0.2f, 0.6f, 1.0f, 1.0f});
imageActor.GetComponent<CRectTransform>()->SetSizeDelta({200, 80});

// width/height 是游戏视口的 framebuffer 尺寸。
const auto ui = OvCore::UI::ExtractFrame(scene, {float(width), float(height)}, sceneGeneration);
const auto hit = ui.HitTestTopLeft(pointerInViewportPixels);
// hit 返回 Actor GUID 和 interactable；上层负责解析当前场景并派发控件事件。
```

## 结构和约定

- `CRectTransform`：布局数据；Actor 的父子关系决定 UI 层级。此版不使用 3D
  CTransform 的旋转/缩放来变换 UI，所有矩形与画布轴对齐。
- `CCanvas`：根画布覆盖整个视口；根节点的 RectTransform 尺寸由画布驱动。
  原生层只保存 Scale Factor；屏幕适配由 C# CanvasScaler 决定。
  嵌套画布沿用根画布缩放和父级布局，默认继承排序，勾选 Override Sorting 后
  使用自己的 Sorting Order。相同排序值按层级遍历顺序绘制。
- `CUIImage`：纯色或纹理矩形，普通 alpha 混合。遮罩裁剪同步调整 UV。
- `CCanvasGroup`：沿父链累乘 alpha，并合并交互与射线阻挡开关。透明但可阻挡
  射线的图形仍能命中；Interactable=false 不会让点击穿透到后方图形。
- `CRectMask2D`：与父级裁剪区域求交，绘制和命中使用同一个可见矩形。
- `UI::Frame`：主线程生成的值快照，不保存 Actor 或组件指针；纹理保留对应 GPU
  资源代际。提交后按 const 使用。旧快照的 GUID 不代表对象仍存在，派发事件前
  必须结合当前场景代际和 GUID 解析结果。
- `OverlayUI`：独立的末尾渲染阶段，位于场景和后处理之后。沿用引擎的
  RenderPlan、Worker Prepare/Record、资源保留和渲染线程，不在 Worker 中访问场景。
  标准 `RenderFrontend::ExtractFrame` 在 includeUI=true 且包含 SceneUI 时自动接入。
  单视图 `ExtractScene` 不负责组合屏幕 Overlay。

## 当前边界与下一步

目前每个可见 Image 一个 draw，布局每帧重算；尚未实现脏标记、图集合批、字体、
九宫格、任意旋转、Stencil Mask、相机/世界空间 Canvas 和控件事件系统。
自动布局已迁移至 C#：LayoutElement、Horizontal/VerticalLayoutGroup、ContentSizeFitter；
尚未包含 GridLayoutGroup 和 AspectRatioFitter；C# 已有布局/图形重建队列，
当前仍会每帧安排活动布局根节点重建。
命中检测是可调用的 C++ 接口，还没有接到 InputManager 或 C# 事件回调。
现有场景格式的 sibling 顺序持久化也需要后续补全，尤其是运行时重新挂父节点后。

完整的待实现模块及源码映射记录在兼容范围文档中。当前并非完整 uGUI 兼容。

验证入口见 `Tests/UI/README.md`。
