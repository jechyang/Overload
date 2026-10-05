# C# 与原生 Inspector 列表统一验收

日期：2026-10-05，Debug/x64，MSVC 14.42、Windows SDK 10.0.22621.0，RTX 3070 Ti。

## 结果

OvUI::ReorderableList 只持有显示状态；原生反射和托管字段分别保留自己的
类型校验、生命周期保护和写回协议。两者共用标题、Size、拖动手柄、选择、
分页、内部滚动、底部 +/− 和元素右键菜单，不再维护两套列表绘制代码。

C# 数组和 List<T> 均允许调整长度，按托管端提供的类型默认值新增元素；
原生固定数组继续固定长度。C# 支持空/null 列表、嵌套列表、记录折叠和记录内列表。
结构修改后终止本次元素迭代并使旧拖动载荷失效。未改变 C# 多选限制。
现有 1024 项、8 层、64 KiB 编码限制保留，增长前先检查编码容量。

连续文本输入写回后重新读取托管端接受的规范文本，避免 XML/hex 规范化被误认为
外部替换而取消输入焦点。外部真实替换仍取消旧元素的活动输入和拖动载荷。
复合字段使用完整宽度；搜索按字段标题筛选，默认值重置位于标题右键菜单。

## 实测

```powershell
F:/npm/node.exe Build/GeneratedFields/build-native.mjs
F:/npm/node.exe Tests/Editor/run-panel-validation.mjs
F:/npm/node.exe Tests/Scripting/Integration/verify_generated_fields.mjs --native
```

原生面板默认和 RenderThread 各 573 条 PASS。托管源生成器正常、运行时、重复与
非法注解检查均通过，真实托管宿主 native-default 229 条 PASS，native-render-thread
230 条 PASS（额外包含渲染线程初始化检查）。日志中的 null 标量、失效程序集等
错误为原有负面测试的预期输出。

新增真实 ImGui 输入覆盖增删、拖动预览与交付、连续文本输入焦点、只读状态、
右键菜单/Esc、数组 Size 草稿与提交、64 位枚举、null 列表、嵌套列表与记录。
原始值通过生产 C# ABI 写回并读回核对，未用 UI 回调模拟托管宿主。

已查看 Build/Tests/GeneratedFields/managed-lists.png；默认和 RenderThread 的
managed-lists*.ppm 字节完全一致。测试 GL 操作经产品使用的 ResourceExecutor
分派到 GL owner，测试结果不依赖在主线程调用无上下文的 GL API。

Debug 编辑器发布到 Build/Debug/OvEditor.exe，保留其他已有工作区修改。
本阶段仅本地提交，不 push。
