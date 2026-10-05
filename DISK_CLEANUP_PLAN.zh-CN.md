# Overload 磁盘清理待批准清单

2026-10-05。A+B+D+F+H 已按用户后续授权全部删除，副本 Toolchains 链接
只解除链接本身，主线目标保留。本文件保留原审计依据；最新执行统计见
DISK_CLEANUP_RESULT.zh-CN.md，剩余可选项见 DISK_CLEANUP_REMAINING.zh-CN.md。

本轮核查时 H 盘剩余约 4.54 GiB，工程可读取文件合计约 38.82 GiB（68,328 个文件）。这里统计文件逻辑字节，
不等于删除后必然释放的磁盘块；未跟随重解析点。有一个旧 Reflection 临时目录
因权限无法读取，未计入总量。Git 对象约 65 MiB，不是主要占用来源。

## 副本核查

主工作区为 H:\overload\Overload，Git 分支为 main。只有一个 Git 工作树，
没有待合并的副本分支。最近的 FrameGraph F0–F3 在主工作区完成，本地提交，未 push。

Build/FrameGraphWorkspace 是较早的基线隔离副本，不是当前开发入口。
按其创建时的 SHA256 清单核对了 4101 个输入文件：4094 个未改动，另 7 个差异
全为 Visual Studio/反射生成工程配置；没有新增或丢失的正式源码输入。
主线已包含后续 Inspector、序列化和 FrameGraph 开发，应保留主线版本。

副本 Build/FrameGraphDevelopment 下的两份早期资源分析原型已单独保留到
Build/FrameGraphValidation/DiskAudit/RecoveredPrototype（约 19 KiB）。
不把副本旧源码覆盖到更新后的主线，也不迁移它的二进制、缓存和旧工具链。

## A：优先建议删除，合计约 12.40 GiB

| 路径（均相对于主工作区） | 估算大小 | 原因 |
| --- | ---: | --- |
| Build/FrameGraphWorkspace | 10.05 GiB | 基线副本，无待合并正式源码；原型已保留 |
| Build/Toolchains/dotnet-10.0.100 | 0.75 GiB | 旧 SDK；当前全局配置和工具链锁定 10.0.401 |
| Build/Toolchains/dotnet-10.0.112 | 0.75 GiB | 旧 SDK；当前构建不引用 |
| Build/Toolchains/dotnet-sdk-10.0.100-win-x64.zip | 0.28 GiB | 旧 SDK 安装归档 |
| Build/Toolchains/dotnet-sdk-10.0.112-win-x64.zip | 0.28 GiB | 旧 SDK 安装归档 |
| Build/Toolchains/dotnet-sdk-10.0.401-win-x64.zip | 0.28 GiB | SDK 已完整安装；保留实际 SDK 目录及锁定文件 |

## B：可重建的测试中间产物，约 3.91 GiB

仅删除清单中明确列出的 197 个 .obj/.pdb/.ilk/.exp/.lib 文件，位于：

- Build/JobsValidation
- Build/FrameGraphValidation/F2
- Build/FrameGraphValidation/F3

保留测试代码、日志、result.json、图像/读回数据、哈希与全部 F0 基线。
执行前核对文件未被新测试改写且未被编译器/调试器使用；不整目录删除。
删除 PDB 会失去这些旧测试程序的调试符号，需要时重跑测试即可生成。

## C：Visual Studio 自动 PCH 缓存，约 3.65 GiB

仅 .vs/Overload/v17/ipch。需先关闭使用此工程的 Visual Studio；重新打开会重建
索引缓存。保留 .suo、布局和其他 IDE 状态，不删除整个 .vs。

A+B 约 16.31 GiB，A+B+C 约 19.95 GiB。路径没有父子重复计数。

## 本次保留

Git 历史、Sources/Tests/Dependencies/Resources/Examples、用户的示例项目及未提交
文件、当前 .NET 10.0.401 和 LLVM 18.1.8、生成反射结果、F0 基线和磁盘核查/
原型存档、当前 Bin/Bin-Int 与 Build/Debug/Release/Publish。后者仍供主线构建、
FrameGraph 回归和产品验证使用；只有下方明确列出的 F/G 项例外。

## D：其他测试的编译中间文件，约 0.20 GiB

清单中 452 个 .obj/.pdb/.ilk/.exp/.lib 文件，主要来自 EditorPanels、
UIValidation、UGUI、FlappyValidation、Scripting/Reflection 等历史验证目录。
与 B 不重复，删除后重跑相应测试即可生成；保留日志、测试源码和结果数据。

## F：Publish 编译中间文件，约 0.35 GiB

仅 Bin-Int/Publish 中的 1023 个文件。当前开发使用 Debug/Release，
Publish 中间输出可重新编译。保留 Bin/Publish 和 Build/Publish 中已有的
可运行发布输出；下次打包需重建 Publish 中间文件。

## H：独立测试程序副本，约 1.92 GiB

清单中 2511 个 .exe/.dll 文件，位于以下独立验证目录：

- Build/JobsValidation、EditorPanels、EditorUX、FlappyValidation、MistGardenValidation
- Build/UIValidation、UGUI-*、Tests、PerformanceLab

这是测试脚本生成或复制的运行程序，不包括 Bin、Build/Debug/Release/Publish、
实际工具链、F0 冻结基线和名称含 baseline 的文件。删除后旧测试目录不能直接
运行，需重跑对应验证脚本。保留测试输入、源码、日志、截图、读回数据和结果。

脚本扫描时发现部分 Scripting 测试目录是指向 C 盘历史证据目录的 Junction，
另有资源目录指向主工作区 Resources。这些链接及其目标不在删除清单中，
也未计入可释放的 H 盘空间。逐文件候选未发现跨工作区路径、父子重复计数或硬链接候选。
后续删除预检查发现 A 组副本内部 Build/Toolchains 是指向主线 Build/Toolchains
的 Junction，故立即停止；该链接目标未计入副本大小，不能递归进入目标删除。

## E/G：低优先级可选项，合计约 2.49 GiB

| 组 | 路径/文件类型 | 估算大小 | 影响 |
| --- | --- | ---: | --- |
| E | Build/JobsValidation 中 13 个 .tracy 录制文件 | 0.78 GiB | 失去历史原始性能录制；保留报告和 CSV，但无法重现完全相同的录制 |
| G | Bin/Debug、Bin-Int/Debug 中 25 个 .pdb/.ilk | 1.71 GiB | 失去当前 C++ Debug 符号和增量链接缓存；调试前需重新构建 |

目前不建议优先删 G，当前引擎开发仍需要调试符号。E 是否保留取决于是否还需
打开原始 Tracy 录制分析；它不属于普通编译缓存。

## 建议执行范围

| 范围 | 估算可释放 | 执行条件 |
| --- | ---: | --- |
| A+B+D+F+H（首轮建议） | 18.77 GiB | 用户同意；确认编译器、测试程序和调试器未使用对应文件 |
| 上述范围再加 C | 22.42 GiB | 还需关闭使用此工程的 Visual Studio |
| 所有 A–H | 24.91 GiB | 还需明确接受 E/G 的原始录制与调试符号损失 |

首轮按逻辑字节估算可将 H 盘空闲提高到约 23.31 GiB；包含 C 则约 26.96 GiB。
实际释放量以删除前后磁盘空闲变化为准。所有范围都保留正式源码、Git 历史、
用户项目及 F0 FrameGraph 基线。H:\overload_projects 下的 study_pro/test_pro
是用户项目，不列为引擎副本删除。当前 .NET 10.0.401、LLVM 18.1.8 和
Build/Generated 也继续保留。

完整路径与逐项字节数见 Build/FrameGraphValidation/DiskAudit/cleanup-manifest.json；
副本核查见同目录 copy-audit.json，占用明细见 disk-report.json。
仅执行用户明确批准的具体组。首次预检查曾因链接停止；用户授权只解除链接
并继续后，A/B/D/F/H 已完成，未删除主线链接目标。C/E/G 仍未删除。

用户追加的 C 盘 .codex 审计已另列于 CODEX_DISK_CLEANUP_PLAN.zh-CN.md：
可读总量约 15.42 GiB，其中旧测试缓存/运行副本可选清理约 13.16 GiB。
优先 I/J 约 9.84 GiB；与本清单首轮范围合计约 28.61 GiB，分盘计算且不重复计数。

本轮副本原型中有用的检查已移植到主线：内容版本校验、同一子资源重复写入
拒绝、Depth/Stencil 独立初始化验证，以及导出纹理禁止被后续临时目标覆盖。
六个受影响原生 Debug 项目构建通过；FrameGraph、后处理（3 种调度模式、
9 份历史图像哈希）、自定义渲染管线回归通过。F4 后处理临时目标迁移及后续
F5/F6 仍待完成，不能将本轮清理审计视为 FrameGraph 全部开发完成。
