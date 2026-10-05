# Inspector 列表交互与布局验收

日期：2026-10-05。Debug/x64，MSVC 14.42、Windows SDK 10.0.22621.0，
NVIDIA RTX 3070 Ti，默认渲染和 RenderThread。

## 本轮结果

原有每个元素上方的 Up/Down/Duplicate/Remove 按钮改为紧凑元素行。
左侧手柄拖动排序，点击元素选择；结构体默认折叠，展开后复用已有字段控件。
标题左侧折叠、右侧 Size 输入使用独立区域，输入草稿在 Enter 后生效。
底部统一 +/−，新增时选择新元素，删除后修正选择；没有选择时删除不可用。
右键菜单提供复制元素、删除、上下移动和清空，Esc 可关闭菜单。
元素标签使用次级颜色，行高亮显示选择，拖动预览显示插入位置。
大列表保持 32 个控件一页，内部滚动限制高度，底部操作栏持续可见。

拖动只在释放时提交；取消和无变化操作不创建编辑。
载荷携带控件独立身份和元素代际，不访问控件指针；列表外部变化后旧载荷失效。
结构变化立即使旧元素回调过期，销毁和重建仍发生在子控件迭代之外。
多选按索引对各目标重放操作，保留各自元素内容，并继续支持完整属性
复制/粘贴/默认值和场景 undo/redo。只读、固定长度、容量和混合长度限制保留。

本阶段覆盖原生反射列表；C# 列表界面统一是随后阶段。

## 实际验证

```powershell
F:/npm/node.exe Build/GeneratedFields/build-native.mjs
F:/npm/node.exe Tests/Editor/run-panel-validation.mjs
```

build-native.mjs 是本地忽略的 MSBuild 驱动；正式构建仍使用现有 Premake/MSBuild。

默认渲染和 RenderThread 各 **573** 条 PASS，包含原有序列化、Inspector、
Probe、历史和生命周期回归，以及真实 ImGui 鼠标/键盘操作：

- 点击选择、底部新增/删除、空列表新增和删除后的选择修正。
- 拖动开始、预览不修改数据、释放后单次编辑、单次 undo。
- 原位置释放、Esc 取消、外部替换列表后的旧载荷拒绝。
- 手柄右键菜单和关闭菜单后的 Size 独立键盘焦点。
- Size 草稿、Enter 提交和 undo，多选的相同主目标长度仍能统一其他目标。
- 多选拖动分别重排不同内容的列表，一次 undo 恢复所有目标。

实际截图位于 Build/EditorPanels/Validation/collections.png 和
collection-records.png；已视觉核对。对应默认渲染/RenderThread PPM 像素完全一致。
夹具通过真实 Clang 生成模块与生产注册表组合，仅存在于测试可执行文件。

Debug 编辑器已发布到 Build/Debug/OvEditor.exe，与
Bin/Debug/OvEditor/OvEditor.exe 字节相同，SHA256：
D29C02DEC5AA09F7CBB2FFA87E8A0929BE6457CC585EE40E6ABECF8D3E2FBE8E。
本地提交仅包含本阶段文件，保留其他已有工作区修改，不 push。
