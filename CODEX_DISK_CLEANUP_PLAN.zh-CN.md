# C 盘 .codex 磁盘清理待批准清单

2026-10-05。用户已批准 I/J；H 盘删除预检查发现目录链接后整体停止，
本清单仍未删除任何文件，也未重试。最新剩余状态见
DISK_CLEANUP_REMAINING.zh-CN.md。根目录：
`C:\Users\Administrator\.codex`。

可读取内容合计约 **15.42 GiB / 49,396 个文件**，其中 visualizations 约
**13.64 GiB（88.5%）**。扫描未跟随 Junction/符号链接，也没有读取聊天内容、
账号凭据或 SQLite 数据。部分旧 Reflection 测试目录和 .sandbox-secrets
无法读取，因此上述总量不包括这些目录。文件逻辑字节不保证等于实际释放的磁盘块。

## H 盘链接到 C 盘的目录是什么

已核查 16 个 `H:\overload\Overload\Build\Tests\Scripting\s3/s4/s5/s6/s7-*`
目录，它们是 Junction，指向 9 月 15–16 日保存的 C# 脚本测试工作目录。
其中保存旧测试程序、脚本程序集、打包运行时、导出示例、编译中间文件和验证证据。
这些目录不是当前 FrameGraph 开发工作区，也不是待合并的正式引擎源码副本。

例如：

| H 盘入口 | 实际 C 盘目标（相对于 .codex） | 目标可读大小 |
| --- | --- | ---: |
| Build/Tests/Scripting/s3-GiLkCY | visualizations/2026/09/15/01a0a405-d069-7173-9faa-df8fbd112be5/s4-baseline-s3-GiLkCY | 0.13 GiB |
| Build/Tests/Scripting/s6-aaF4Qf | visualizations/2026/09/16/01a0a8d7-cf22-7003-8629-b520eb57aee9/s6-aaF4Qf | 0.29 GiB |
| Build/Tests/Scripting/s7-70i0EU | visualizations/2026/09/16/01a0a8d7-cf22-7003-8629-b520eb57aee9/s7-final/s7-70i0EU | 0.42 GiB |
| Build/Tests/Scripting/s7-hmAecC | visualizations/2026/09/16/01a0a8d7-cf22-7003-8629-b520eb57aee9/s7-work/s7-hmAecC | 0.70 GiB |

完整 16 项映射见 `Build/FrameGraphValidation/DiskAudit/codex-linked-tests.json`。
只删除 H 盘链接本身不会释放 C 盘目标占用。下方 I/J/K 清理的是明确列出的
C 盘真实文件，保留链接、源码和证据文件；清理 J/K 后历史测试目录需要重新构建
才能调试/运行。不能把链接与目标重复计数，也不能通过递归删除链接跨入目标。

## 主要占用来源

| .codex 内的路径 | 估算大小 | 说明 |
| --- | ---: | --- |
| visualizations/2026/09/20/01a0bf97-9079-7d33-a851-04e095eb40a8 | 9.52 GiB | 几乎全部是 framegraph-cache-backup 历史测试编译缓存 |
| visualizations/2026/09/16/01a0a8d7-cf22-7003-8629-b520eb57aee9 | 2.53 GiB | S5–S7、自动构建、反射与打包测试产物/证据 |
| visualizations/2026/09/16/01a0a853-319c-7883-9bdd-98466bd3d855 | 0.75 GiB | S4–S5 历史测试产物/证据 |
| visualizations/2026/09/15/01a0a405-d069-7173-9faa-df8fbd112be5 | 0.68 GiB | S3–S4 历史基线、脚本测试产物/证据 |
| sessions | 0.49 GiB | 聊天历史，保留 |
| plugins | 0.46 GiB | 包括正在使用的插件运行程序，保留 |
| .sandbox-bin | 0.32 GiB | Codex/命令执行运行程序，保留 |
| 根目录 SQLite 数据及 WAL/SHM | 约 0.25 GiB | 聊天、状态、目标、队列等数据库，保留 |
| .tmp | 0.12 GiB | 旧插件/市场临时目录，条件性候选 |
| archived_sessions | 0.05 GiB | 已归档聊天历史，保留 |
| generated_images | 0.04 GiB | 用户生成图片，保留 |
| cache | 0.03 GiB | 既有目录缓存，也有当前工具服务缓存，不能整目录删 |

## I：优先建议，历史 FrameGraph 测试编译缓存，约 9.52 GiB

真实路径：
`C:\Users\Administrator\.codex\visualizations\2026\09\20\01a0bf97-9079-7d33-a851-04e095eb40a8\framegraph-cache-backup\files`。

仅删除清单中的 **467 个文件（361 个 .obj、106 个 .pdb）**，保留相邻的
manifest.json、result.json、backup.log 和 backup.ps1。这里没有正式源码文件。
原备份 manifest 的 sourceRoot 是 `H:/overload/Overload/Build/JobsValidation`；
备份脚本及结果表明，当时将部分旧编译缓存复制到 C 盘并移除了 H 盘原文件。
这解释了为何 H 盘曾减小，但 .codex 继续占用空间。

这些文件是旧测试的对象文件/调试符号；当前主线构建与
`H:\overload\Overload\Build\FrameGraphValidation\F0` 基线不依赖它们。
删除后若需调试同一历史测试二进制，需要用相应历史源码重新生成符号。

## J：其他旧测试编译中间文件，约 0.32 GiB

仅 visualizations/2026/09 中明确列出的 **2638 个**
.obj/.pdb/.ilk/.exp/.lib；不包括 I。主要来自旧 S3–S7 与 EditorUX 测试。
保留 C++/C#/脚本源码、工程配置、日志、JSON、CSV、截图和报告；旧程序的调试
需要重建对应版本。I+J 合计约 **9.84 GiB**。

## K：可选，旧测试运行程序与打包副本，约 3.32 GiB

仅 visualizations/2026/09 中明确列出的 **5599 个 .exe/.dll**，主要是
独立测试执行文件、脚本程序集及打包验证复制的运行时 DLL。
不包括 .sandbox-bin、plugins、当前线程和主工作区 Bin/Build 发布程序。

保留旧测试源码、脚本、工程配置及结果，但这些历史测试/打包目录不能再直接
运行。需要重建对应版本，或重跑现有主线的验证脚本。该组包含旧基线测试程序，
如果仍要直接运行历史程序做对比，可先只批准 I/J。
I+J+K 合计约 **13.16 GiB**；当前 FrameGraph F0 基线继续保留。

## L/M：条件性缓存项，合计约 0.15 GiB

| 组 | .codex 内的路径 | 估算大小 | 条件 |
| --- | --- | ---: | --- |
| L | .tmp/bundled-marketplaces、.tmp/plugins、.tmp/marketplaces | 0.12 GiB | 退出 Codex 后，确认没有插件安装/同步/更新再清理；不删锁文件 |
| M | cache/remote_plugin_catalog | 0.03 GiB | 退出 Codex后清理；后续刷新目录需联网；保留其他当前工具服务缓存 |

L/M 的候选判断来自本机目录结构与修改时间，不是官方保证可删的全部缓存。
这两组收益较小，不优先处理。I–M 全选估算约 **13.31 GiB**。

## 保留与执行约束

不删除整个 .codex、整个 visualizations 或整个 cache。保留账号/配置、聊天历史、
归档聊天、SQLite 及其 WAL/SHM、队列/目标、技能、已安装插件、运行程序、
沙箱目录、生成图片和用户交付文件。
OpenAI Docs 确认 [.codex 是本地配置与状态目录](https://learn.chatgpt.com/docs/config-file/config-advanced)，
且 [plugins/cache 保存插件实际加载的安装副本](https://developers.openai.com/plugins/build/plugins)，
因此不能因名称含 cache 就当作无用文件整目录删除。

完整逐文件待批准清单：`Build/FrameGraphValidation/DiskAudit/codex-cleanup-manifest.json`；
占用统计：同目录 codex-disk-report.json。所有候选均在 C 盘 .codex 范围内，
未纳入 Junction、跨目录目标或硬链接文件。文件可能继续被其他任务更新，
执行前需重新核对路径、大小、修改时间与使用状态。I/J 已批准但尚未执行；
K/L/M 尚未单独批准。删除保持停止，等待用户下一步指示。

与 H 盘清单合并：首轮 H 盘 A+B+D+F+H **18.77 GiB**，C 盘 I+J **9.84 GiB**，
合计约 **28.61 GiB**。若再选择 C 盘 K，合计约 **31.93 GiB**；
额外关闭 Visual Studio 后清理 H 盘 C，可再释放约 **3.65 GiB**。
