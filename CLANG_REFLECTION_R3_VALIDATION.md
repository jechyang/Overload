# Clang Reflection R3 实施与验收记录

日期：2026-09-12。阶段：R3 本地实现、三配置主验收及完整 Clean/Build 验收全部完成；R4 尚未开始。
执行前已阅读 R2 验收记录、实现计划 v1、工具 README 与协议。
本轮没有 Git 提交、推送或远端 CI 执行。

## 实现

- 新增 OvReflectionGen 独立生成项目，以 VS Makefile 形式执行 Python ensure。
  OvMaths、OvCore、OvEditor、OvGame 同时具有解决方案依赖和 ProjectReference，
  单独构建模块也会等待生成；生成项目不输出或链接 LLVM 到引擎。
- 正式模块显式列出三配置的固定 Reflection.generated.cpp 路径，按配置排除
  另两份源文件。生成代码编入 OvMaths/OvCore，并链接 OvReflection。
- Premake 将已烘焙的配置写在 Tools/OvReflectGen/Projects/Premake，
  与项目文件一起忽略，独立于 Build/Generated。首次构建自行引导 CMake/Ninja
  构建工具；业务头与扫描 TU 不依赖生成头，消除首次生成依赖环。
- 每次构建调用内容检查，使用实际直接/间接/系统头 SHA256、include 目录
  文件名清单、选定配置与宏、扫描清单、生成器二进制/源码及版本、Schema
  版本判断失效。目录清单覆盖新增的遮蔽头及此前不存在的 __has_include 文件。
- 未失效模块复用 Schema；变更模块重新分析，发布前统一校验模块归属与依赖。
  对输入摘要和输出摘要都做校验，缺失/损坏的生成文件不能被当作有效缓存。
- 每个 target/configuration 使用一个进程所有的 OS 文件锁；工具构建另有
  全局锁。不同配置可并行分析，同一配置多个构建调用只有一个写者。
- 原生分析与 emitter 在临时事务目录写入，经校验后逐文件原子替换。
  模块 manifest 最后发布，generation-state.json 确认整配置成功；
  消费者必须等待生成目标成功。内容相同不更新时间戳。
- 失败移除完成标记和可安全识别的所属成功产物，返回非零阻断当前构建。
  只清理 manifest 所属模块目录内的平面文件，拒绝越界路径、目录和链接跳转；
  未登记的相邻文件保留。临时目录递归清理仅限本次分配的事务目录。
- 显式 analyze/generate/clean 也使用配置锁；export 的项目写入有独立锁。
  手动 native CLI 保持为独立诊断入口，不能对受管理的产物目录并发使用。
- Lua 构建配置发生改变时检查并拒绝过期导出，要求重新 gen_proj；不会在
  已加载的 MSBuild 图下改写项目。普通头文件、标注和已有模块的扫描清单
  修改无需重新 gen_proj。新增模块/项目仍需更新 Premake 图。
- 生成器版本统一为 0.4.0，Schema 版本保持 1。R4 的组件工厂、生产注册、
  Inspector、XML 和 Lua 迁移没有开始。

## 验收入口

~~~powershell
python Tests/Reflection/Integration/verify_r3.py --fixtures-only
python Tests/Reflection/Integration/verify_r3.py
python Tests/Reflection/Integration/verify_r3.py --full-solution
~~~

最终采用 --full-solution，覆盖 Debug / Release / Publish 的完整解决方案。
验收会把旧生成树及工具可执行文件移动到本次证据目录，然后从无生成产物
状态构建。临时修改正式 CPhysicalBox 的显示名及 setter 标注，随后逐字节恢复，
据此验证真实编译依赖范围及生成失败阻断。验收期间应避免其他构建和源码编辑。

本机 Python：C:/Users/Administrator/AppData/Local/Programs/Python/Python312/python.exe。
LLVM 18.1.8、MSVC 14.42.34433、Windows SDK 10.0.22621.0。

## 结果与证据

最终 R3 主矩阵：Build/Tests/Reflection/r3-h6aeczox/verification.json。
补充完整清理验收：Build/Tests/Reflection/r3-h6aeczox/FullClean/verification.json。

显式 solution Clean 后，Debug 对象文件数量为 0，生成 C++ 也全部清除。
完整 Build 用时 309.03 s，22 个实际库/应用产物，0 警告、0 错误。
重建的 803 个对象与当前解决方案的全部有效 ClCompile 项严格一致。
随后无修改 Build 用时约 2.06 s，所有对象及生成文件的内容、时间戳均保持不变。

补充脚本最初错误要求旧缓存中的所有对象都恢复，其中含已删除的历史渲染
源码对象；Clean/Build 本身已成功。改为读取当前 .sln/.vcxproj 并严格比较
有效编译项后通过，原始失败日志和后续验证均保留，未为通过检查更改引擎源码。

| 配置 | 无生成产物后的完整解决方案 Build | 无修改 Build | 无修改生成检查 | 重编译对象 |
|---|---:|---:|---:|---:|
| Debug | 6.719 s | 1.875 s | 0.688 s | 0 |
| Release | 380.562 s | 1.953 s | 0.782 s | 0 |
| Publish | 370.532 s | 2.157 s | 0.938 s | 0 |

三配置均为 22 个实际库/应用产物、0 警告、0 错误。表中第一列 Build 清空
反射产物与工具可执行文件，但保留既有 C++ 对象缓存；不能解释成三个配置
在相同冷缓存条件下的性能比较。完整对象清理的独立结果另外记录。

正式 Box 显示名修改只重新分析 OvCore，生成检查 1.657 s；
MSBuild 重编译 5 个对象：Reflection.generated、Actor、CPhysicalBox、
LuaActorBindings、LuaComponentsBindings，均位于 OvCore。OvMaths 和其他
模块对象保持不变。将 setter 改为不存在的方法后，当前构建非零退出，
没有消费者对象被改写；原标注恢复后构建通过。

直接/间接头文件（含同大小、同时间戳改动）、宏、文件增删改名、可选 include、
扫描清单、生成器源码/版本、Schema 版本、产物缺失/损坏、登记文件清理、
越界清理拒绝、模块移除、过期 Premake 配置、失败恢复、同配置三个生成进程、
三配置并行、被终止写者的锁恢复，以及真实 MSBuild 项目/配置并行均通过。

正式 OvMaths/OvCore 归档已确认包含生成对象和注册符号。Debug/Publish
编辑器动态依赖未包含 LLVM/Clang；对应证据在主矩阵目录内。最终实现源码
摘要保存在 r3-source-receipt.json，验收期间保持不变。

已完成的独立回归：

- R0：Build/Tests/Reflection/r0-f7f2a1rs/verification.json。
- R1：Build/Tests/Reflection/r1-t50g0252/verification.json。
- R2：Build/Tests/Reflection/r2-wr5xf766/verification.json，三配置 MSVC/Clang
  运行、只按 ID 查询消费者及正式生成对象编译通过；本轮未重新运行 GPU 行为。
  R2 的 Bullet/RenderThread 既有完整证据见 R2 验收记录。

早期 Debug 完整构建为 22 个项目、0 警告、0 错误；构建约 306 秒。
期间仍有生成驱动修正，第二次构建正确失效并更新摘要，严格的输出不变检查
因此未通过。这是开发中的预验收，最终验收已固定源码并单独记录 SHA256。

## 环境问题与处理

通用命令通道仍存在 CET 启动问题，使用本地 Node 子进程通道执行。
最初 MSBuild FileTracker 因受限环境的短临时路径及缺失系统目录环境变量
初始化失败。验收进程使用工作区临时目录，并补齐标准 ProgramData、
ALLUSERSPROFILE、SystemDrive、USERPROFILE、PROCESSOR_ARCHITECTURE，
禁用 MSBuild 节点复用，避免复用此前初始化失败的节点。

本轮保留正常文件跟踪与最小重编译，不使用 TrackFileAccess=false 或
ForceRebuild=true。机器级 vcpkg 接入仍在验收命令中禁用，显式选择 x64
主机工具。没有修改系统设置、全局 Git 配置或正式项目的编译跟踪行为。

## 边界与 R4 交接

- 原子性是单文件替换 + manifest/配置完成标记 + 构建依赖门控；没有宣称
  对构建过程中绕过门控的任意外部读取者提供整个目录的原子快照。
- 显式传入 --generator 的诊断调用由调用方选定工具；正常 ensure 自行
  检查并构建本仓库的工具源码。
- MSBuild 自定义属性表与额外命令行编译参数不在 Premake 导出契约内；
  正式语义选项应在 Premake 配置中维护并重新生成项目。
- CI 已增加完整 R3 入口，本轮没有远端运行结果。
- 下一阶段 R4：原生组件类型标注、显式工厂/取得策略、Actor 创建链和
  Inspector 添加列表迁移、旧场景/Prefab 与生命周期兼容验收。

完成 R3 后停下，重开对话继续 R4；开始前阅读本记录、R2 记录和工具协议。

下一次可发送：

> 继续实现计划 v1 的 R4，先阅读 CLANG_REFLECTION_R3_VALIDATION.md、
> CLANG_REFLECTION_R2_VALIDATION.md 与 Tools/OvReflectGen/Schema/PROTOCOL.md。
> R3 三配置完整解决方案、增量/并行/失败阻断及显式 Clean/Build 已通过；
> CI 尚未远端验证。完成 R4 后停下并提醒我重开对话。
