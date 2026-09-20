# C# 自动构建体验验收

日期：2026-09-20 至 2026-09-21，工作区 HEAD 为 `dbbe799`。本次是在已有 S5/S6 工作流上补全打开项目、保存源码和工具发现的自动化；仍通过完整 MSBuild 编译程序集，再由现有 .NET host 加载，不引入 IL 解释器。SDK 10.0.401 / runtime 10.0.12 / net10.0 不变。

## 行为

- 有 GameScripts.csproj 的项目默认在打开时排队后台构建；无修改时复用已验证的产物代。
- 尚无 C# 的项目不会主动创建托管工程；在 Assets 保存第一个 .cs 后自动创建工程并编译。
- 保存触发既有防抖构建，输出目录不会触发循环；失败后等待修改或手动重试，保留已有成功版本。
- 停止态在 Main 安全点应用。Play 中默认等待 Stop；项目设置 `csharp_hot_reload=true` 可启用现有 S6 的有限字段迁移和恢复生命周期，不迁移任意对象图、静态状态、任务或委托。切换后的用户回调副作用不承诺回滚。
- `csharp_auto_build`、`csharp_hot_reload` 与成功构建的 Debug/Release 配置保存到项目设置。
- Node 与引擎位置可发现或显式配置。Python 在后台验证版本；指定了显式路径时验证该路径，未指定时检查环境提示、引擎工具链、原生反射构建配置、Python launcher 和 PATH 候选；拒绝 Python 2。修正项目工具路径后直接重试，无需重启编辑器。
- 手动 Build 仍可选择配置或重试；首次构建完成后再进入 Play。

## 已执行证据

1. 无 GPU 工具发现测试：`Build/Tests/Scripting/build-tools-Z704cC/verification.json`，全部通过。实际 Python 3.12.14；实际 Python 2.7 被拒绝；相对/缺失显式路径被拒绝；PATH 指向 Python 2 时仍能复用原生工程选定的 Python 3。
2. 初轮真实编辑器与 RTX 3070 Ti，本机默认/RenderThread：`Build/Tests/Scripting/example-scene-iTeKOd/verification.json`，两种模式及各自重新打开均通过。未点击首次 Build，验证自动构建、场景 XML/注释兼容、Transform/Box 更新、Play/Stop、保存语法错误保留旧代、无重试循环、修复保存恢复、输出排除、等待 Stop、Play 中修改速度生效、关闭自动构建后的手动按钮、工具路径修正后原地恢复、设置持久化和无修改构建。后续 S5 回归发现中文项目设置读取问题，修复后的最终结果另列。
3. 无 C# 到首个脚本自动建工程：`C:/Users/Administrator/.codex/visualizations/2026/09/16/01a0a8d7-cf22-7003-8629-b520eb57aee9/example-scene-ejJqoY/verification.json`，通过；无 C# 时不生成工程，保存 Assets/FirstBehaviour.cs 后自动得到一个可加载类型。
4. 最终生产 Debug 解决方案 MSBuild：`C:/Users/Administrator/.codex/visualizations/2026/09/16/01a0a8d7-cf22-7003-8629-b520eb57aee9/csharp-autobuild-native-0gd9rB/verification.json`，退出码 0；实际更新 `Bin/Debug/OvEditor/OvEditor.exe` 和受影响原生依赖。
5. 修复后原有 S5 编辑器工作流：`Build/Tests/Scripting/example-scene-FaYGDM/apps-s5/workflow-verification.json`，全部通过。默认/RenderThread、各自重新打开、中文/空格路径与指定的项目内/外缓存、真实 Inspector 类型选择和字段覆盖、Lua 同时更新、损坏产物拒绝、编译失败保留旧代、非法类型清单拒绝、配置请求合并、Debug/Release 切换、无修改构建、输出排除及 Main 线程限制均通过。两个独立并发请求返回同一 Release 代，两个回执均 `unchanged=true`。这是原有 S5 工作流用例的重跑，不包含 S1–S4 全量重跑。
6. 最终固定源码组合验收：`Build/Tests/Scripting/example-scene-FaYGDM/verification.json`，`--with-s5` 退出码 0。在第 5 项之后完整重跑新增自动构建用例，默认/RenderThread、各自重新打开及首个脚本建工程全部通过；这是本次完成判断所使用的最终结果。

## 失败记录与范围

- 首次原生构建错误地使用解决方案目标 `OvEditor`，MSB4057；改为标准 `Build` 后进入正常构建。随后一次 glfw.lib 写入遇到 LNK1104；结束同时进行的隔离测试后顺序重试通过，未删除产物或终止用户进程。
- `example-scene-HvNFQm` 的 S5 两种编辑器模式通过，但并发无变化断言失败。定位到新增的 IniFile 构造把中文项目路径转成 UTF-8 传给既有窄字符文件接口，设置未加载，导致编辑器误用默认缓存；CLI 并发请求访问配置缓存时首次新建了一代。本次已改为复用 Context 的项目设置对象并 Reload，增加“中文/空格项目必须使用配置缓存”的直接断言。该失败运行不计为整体通过。
- 首次把完整测试项目与默认缓存放到较长的 C 盘证据目录时，编译成功但原生加载报 `Incomplete generation dependency`：`example-scene-dqXH6E/first-script.log`。使用较短的项目外缓存后通过；当前测试不声称支持任意超长 Win32 路径。需要时在 .ovproject 设置较短的绝对 `csharp_build_cache`。
- 标准 dotnet build 的 CET 问题没有修改或重新宣称解决；本次成功的编辑器构建均沿用完整 MSBuild 兼容流程，不修改 Windows、CET、SDK 或全局 PATH。
- 本次没有重新执行 S1–S7 全套验收、独立机器/VM 或远端 CI。上述 GPU 结果均为本地真实 GPU，工具发现测试不需要 GPU。
- 保留既有无关工作区改动，不 push。

## 可执行入口

```text
node Tests/Scripting/Integration/verify_csharp_build_tools.mjs --python <absolute-python-3.10+-path>
node Tests/Scripting/Integration/verify_example_scene.mjs --python <absolute-python-3.10+-path>
node Tests/Scripting/Integration/verify_example_scene.mjs --s5-workflow-only --python <absolute-python-3.10+-path>
node Tests/Scripting/Integration/verify_example_scene.mjs --with-s5 --python <absolute-python-3.10+-path>
```

真实编辑器入口需要已构建的 Debug 原生库和 Editor 对象；测试重新编译本次修改的生产 C++ 文件并链接实际编辑器。`--first-script-only` 可单独复验无 C# 到首个脚本的流程。`OV_SCRIPTING_EVIDENCE_ROOT` 可在进程级指定可写证据目录；原有 S5 用例仍包含项目内中文/空格缓存，宜使用较短证据根目录。
