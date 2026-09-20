# U0 运行时清单验收

日期：2026-09-21。源码基线 `dbbe799`，验证期间并行 C# 构建任务提交了
`b467a24`；其改动保留，未 reset、checkout 或混入本阶段提交。

## 已实现

- 固定参考 2018.4 / `7ae6166c9b673a966f62905953f4bc6b3153a675`。
- 84 个运行时文件、178 个类型声明（148 个公开类型）逐项映射，覆盖率 100%。
- 现有 27 个声明保持 partial，其余 151 个保持 pending，不声明运行时全部完成。
- 哈希、类型名称/可见性、文件漏项/重复、未知状态、测试编号、依赖和 blocker 校验。
- 16 个负向案例、声明扫描 fixture，以及拒绝尚未完成状态的 `--complete` 闸门。
- 独立 U0 验证、U1–U13 预留入口及包含命令/提交/编译器/运行时/模式的 evidence。

## 本机验证

以下结果在真实 CLR、OpenGL 和重建后的 Debug 原生库上通过：

```text
node Tools/UI/reference-ugui.mjs --check
node Tests/UI/run-validation.mjs Debug --matrix-only
node Tests/UI/run-validation.mjs Debug --u0 --managed --scripting-regression
node Tests/Skinning/run-validation.mjs Debug --editor-camera
```

UI 全回归记录：`Build/UIValidation/Debug-q4wYzP/result.json`。
矩阵最终记录：`Build/UIValidation/Debug-cwfSAr/result.json`。
相机记录：`Build/JobsValidation/Skinning/Debug-editor-camera-editor/result.json`。
`--complete` 当前按预期退出 1，报告 182 个未完成类型/外部依赖。

### 构建环境差异

要求的 `Scripts/Windows/Build.bat Debug -skip-project-generation` 已执行，但本次
执行通道的 Windows 环境变量不完整，且 .NET Framework FileTracker 访问用户目录
被拒绝，标准入口未成功。未修改仓库构建脚本或系统设置来掩盖此失败。

补齐进程级 SystemDrive、ProgramFiles、ProgramData、PROCESSOR_ARCHITECTURE 等变量后，
通过已有 VS2022 MSBuild，使用 `TrackFileAccess=false`、`ForceRebuild=true` 重建。
诊断还确认：缺失 PROCESSOR_ARCHITECTURE 会让 MSBuild 将请求的 x64 工具回退为
HostX86，导致 baregl 的 C1060；最终已使用日志确认的 HostX64 成功重建 baregl。

所有原生库完成重建后，OvEditor 和 OvGame 使用 `BuildProjectReferences=false`
成功编译链接；两个项目的独立构建没有 SolutionDir，后置复制未正确执行，因此显式
复制了最新 EXE 到运行目录。此为本轮环境适配，不等于标准 Build.bat 成功。

最新 `Bin/Debug/OvEditor/OvEditor.exe` 与 `Build/Debug/OvEditor.exe` 一致：

- 大小：42,277,888 字节。
- 修改时间：2026-09-20T16:14:47.964Z。
- SHA-256：`9faf544f8133fd85909df53f65b6923fb2c898f7b3e6930d2dc458f297652fd2`。

构建和失败诊断的完整日志在 `Build/UIValidation/bootstrap-*`，均不提交。
尚未进行 Release 全矩阵验收；U1 及之后仍须继续实现与逐阶段验证。
