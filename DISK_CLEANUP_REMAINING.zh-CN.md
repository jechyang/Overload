# 磁盘清理剩余未删除清单

2026-10-05，已重新核对候选项是否存在及其逻辑字节大小。
所有候选项仍在，大小与原审计一致。本次只列清单，未执行或重试删除。

上次执行记录为 Preflight / STOPPED_FIRST_ERROR：删除文件 **0**、删除目录 **0**、
释放空间 **0**。因此下列清单是全部剩余项，而不是部分清理后的余量。

## 已批准、尚未执行：约 28.61 GiB

| 盘 | 组 | 剩余内容 | 估算大小 | 状态 |
| --- | --- | --- | ---: | --- |
| H | A（一部分） | Build/FrameGraphWorkspace 基线副本 | 10.05 GiB | 预检查发现内部目录链接，停止等待下一步指示 |
| H | A（其余） | .NET 10.0.100/10.0.112 目录，以及 10.0.100/10.0.112/10.0.401 SDK ZIP，5 项 | 2.35 GiB | 尚未删除 |
| H | B | JobsValidation、FrameGraphValidation/F2/F3 中 197 个编译中间文件 | 3.91 GiB | 尚未删除 |
| H | D | 其他历史验证目录的 452 个编译中间文件 | 0.20 GiB | 尚未删除 |
| H | F | Bin-Int/Publish 中 1023 个编译中间文件 | 0.35 GiB | 尚未删除 |
| H | H | 独立验证目录中的 2511 个测试 EXE/DLL 副本 | 1.92 GiB | 尚未删除 |
| C | I | 旧 framegraph-cache-backup 中 467 个 OBJ/PDB | 9.52 GiB | 尚未删除 |
| C | J | visualizations/2026/09 中其他 2638 个编译中间文件 | 0.32 GiB | 尚未删除 |

H 盘已批准范围 A+B+D+F+H 合计约 **18.77 GiB**；
C 盘已批准范围 I+J 合计约 **9.84 GiB**。各行取整会产生少量舍入差异。

触发停止的 Junction：

`H:\overload\Overload\Build\FrameGraphWorkspace\Build\Toolchains`

实际目标：

`H:\overload\Overload\Build\Toolchains`

链接目标是主线工具链目录，包括必须保留的 .NET 10.0.401 和 LLVM 18.1.8。
副本的 10.05 GiB 统计没有跟随此链接，也没有重复计入目标大小。
本轮没有解除链接、调整删除方法、删除目标或继续其他删除。

## 未单独批准的可选项：约 9.60 GiB

| 盘 | 组 | 剩余内容 | 估算大小 | 影响/条件 |
| --- | --- | --- | ---: | --- |
| H | C | .vs/Overload/v17/ipch | 3.65 GiB | 关闭该工程的 Visual Studio 后才能清理 |
| H | E | 13 个历史 Tracy 原始录制 | 0.78 GiB | 失去原始性能录制，仅保留报告/CSV |
| H | G | 当前 Debug 的 25 个 PDB/ILK | 1.71 GiB | 调试前需重建符号，不建议优先删 |
| C | K | 5599 个历史测试 EXE/DLL 和打包运行时副本 | 3.32 GiB | 旧测试/打包目录需重建才能运行 |
| C | L/M | 旧插件暂存及远程插件目录缓存，4 项 | 0.15 GiB | 退出 Codex、确认无安装/同步/更新后处理 |

已批准范围与可选范围合计约 **38.21 GiB**，其中包含可选项的使用影响；
这不是本次已释放空间，也不是自动扩大的删除授权。

## 继续保留

正式源码、Git 历史、用户项目、主线当前工具链、生成反射结果、当前可运行的
Debug/Release/Publish 输出、FrameGraph F0 基线、核查/原型存档、聊天与配置、
SQLite 数据、已安装插件和生成图片。

完整逐项剩余路径及本轮核对结果：
`Build/FrameGraphValidation/DiskAudit/remaining-cleanup-report.json`。
原删除范围不变，删除保持停止。用户要求的规则继续有效：
任何删除失败都停止，不自行改权限、绕过检查、重试或继续其他项目。
