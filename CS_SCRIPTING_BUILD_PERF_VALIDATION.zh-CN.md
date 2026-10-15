# C# 脚本编译提速验证

日期：2026-10-15　配置：Debug　样例：`Examples/CSharpGameplay`　SDK：10.0.401（私有、锁定）

## 改动范围

| 文件 | 内容 |
| --- | --- |
| `Scripts/Windows/BuildGameScripts.mjs` | 每次编辑只调用一次 `dotnet build`；ManagedHost 按引擎输入指纹缓存到 `Build/Managed/<Config>/<key>`；元数据（路径/大小/mtime）指纹 + 自包含工程快速路径；restore 仅在其输入变化时执行（NETSDK1004 时自动补 restore）；构建中源码变化返回退出码 3；失败时清理 staging。 |
| `Scripts/Windows/CSharpBuildTools.mjs` | 新增 `dotnetToolchain()`：与 `BuildCSharp.mjs` 相同的私有 SDK / `dotnet csc.dll` 驱动方式，强制英文诊断。 |
| `Sources/OvEditor/.../CSharpProject.{h,cpp}` | 退出码 3 视为"源码已变化，重新构建"而非错误；编译诊断逐条去重写入 Console（路径相对工程目录），失败摘要只保留错误列表或日志尾部 40 行。 |

不再在每次编辑时执行：`reflection.py ensure`（仅在该配置从未生成过时引导一次）、`dotnet --version` 探测、ManagedHost 的 restore/build、内容级 SHA-256 全量指纹。

## 计时（同机、热磁盘缓存，Node 20.12）

| 场景 | 旧流程（HEAD） | 新流程 |
| --- | ---: | ---: |
| 首次构建（含 ManagedHost） | 16.5 s | 11.9 s |
| 无改动再次构建 | 9.5–9.9 s | **0.08–0.10 s**（快速路径，不启动 dotnet） |
| 仅 touch `.cs`（内容不变） | 9.6 s | 2.9 s（确定性输出 → 复用旧 generation，不触发重载） |
| 新增 `.cs` 文件 | — | 3.0 s（新 generation） |
| 编译错误（CS1002） | — | 2.1 s，退出码 1 |
| 删除文件恢复 | — | 2.8 s（新 generation） |

## 正确性检查

- `node --check` 两个脚本通过。
- 生成目录包含 `GameScripts.dll`、`GameScripts.deps.json`、`Overload.ScriptCore.dll` 与 `host/*` 全部必需文件；`manifest.xml` 与编辑器校验逻辑一致。
- 错误输出为单行 `file(line,col): error CSxxxx: message [project]`，MSBuild 汇总段重复行由编辑器去重。
- 失败/放弃的构建不残留 `staging-*` 目录。
- `OvEditor.vcxproj` Debug x64 增量构建通过（MSBuild 退出码 0）。

## 未覆盖 / 后续

- 编辑器内端到端（保存脚本 → 自动构建 → 热重载）需手动在 GUI 中确认。
- `-clp:NoSummary` 未能抑制 `dotnet build` 默认附加的汇总段，目前依赖编辑器侧去重。
- ManagedHost 指纹变化后，已运行的 RuntimeHost 不会重启（与旧行为一致），需 Stop 后生效。
