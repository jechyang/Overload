# .NET 10 SDK 替换试验

日期：2026-09-14。按用户要求，在不更新 Windows 的情况下试用其他正式版
.NET 10 SDK。当前系统为 Windows 11 21H2，22000.318；仓库原锁定版本为
SDK 10.0.401 / runtime 10.0.12。

## 结果

| SDK | 随包运行时 | SDK 启动 | 原版 csc.exe -version | 标准 smoke 构建 | 标准 ManagedHost/ScriptCore 构建 |
| --- | --- | --- | --- | --- | --- |
| 10.0.112 | 10.0.12 | exit 0 | exit 2148734214；明确 CET fatal error | exit 1；CET | exit 1；CET |
| 10.0.100 | 10.0.0 | exit 0 | exit 3221225477（0xC0000005） | 等待期间手动结束进程树；未通过 | 正常返回 exit 1；MSB6006，csc.exe 退出 -1073741819（0xC0000005） |

两个候选均未解决标准构建问题。10.0.100 的崩溃不能仅凭退出码归因为 CET；
本轮没有取得该版本崩溃的调用栈，也没有据此断言所有 .NET 10 SDK 均不可用。
10.0.100 的 smoke exit 1 来自强制结束，不能当作正常完成的编译结果；
最终收据 timed_out=false，配置的 180 秒超时未触发，应记为手动中止。
其后宿主构建自行返回上述 MSB6006；结束该宿主 PID 的后续尝试返回“进程不存在”，
没有改变宿主构建结果。所有候选均未运行 smoke 产物，因为构建未成功。

仓库 `global.json`、工具链锁、项目目标框架、运行时约束及源文件均未修改。
候选仅保存在忽略目录 `Build/Toolchains/dotnet-10.0.112` 和 `dotnet-10.0.100`。
没有更新 Windows、关闭 CET、替换 SDK 内部文件或调用 csc.dll 作为备用编译器。

## 方法与证据

从[微软官方 .NET 10 发布元数据](https://builds.dotnet.microsoft.com/dotnet/release-metadata/10.0/releases.json)
选择候选：10.0.112 是 1xx 系列的当前补丁，随包运行时与仓库一致；10.0.100
是最早正式版，用于比较早期编译器表现。两个 Windows x64 ZIP 均完成官方 SHA-512 校验。

- 10.0.112：`db1e327925763c7d3e9dcb81e868067ff2da66623ec41f41e180a3faaa25a0d43eb7f079cda9d4b38cb2a8b3a3636a0b0d09353fc1cf1c0e3787eb906aaaf464`
- 10.0.100：`24b033418a3969effd49b4651ef7ebbffeb284773b99545d78dce61a82e57f38db7facdb013c609ba15573c072f0e093363ae470824a6847f3c6111078c1fb64`

为不改动仓库锁文件，在每个独立试验目录创建候选版本的 `global.json`
（rollForward=disable），复制原 S0 smoke、真实 ManagedHost/ScriptCore 项目及
依赖源码、Debug 生成文件，排除 bin/obj；记录复制源文件的 SHA-256。
项目内容保持原样，包括 net10.0 和已有 runtime 10.0.12 约束。
因此这是源文件副本上的标准构建试验，不代表仓库已切换 SDK，也不代表 S2 行为验收重跑。

各命令以候选 dotnet.exe 的绝对路径、独立 DOTNET_ROOT/CLI_HOME/NUGET_PACKAGES
运行，清除继承的编译器替换环境变量。先 `restore --locked-mode --configfile ...`，
再 `build <project> --no-restore --no-incremental -c Release/Debug -v:normal`。
两版的 smoke 与宿主 restore 均 exit 0；失败发生在随后的编译阶段。

本机完整参数、退出码、超时标记、源文件校验值和日志：

- `Build/Tests/Scripting/sdk-10.0.112-ZvNywZ/verification.json`
- `Build/Tests/Scripting/sdk-10.0.100-yGWYx2/verification.json`
- 对应目录的 `compiler-apphost.log`、`standard-smoke-build.log`、`standard-host-build.log`
- 本机试验脚本：`Build/try-sdk-candidate.mjs`；候选元数据来自 `Build/dotnet10-releases.json`。

原 SDK 10.0.401 的标准构建失败记录仍然有效，不能改写成通过。
