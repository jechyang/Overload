# 磁盘清理执行汇总

2026-10-05。执行记录 `20261005-213829`，状态 **COMPLETE**。
用户本轮批准 H 盘 A/B/D/F/H、C 盘 I/J/K，并允许只解除副本 Toolchains
链接、保留其主线目标。已完成全部批准项，删除失败 **0**、自动重试 **0**。

## 实际释放空间

| 盘 | 删除前空闲 | 删除后空闲 | 实际释放 |
| --- | ---: | ---: | ---: |
| H | 4.52 GiB | 23.32 GiB | 18.80 GiB |
| C | 191.66 GiB | 204.84 GiB | 13.18 GiB |
| 合计 | — | — | **31.98 GiB** |

此表来自删除执行前后磁盘空闲字节差。删除文件逻辑字节合计约 **31.93 GiB**；
与实际释放的差异包括文件分配块和执行期间的其他磁盘写入。

## 已删除内容

| 盘 | 组 | 内容 | 删除文件数 | 逻辑大小 |
| --- | --- | --- | ---: | ---: |
| H | A | FrameGraphWorkspace 副本、旧 .NET 10.0.100/10.0.112 和 SDK ZIP | 18,666 | 12.40 GiB |
| H | B | JobsValidation、FrameGraph F2/F3 的编译中间文件 | 197 | 3.91 GiB |
| H | D | 其他旧测试的编译中间文件 | 452 | 0.20 GiB |
| H | F | Bin-Int/Publish 的编译中间文件 | 1,023 | 0.35 GiB |
| H | H | 独立测试 EXE/DLL 副本 | 2,511 | 1.92 GiB |
| C | I | framegraph-cache-backup 的旧 OBJ/PDB | 467 | 9.52 GiB |
| C | J | 其他旧测试的编译中间文件 | 2,638 | 0.32 GiB |
| C | K | 历史测试 EXE/DLL、脚本程序集及打包运行时副本 | 5,599 | 3.32 GiB |

合计删除 **31,553 个文件、3,025 个空目录、1 个 Junction**。
删除前逐项核对路径、类型、大小和修改时间；未使用递归删除进入链接目标。
副本链接 `H:\overload\Overload\Build\FrameGraphWorkspace\Build\Toolchains`
已解除，目标 `H:\overload\Overload\Build\Toolchains` 保留。
批准清单中的文件/目录已经再次核对，剩余 **0 项**。

## 保留与验证

当前 .NET 10.0.401、LLVM 18.1.8、主线源码、Git、用户项目、生成反射结果、
当前 Debug/Release/Publish 运行输出、FrameGraph F0 基线、原型及 F4 工作存档
继续保留。没有修改用户已有的未提交源码。

执行器对 Git HEAD、global.json、当前 dotnet.exe、clang.exe、F0 输入清单、
恢复的原型源码和主线 RenderPlan.cpp 做了删除前后 SHA256 校验，全部一致。
聊天历史、账号配置、SQLite、已安装插件、沙箱及生成图片未纳入删除范围。
旧测试运行副本已清理，重跑验证或运行历史版本前需重新构建相应程序；
下次 Publish 编译也需重建其已清理的中间文件。

删除后的可读取文件逻辑占用：

- H:\overload\Overload：约 **20.10 GiB**。
- C:\Users\Administrator\.codex：约 **2.26 GiB**（此前约 15.42 GiB）。

占用统计未跟随目录链接，24 个原有受限目录无法读取（旧 Reflection 测试
目录及沙箱秘密目录等），不计入统计；没有对这些目录尝试删除或改权限。
这属于只读占用统计的限制，与批准删除项的成功状态分开记录。

## 剩余可选项：约 6.28 GiB，未删除

| 盘 | 组 | 内容 | 大小 | 条件/影响 |
| --- | --- | --- | ---: | --- |
| H | C | Visual Studio ipch 缓存 | 3.65 GiB | 关闭使用此工程的 Visual Studio 后再处理 |
| H | E | 历史 Tracy 原始录制 | 0.78 GiB | 删除会失去原始录制 |
| H | G | 当前 Debug 调试符号/链接缓存 | 1.71 GiB | 调试前需重建，不建议优先删 |
| C | L/M | 旧插件暂存及目录缓存 | 0.15 GiB | 退出 Codex、确认无安装/同步/更新后处理 |

没有将可选项自动扩大到本次删除范围。
完整执行明细与核验结果保存在 `Build/FrameGraphValidation/DiskAudit`：

- `20261005-213829-approved-plan.json`：本轮展开后的批准路径。
- `20261005-213829-deleted.jsonl`：逐项删除日志。
- `20261005-213829-result.json`：执行状态、分组统计和磁盘空闲字节。
- `cleanup-completed-summary.json`：删除后占用、保留项校验和剩余可选范围。

之前 `20261005-213500` 的停止记录继续保留，未覆盖或隐瞒首次预检查停止。
