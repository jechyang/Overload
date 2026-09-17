# C# UI 布局与 Graphic 示例

布局和几何策略都在项目脚本层：

- `Assets/Scripts/CustomHorizontalLayout.cs` 继承 `HorizontalLayoutGroup`，为第二个子项增加额外间距。
- `ResponsiveCanvas` 继承 `CanvasScaler`，按 300×200 参考分辨率缩放。
- `RedTriangle` 继承 `Graphic`，重写 `OnPopulateMesh(VertexHelper)`，在项目 C# 中提交三个顶点、UV、颜色和三角形索引。
- `GreenImage` 继承基础 `Image` 并设置绿色 tint。

修改这些项目脚本并 Build/Reload 后会热重载，无需重新编译 C++。

## 打开示例

先构建最新 Debug 引擎，再在仓库根目录执行：

    node Examples/ManagedUI/build.mjs <Python 3 可执行文件的绝对路径>

也可通过 `OV_REFLECTION_PYTHON` 环境变量提供 Python。脚本使用与编辑器相同的 BuildGameScripts 流程，生成匹配的宿主和项目程序集，并写入仅本机使用的 `ManagedUI.local.ovproject`。

用重新编译的 `OvEditor` 打开 `ManagedUI.local.ovproject`，再打开 `Assets/Scenes/ManagedUI.ovscene`。编辑态首帧即会准备 UI 脚本并显示红色三角形和绿色 Image，无需选中 Canvas 或进入 Play。尺寸随视口缩放；进入 Play 后才触发 Awake/Enable/Start 等运行生命周期。

`extraGap` 和框架可序列化字段可通过 Behaviour Inspector 编辑。编辑态 override 会保存；Play 期间的改值按引擎原规则不自动写回场景。

ScriptCore 中的 UI 框架变更需要重建托管宿主并重启；项目中的 Graphic/布局子类支持热重载。当前 Image/RawImage 是基础版本，Text、完整 Sprite 模式、输入事件和控件尚未实现。完整架构边界见 `Tests/UI/UGUI-COMPATIBILITY.zh-CN.md`。
