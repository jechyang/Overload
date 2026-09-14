# C# MSBuild 兼容构建入口与验收

日期：2026-09-14。起始 HEAD：`3c9f9fa`（S2），已实际核对。
本轮只调整构建工具链和验收编排，不实现 S3–S7；不 push。

## 使用与撤销

在仓库根目录执行（Node.js 20+、Windows x64）：

~~~text
node Scripts/Windows/BuildCSharp.mjs --compat Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj --rebuild
~~~

默认 Debug，原生 Debug 反射生成文件必须已存在。依赖仍由项目中的
ProjectReference 和 NuGet.Config/packages.lock.json 管理。入口先执行 locked restore，
再执行完整 MSBuild build；`--rebuild` 对应 `-t:Rebuild`，不传则允许增量构建。
可选 `--configuration Debug|Release`、`--artifacts-path <目录>`、
`--output <目录>`；验收使用 `--define S1_ACCEPTANCE` 构建独立测试程序集。
生产构建不传该符号。Release 需要相应原生生成文件，本轮主要验收 Debug。

默认产物位于 `Build/CSharpCompat/artifacts`，按 SDK 的项目/配置目录布局组织；
每次命令及退出码位于 `Build/CSharpCompat/logs/build-*/verification.json`。
日志保留完整 restore/build 输出，包括 SDK Csc 的引用、analyzer、源文件和生成文件参数。
入口失败返回非零，不自动改用另一种编译方式。

撤销只需停止调用该入口，使用原来的私有 dotnet 命令；没有 Directory.Build.*
全仓库自动导入，也没有修改 csproj、global.json、工具链锁或全局环境。
兼容产物与原版 bin/obj 隔离，不能用增量命中兼容 DLL 声称原版构建成功；
比较原版时必须使用 `-t:Rebuild`。工具不会自动删除上次产物，失败后不得把目录中
剩余的旧文件当作新构建结果，也没有实现 S5 的代际发布/编辑器工作流。

## 实现依据

实际检查锁定 SDK 10.0.401 的 `Roslyn/Microsoft.CSharp.Core.targets`：
第 167/168 行把 CscToolExe/CscToolPath 绑定到 Csc 任务，171 行绑定
UseSharedCompilation。该文件的 Csc 调用没有绑定 RoslynCompilerType；不能凭属性名
推断它能切换本机这条路径。核对的 SDK 文件 SHA-256 见验收收据。

微软 [ManagedToolTask 实现](https://github.com/dotnet/roslyn/blob/main/src/Compilers/Core/MSBuildTask/ManagedToolTask.cs)
显示 ToolPath/ToolExe 的自定义工具分支与内置 apphost 选择是不同路径。
这里以本机 targets 加实际构建结果为准，不把 main 分支当作锁定 SDK 的精确源码版本。

`BuildCSharp.mjs` 使用锁定私有 dotnet 的绝对路径，检查其 --version；以命令级属性
设置 CscToolPath、CscToolExe=csc-dotnet.cmd、UseSharedCompilation=false、
UseHostCompilerIfAvailable=false、SkipCompilerExecution=false。
这些属性传递到 ProjectReference。启动器 `Tools/OvCSharp/csc-dotnet.cmd`
只执行进程环境中明确指定的 `dotnet.exe csc.dll %*` 并原样返回退出码，
禁用 CMD 延迟展开。MSBuild 仍调用 SDK 的原始 Csc 任务，生成响应文件、解析依赖、
运行 analyzers、生成 assembly attributes/editorconfig、管理 DLL/PDB/ref/deps/runtimeconfig。
没有手写引用清单或覆盖 CoreCompile，也没有 SkipCompilerExecution/伪造 DLL。

所有设置仅属于该次子进程；没有更新 Windows、关闭 CET、修改 SDK 二进制、
降级 net10.0/runtime 或修改全局 PATH。初期 Node 工具的精简环境缺少 Windows
目录变量导致 NuGet path1 异常；只在试验进程补齐所需变量，随后 restore exit 0。

## 验收入口

~~~text
node Tests/Scripting/Integration/verify_build_compat.mjs
node Tests/Scripting/Integration/verify_s2.mjs --compat-build --python C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe
~~~

S2 入口包含 S1 生产宿主、默认/RenderThread 真实组件和失败矩阵，并重新编译原生夹具。
`--compat-build` 使生产/验收 ManagedHost、ScriptCore 和全部 S2 测试项目通过完整
MSBuild 兼容构建生成。未传此开关时仍保留原验收器的直接 csc.dll 路径，二者分别记录。
测试程序中的组件值、类型查询、Lua、XML 和失败断言未修改。

接入前的隔离试验：`Build/Tests/Scripting/msbuild-compat-probe-7qHShZ/commands.json`。
先复制 S0、ManagedHost/ScriptCore 及生成文件，排除 bin/obj；原版 smoke 强制构建
exit 1（CET），兼容 smoke 与完整 host 重建 exit 0，smoke 实际运行 exit 0，
故意引入 #error 返回 exit 1/CS1029。成功后才加入仓库入口。

独立构建验收：`Build/Tests/Scripting/msbuild-compat-cnSy35/verification.json`，
入口 exit 0，success=true，compatibility_msbuild_passed=true，
standard_dotnet_build_passed=false。所有命令完整 argv 和真实退出码均在收据内；
表中的项目是该目录下 `project 空格` 的源码副本，排除了 bin/obj。

| 检查 | 实际退出码/断言 |
|---|---|
| 原版 smoke、host：dotnet build <project> --no-restore -t:Rebuild -v:normal -nr:false | 各 1，CET |
| 兼容 smoke/host 完整重建 | 各 0 |
| 新 smoke DLL 通过锁定 dotnet 执行 | 0；实际断言 .NET 10.0.12/x64 与值处理 |
| 兼容 host 无修改构建 | 0；两个 DLL 的 SHA-256/mtime 均不变，无 Csc 重编 |
| 兼容 host 强制重新构建 | 0；两个 DLL mtime 改变且确定性哈希相等 |
| 在引用项目 ScriptCore 新增 #error | 1；CS1029 含预期标识与源位置 |
| 移除编译错误，再重建 | 0 |
| 移走原生 generation-state.json，再重建 | 1；原项目 CheckNativeGeneration 错误仍有效 |
| 恢复生成文件，再重建 | 0 |
| 兼容成功后再次原版强制构建 | 1，CET；证明确实可撤销 |
| SDK dotnet/csc.exe/csc.dll/CSharp.Core.targets | 前后 SHA-256 完全一致 |
| MSBuild 附加产物 | 实际存在 deps.json、runtimeconfig、ScriptCore PDB 和项目隔离中间产物 |

上述 smoke/host 的 locked restore 均 exit 0。兼容命令统一为：

~~~text
node Scripts/Windows/BuildCSharp.mjs --compat <project.csproj> --artifacts-path <独立目录> --output <产物目录> --rebuild
~~~

省略 --rebuild 的命令用于无修改构建。测试操作错误源文件/缺生成文件时只修改隔离副本，
并验证原始源文件校验值未变。本轮开始前已有 587 个工作区文件的 SHA-256 也已核对未变。

## 新产物的原生宿主与 S1/S2 回归

最终收据：`Build/Tests/Scripting/s2-OF1P2f/verification.json`。
入口 exit 0，stage=S2，success=true，compatibility_msbuild_passed=true，
standard_dotnet_build_passed=false；38 条命令及 71 个固定源码 SHA-256，结束后复核一致。
本轮开始时 HEAD 仍为 3c9f9fa；共享 S2 验收器的 baseline 字段保留原 S1 祖先 a1c3a1a。

| 检查 | 实际结果 |
|---|---|
| Debug 完整原生 solution（Editor/Game）及新原生夹具编译/链接 | exit 0 |
| 原版仓库 ManagedHost/ScriptCore 强制构建 | exit 1，CET |
| 生产 ManagedHost/ScriptCore 完整兼容重建 | exit 0 |
| S1_ACCEPTANCE 独立完整兼容重建、再次强制重建 | 各 exit 0；DLL 哈希一致 |
| 7 个类型变体及 Dependency/Dependent 测试项目 | 全部完整 MSBuild 兼容重建 exit 0，含 ProjectReference 依赖链 |
| 生产 DLL 原生启动 | exit 0；启动前无 CLR，nethost/hostfxr 主动加载，R6 绑定且无测试 Smoke 导出 |
| S1 默认/RenderThread 真实 Box/Transform | 各 exit 0；native 直接检查 Box=(3,4,5)、位置=(6,7,8) |
| S1 ABI/线程/异常/会话/失效句柄矩阵 | 保留原有真实断言；缺配置/DLL/位数等负例按预期 exit 1 |
| S2 默认/RenderThread | 各 exit 0，各 115 项运行断言 |
| S2 Null 后端 | exit 0，5 项运行断言 |
| 原生无修改再构建 | exit 0；Debug obj 哈希和 mtime 不变 |

S2 覆盖身份冲突、类型发现而不构造、改名/移动、依赖缺失与恢复、程序集缺失与恢复、
Lua 字段/回调/Prefab、未知字段和 XML 往返等已有行为。没有改动 native 测试断言，
没有用打印字符串作为编译成功判据，也没有用旧托管 DLL 替代本轮产物。

新产物 SHA-256：

~~~text
production ManagedHost: 7fd25fc5c2fc853f397e4fe400369d5ef7e0f30125f99af62dbbc469f7a93aac
production ScriptCore: 989bdc9fc1ac959340a21438d456b2239fddcaaed1fcfa6a9f456162a3c912a7
acceptance ManagedHost: 9be66ca0b12598fc38f9910ad5e64a2b3187e61998be691899a3340fe1393d6f
native-host.exe: 13aed28be62b1f4ef881d72686b3bd27457991bc52c51680d02b638c6e4667b1
s2.exe: 805a2d1fab97a8b2c74622204e1330d272a2af37cf7f7b97bd02a5c11a0c89eb
s2-null.exe: d7a15996edcd30772aa5d1bcca290da641db409fcabbf414f178fcf05f283473
~~~

## 失败记录和限制

- 原版构建的 CET 系统前提未修复，SDK/runtime/net10.0 锁保持 10.0.401/10.0.12/net10.0。
  兼容成功不等于 Windows 或原版 SDK apphost 修复。
- `s2-Qc8qJA/failure.json` 记录沙箱原生 FileTracker E_ACCESSDENIED；
  在自动审批通过后的本机权限下完整重跑为 s2-OF1P2f，没有关闭文件跟踪来绕过验收。
- `msbuild-compat-9MAoJE/verification.json` 最后校验源码时因测试脚本未规范化
  Node cp 的 Windows 长路径前缀而 ENOENT；修正后整轮重跑为 cnSy35，不拼接成功片段。
- 仅验证本机 Windows 11 21H2 22000.318、Windows x64 私有 SDK 10.0.401。
  CMD 启动器和属性绑定依赖该 SDK 已实测的 ToolTask 行为；未来 SDK 升级需重新验收。
  中文/空格的项目、输出路径已实测；CMD 特殊元字符路径、超长路径、并发写同一产物目录未验收。
  并发任务应使用不同 --artifacts-path/--output；此入口不实现构建队列或代际发布。
- 入口要求项目相邻 NuGet.Config 和可用依赖锁，不安装依赖 SDK、不下载原生反射生成文件。
  远端包依赖、发布/AOT、Visual Studio 内部托管编译、其他 OS/架构未验收。
- 没有新增 S3–S7 实现，没有完整 Editor/Game 交互 Play/Stop、Release/Publish 全矩阵或远端 CI。
  实際 GPU/组件/宿主运行使用现有原生验收夹具，不能扩展为上述未测场景通过。

## 工作区保留

起始的 RENDERING_ARCHITECTURE.md、.zcode/、CODEX_CHANGE_LIST_TEST.txt、Docs/、
Examples/PerformanceLab/、RENDERING_ARCHITECTURE_DIAGRAM.zh-CN.md、
Scripts/Windows/ResolveCSharpBuild.cmd/.md/.mjs、
CS_SCRIPTING_SDK_COMPATIBILITY_VALIDATION.md 全部保留，不纳入本轮提交。
旧 S0/S1/S2 验收文档记录的原版构建失败仍有效，不追改成通过。
