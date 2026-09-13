# Clang Reflection R0 实施与验收记录

日期：2026-09-12。阶段：R0 本地实现与验收完成；R1 尚未开始。
CI 工作流已提供，本次未提交远端、未触发 GitHub Actions，不能视为 CI 已通过。

## 完成内容

- 新增独立 Tools/OvReflectGen，使用 Clang LibTooling / ASTConsumer /
  RecursiveASTVisitor，Frontend、Schema、Emitters 分离。
- 使用 LLVM 18.1.8 完整开发包，CMake 精确校验 LLVM 版本，源码校验
  Clang 版本。引擎不链接 LLVM，不改变组件、Inspector、序列化或 Lua。
- Premake 的可选导出钩子读取 VS2022 已解析的 OvMaths / OvCore
  配置；保留列表顺序，导出 Debug / Release / Publish 的宏、include、
  语言、运行库及其他选项。默认 gen_proj 行为不启用反射生成。
- scan-modules.json 明确限定 FVector3、CPhysicalBox、CTransform。
  每模块、配置生成一个扫描翻译单元，不依赖任何反射生成头。
- 输出实际 argv、工具链路径与版本、模块清单、参数来源和 AST 分析报告。
  失败返回非零并移除本次报告的旧成功结果。
- 冻结 Schema 版本、默认/显式身份、历史别名、类外查询与目录协议。
- 提供开发包下载脚本、使用说明、独立 CI 和集成验收脚本。

## 实测环境

| 项目 | 本次使用 |
|---|---|
| LLVM / Clang | 18.1.8 |
| 开发包目录 | H:/overload/Overload/Build/Toolchains/llvm-18.1.8 |
| MSVC 工具目录版本 | 14.42.34433 |
| cl 编译器版本 | 19.42.34433 |
| Windows SDK | 10.0.22621.0 |
| Target | x86_64-pc-windows-msvc |
| Premake | 仓库自带 5.0.0-beta7 |
| CMake | 3.26，Ninja 使用 VS 自带版本 |
| 分析器构建 | MSVC / C++20 / Release / 动态 Release CRT |

官方 LLVM 包的 LLVMDebugInfoPDB 导入项写死了构建机器上的 VS2019
diaguids.lib 路径。本项目只重定位该 CMake 导入项到实际 VS2022 DIA SDK，
没有修改下载包，也没有替代系统类型。

本机默认 python 命令是 Python 2.7；本次使用 Codex 提供的 Python 3
运行脚本。复现时必须使用 Python 3.10+，不能直接假定 PATH 中的 python
版本正确。

## 验收结果

| 类型 | AST 类别 | 基类 | Debug | Release | Publish |
|---|---|---|---|---|---|
| OvMaths::FVector3 | struct | 无 | 通过 | 通过 | 通过 |
| OvCore::ECS::Components::CPhysicalBox | class | CPhysicalObject | 通过 | 通过 | 通过 |
| OvCore::ECS::Components::CTransform | class | AComponent | 通过 | 通过 | 通过 |

- OvReflectGen.exe 已实际编译、链接并运行。不是 AST dump 或编译器语法
  检查替代 LibTooling。
- 全新 Build/Tests/Reflection/r0-2rdj38y5 输出目录通过正式验收。
- 与生成的 .vcxproj 对照：宏、include 顺序、C++20 一致；验证
  Unicode、Debug CRT 和 Publish 不包含 Tracy。
- 相同输入再次导出，编译数据库字节一致；重复 AST 分析报告字节一致。
- 故意非法语法：非零返回并定位 SyntaxError.h 的源码行。
- 故意缺失头文件：非零返回，明确报告缺失依赖。
- 故意请求不存在的完整类型：非零返回，不遗留成功报告。
- 失败后恢复输入可再次通过，未修改业务头文件或生成假依赖。
- 普通默认输出目录也已生成正式 analysis-report.json。
- git diff --check 通过。

正式验收入口：

~~~powershell
python Tools/OvReflectGen/reflection.py build
python Tests/Reflection/Integration/verify_r0.py
~~~

日常分析入口：

~~~powershell
python Tools/OvReflectGen/reflection.py export
python Tools/OvReflectGen/reflection.py analyze
~~~

本次证据：
Build/Tests/Reflection/r0-2rdj38y5/verification.json；
Build/Generated/Reflection/x86_64-pc-windows-msvc/<配置>/<模块>/analysis-report.json。

## 边界与后续

R0 报告只是显式扫描的真实类型分析证据，不是公开反射 Schema。尚未添加
OV_TYPE 等宏，也未导出字段或生成运行时访问器。增量失效、单写者、
原子发布和完整引擎生成依赖图按计划留到 R1 / R3。

CI 文件为 .github/workflows/reflection.yml，使用 windows-2022、
LLVM 18.1.8、MSVC 14.42 和 SDK 10.0.22621.0。其实际远端结果仍需
首次运行确认；远端安装器或镜像变化应显式失败，不能跳过头文件兼容错误。

按用户要求，每完成一个阶段后重开对话。本次停在 R0。
新对话可直接发送：

> 继续实现 CLANG_REFLECTION_IMPLEMENTATION_PLAN.md 的 R1。
> 先阅读 CLANG_REFLECTION_R0_VALIDATION.md、Tools/OvReflectGen/README.md
> 和 Tools/OvReflectGen/Schema/PROTOCOL.md。R0 本地 LibTooling 验收已通过，
> LLVM 开发包位于 Build/Toolchains/llvm-18.1.8；CI 尚未远端验证。
> 完成 R1 后停下并提醒我重开对话。

仓库原有的其他未提交改动保持不动，本次没有创建提交。
