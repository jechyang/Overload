# Clang Reflection R1 实施与验收记录

日期：2026-09-12。阶段：R1 本地实现与验收完成；R2 尚未开始。
执行前已阅读计划 v1、R0 验收记录、工具 README 和协议。
CI 已增加 R1 验收入口，本次没有提交远端或触发 GitHub Actions。

## 完成内容

- 新增固定的 `OvReflection/Annotations.h`，提供 OV_TYPE、OV_FIELD、
  OV_PROPERTY、OV_METHOD，以及 `Access<T>` 前置声明。普通编译模式标注
  为空；分析模式使用 Clang annotate。没有 GENERATED_BODY 或生成头依赖。
- Frontend 按真实 AST 节点提取 class、struct、enum、enum class、基类、
  字段、getter/setter 属性及方法签名；未标注的依赖不自动进入 Schema。
- Schema 独立保存原始类型拼写、canonical 类型、值类型类别、整数位宽与
  符号信息；枚举值用十进制字符串保存，避免 64 位整数经过 double。
- 实现默认/显式 ID、历史别名、名称推导、属性配对、私有访问授权、
  类别策略、继承冲突和不支持声明的诊断。错误包含源码行列。
- Component 策略要求真实 AComponent 继承和明确的 Factory/Lookup，
  组件属性限定 Main 线程。R1 只描述策略，不生成或执行工厂。
- 输出稳定排序的 `schema.json`。保留 R0 `analysis-report.json`。
- 通过 Clang 预处理回调收集实际进入的扫描文件、直接/间接头和系统头，
  对解析时读取的字节计算 SHA256，输出 `dependencies.json`。
- Python 驱动生成 `input-summary.json`，纳入编译命令、配置、扫描清单、
  工具链锁、生成器二进制/源码及 Schema 版本。此摘要是 R3 的输入证据，
  尚未用于跳过生成。
- 原生工具与驱动在失败前清理旧成功产物；修复了成员函数模板标注及仅在
  类外定义上新加标注可能被漏掉的问题，统一明确拒绝。

具体语法、支持范围、JSON 字段和诊断规则见
[协议](Tools/OvReflectGen/Schema/PROTOCOL.md)。

## 实测环境

沿用 R0：LLVM/Clang 18.1.8 完整开发包、MSVC 14.42.34433、
Windows SDK 10.0.22621.0、C++20、x86_64-pc-windows-msvc。
生成器版本更新为 0.2.0，Schema 版本保持 1。

工具实际由 MSVC 构建并链接 LLVM/Clang 库。验收使用 Python 3；
本机 PATH 中的默认 python 仍为 2.7，不可直接使用。当前执行环境的
PowerShell 有 CET 启动问题，本次通过 cmd.exe 调用 Python 3 完成验收，
没有修改系统设置或全局 Git 配置。

## 验收结果

| 验收项 | 结果 |
|---|---|
| 正例的 Debug / Release / Publish Schema | 全部通过 |
| class / struct / enum / enum class 空 OV_TYPE | 全部通过 |
| 嵌套/inline 命名空间、跨命名空间同名类型 | 通过 |
| 显式 ID、多历史别名、默认属性身份 | 通过 |
| typedef canonical 类型、int8/uint64、std::string | 通过 |
| 负枚举值和 uint64 最大枚举值 | 精确保留 |
| getter/setter、ReadOnly、const 字段、方法 const/ref/noexcept | 通过 |
| 私有字段与 setter 的精确 Access<T> 友元授权 | 通过 |
| 重复 include、前向标注与定义、类外方法实现 | 不重复导出 |
| 相同输入重复运行 | Schema、报告、依赖、输入摘要字节一致 |
| 直接、间接、MSVC/SDK 系统头摘要 | 与实际文件 SHA256 一致 |
| 间接头 uint64 改为 uint32 | Schema 位宽与输入摘要同时变化 |
| 仅修改无语义影响的宏参数 | Schema 相同，输入摘要变化 |
| /J 无符号 char 模式下 std::string | 通过 |
| 51 个必须失败的独立反例 | 全部按预期诊断失败，且无旧成功产物 |
| R0 真实头文件三配置回归 | 通过 |

正例和真实组件派生夹具均在三个配置下用 **Clang 与 MSVC 普通模式**
实际编译检查。正例包含手写的测试 Access 特化，验证声明的友元授权
确实允许合法 C++ 访问。这里的编译检查是语法/类型检查，不声称已经
编译链接完整引擎或验证反射运行时。

真实组件夹具包含正式 CPhysicalBox、CTransform 头，使用真实继承关系，
并用正式 FVector3 配对 getter/setter；未替换任何第三方或业务类型。
默认 OvMaths/OvCore 业务头仍未标注，因此其公开 Schema 为空，这是
“仅显式导出”的预期行为；R0 的三个真实类型分析报告仍正常产生。

51 个反例覆盖非法/缺失/重载/私有 setter、重复 ID/别名/属性名、继承
冲突、枚举非法策略、union、指针/数组/容器/位域、静态成员、模板和
重载方法、局部/匿名/不完整类型、错误标注位置及非法元数据等。

正式验收入口：

~~~powershell
python Tools/OvReflectGen/reflection.py build
python Tests/Reflection/Integration/verify_r0.py
python Tests/Reflection/Integration/verify_r1.py
~~~

R1 最终证据：`Build/Tests/Reflection/r1-kzsjgll3/verification.json`。
对应目录保留 Positive、Real、Negative、UnsignedChar、DependencyChange
的 Schema、原生诊断、普通模式编译结果及输入摘要。
R0 回归证据：`Build/Tests/Reflection/r0-kurl_cm6/verification.json`。

## 边界与交接

- 未实现 R2 的描述运行时、TypeOf<T>()、注册表或生成 C++ 访问器。
- 未迁移生产组件、Inspector、序列化或 Lua，未改变对象生命周期。
- ID/别名冲突验证目前针对单个模块 Schema；全局注册冲突与模块归属
  仍需在后续注册表/生成依赖图中处理。
- 完整引擎构建依赖、增量跳过、单写者、原子发布和内容不变不改时间戳
  属于 R3。依赖摘要不能当作已经实现这些保证。
- CI 仅更新了工作流，本次没有远端执行结果。
- 保留进入本次任务前已有的未提交改动，没有创建 Git 提交。

按用户要求，完成 R1 后停下并重开对话。下一次可发送：

> 继续实现计划 v1 的 R2，先阅读 CLANG_REFLECTION_R1_VALIDATION.md、
> Tools/OvReflectGen/README.md 和 Tools/OvReflectGen/Schema/PROTOCOL.md。
> R1 本地 LibTooling 与 Clang/MSVC 普通模式验收已通过，LLVM 18.1.8
> 位于 Build/Toolchains/llvm-18.1.8；CI 尚未远端验证。
> 完成 R2 后停下并提醒我重开对话。
