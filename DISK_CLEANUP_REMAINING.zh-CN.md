# 磁盘清理剩余未删除清单

2026-10-05，已完成 H 盘 A/B/D/F/H 和 C 盘 I/J/K 的全部批准项。
实际释放约 **31.98 GiB**；批准范围剩余 **0 项**，删除失败 **0**、自动重试 **0**。
执行汇总见 DISK_CLEANUP_RESULT.zh-CN.md。

## 当前剩余可选项：约 6.28 GiB，本轮未删除

| 盘 | 组 | 剩余内容 | 估算大小 | 影响/条件 |
| --- | --- | --- | ---: | --- |
| H | C | .vs/Overload/v17/ipch | 3.65 GiB | 关闭使用此工程的 Visual Studio 后才能清理 |
| H | E | 13 个历史 Tracy 原始录制 | 0.78 GiB | 失去原始性能录制，仅保留报告/CSV |
| H | G | 当前 Debug 的 25 个 PDB/ILK | 1.71 GiB | 调试前需重建符号，不建议优先删 |
| C | L/M | 旧插件暂存及远程插件目录缓存，4 项 | 0.15 GiB | 退出 Codex、确认无安装/同步/更新后处理 |

以上项均再次核对存在，不自动扩大删除范围。

## 继续保留

正式源码、Git 历史、用户项目、主线当前工具链、生成反射结果、当前可运行的
Debug/Release/Publish 输出、FrameGraph F0 基线、核查/原型/F4 工作存档、聊天与配置、
SQLite 数据、已安装插件和生成图片继续保留。

副本 Toolchains Junction 已只解除链接，其主线目标 Build/Toolchains 保留。
首次预检查停止的记录仍在；用户授权继续后的本轮执行完成，没有失败后重试。
逐项执行和剩余范围核验见 Build/FrameGraphValidation/DiskAudit 下的
20261005-213829-result.json 与 cleanup-completed-summary.json。
用户要求的规则继续有效：任何删除失败都停止，不自行改权限、绕过检查、
重试或继续其他项目。
