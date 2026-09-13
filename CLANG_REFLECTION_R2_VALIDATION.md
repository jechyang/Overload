# Clang Reflection R2 实施与验收记录

日期：2026-09-12。阶段：R2 本地实现与三配置完整行为验收完成；R3 尚未开始。
执行前已阅读 R1 验收记录、实现计划 v1、工具 README 和协议。
本次没有提交 Git、推送远端或触发 GitHub Actions。

## 实现

- 新增独立 OvReflection 静态库：TypeDesc、PropertyDesc、ValueType、
  MethodDesc、枚举描述、Traits/TypeOf 和 Registry。运行时仅依赖 C++ 标准库。
- 提供 bool、C++ 有符号/无符号整数、float/double、std::string 的固定描述。
  逻辑字符串 ID 与进程内 C++ 类型 token 分开；typedef 不产生另一个 C++ 类型。
  枚举值保留精确十进制字符串，整数读写不经过 double。
- Registry 按模块显式注册，批量检查模块归属、C++ 类型身份和全局
  类型/属性/方法 ID、别名冲突；失败不部分注册。Freeze 后拒绝修改，
  初始化完成并同步发布后可跨线程只读查询。
- ObjectRef/ValueRef 仅从带类型的左值创建，保留 const 性；没有公开的
  裸指针与描述配对构造器。类型擦除操作在访问前检查对象兼容性、值类型、
  可写权限和线程。底层基类转换回调保持私有。
- 生成 Access<T> 特化，字段使用合法 C++ 成员表达式，属性调用既有
  getter/setter。只读属性不生成强类型 setter。私有成员必须有精确友元授权；
  未标注成员不会产生访问器。不记录或依赖 AST 成员偏移。
- 为正式 FVector3 的 x/y/z 和 CPhysicalBox.Size 增加标注。Box 保留旧
  ComponentTraits 字符串作为别名，size 保留既有名称和 Main 约束。
  Actor 注册、组件工厂、Inspector、序列化、Lua 及生命周期实现未迁移。
- 增加独立 Python C++ emitter，消费语言无关 Schema，输出固定名称的
  Reflection.generated.h/.cpp 和 manifest.json。生成器版本为 0.3.0，
  Schema 版本保持 1，新增兼容字段 cast_bases。
- Schema 的 bases 保留直接基类，cast_bases 保存包含未标注祖先的合法
  继承链。修复跨未标注中间基类时的转换及继承属性冲突检查。
- 模块 Schema 仍含显式标注的 include 依赖以便校验；正式 C++ 生成按
  Sources/<module>/ 归属输出，避免 OvCore 重复生成 FVector3。找不到
  扫描模块归属的已标注依赖会失败。
- 原生分析与驱动失败会清理旧生成头、源文件和 manifest。输入摘要包含
  Python emitter 源码。Premake 仅接入固定运行时库及必要 include 路径；
  自动生成依赖图、增量失效、单写者和原子发布仍属于 R3。

## 验收方法

入口：

~~~powershell
python Tools/OvReflectGen/reflection.py build
python Tests/Reflection/Integration/verify_r0.py
python Tests/Reflection/Integration/verify_r1.py
python Tests/Reflection/Integration/verify_r2.py --engine
~~~

无 OpenGL 4.5 环境时可省略 --engine；该模式仍实际执行三配置 MSVC/Clang
反射运行测试，并编译正式模块生成对象，但不能算作真实物理/RenderThread 验收。

基础运行测试把运行时和生成源编译进静态库，使用 /OPT:REF 和 /OPT:ICF
链接，不使用 whole-archive。另有只包含固定运行时头、仅按 ID 查询的
消费者，通过显式 RegisterFixtureTypes 保留生成描述。

真实引擎测试从正式 SDK/依赖项目构建静态库，链接正式 Actor、
CPhysicalBox、OvPhysics/Bullet、RenderThread 和 GL 驱动；没有替换
组件、数学、物理或第三方类型。测试按引擎既有顺序唤醒并启用 Actor，
验证初始射线不命中、反射放大 Size 后命中、强类型 setter 恢复尺寸后
再次不命中。开启实际 RenderThread 后，Main 修改仍有效，Render owner
上的描述查询可用，组件读取、擦除写入和强类型写入均返回 WrongThread。

## 结果

| 验收项 | 结果 |
|---|---|
| Debug / Release / Publish，MSVC 与 Clang 基础运行测试 | 通过 |
| 两个正式模块的生成 C++，三配置、两种编译器对象编译 | 通过 |
| 独立按 ID 查询消费者、静态库保留生成描述 | 通过 |
| bool、8/16/32/64 位整数极值、浮点、含 NUL 字符串、枚举往返 | 通过 |
| signed 64 最小枚举值、unsigned 64 最大枚举值描述 | 通过 |
| getter/setter 副作用，UI 范围不裁剪业务值 | 通过 |
| 私有字段/私有 setter、const 对象读取 | 通过 |
| 错误对象/值类型、const 对象/输出、只读写入拒绝 | 通过 |
| 非零基类调整、跨未标注基类、禁止未经检查的向下转换 | 通过 |
| 重复模块、全局别名/属性 ID 冲突、批量回滚、冻结后注册 | 通过 |
| Main/Any 访问规则、只读元数据跨线程查询 | 通过 |
| 相同输入的 C++/manifest 字节一致，失败清除旧 C++ | 通过 |
| 跨未标注中间基类的继承属性冲突诊断 | 通过 |
| R0 三配置真实头回归 | 通过 |
| R1 三配置与 51 个反例回归 | 通过 |
| 三配置真实 SDK 构建及默认/RenderThread 物理行为 | 全部通过 |

R0 回归证据：Build/Tests/Reflection/r0-tvznne5s/verification.json。
R1 回归证据：Build/Tests/Reflection/r1-7cszrn8w/verification.json。
R2 完整证据：Build/Tests/Reflection/r2-66onq3zg/verification.json。
该目录保留六组 MSVC/Clang 运行及只按 ID 查询测试、六组正式引擎模式
测试、三配置 SDK 构建日志、各步命令、运行时动态依赖及 R2 源码 SHA256。
Publish/Clang 测试程序仅依赖系统与 C++ 运行库，没有 LLVM/Clang 动态依赖。
默认 Build/Generated/Reflection 已刷新，三配置的生成头、源文件和 manifest
均与已编译验收产物字节一致。

## 本机环境与构建说明

LLVM/Clang 18.1.8、MSVC 14.42.34433、Windows SDK 10.0.22621.0，
C++20、x86_64-pc-windows-msvc。真实 GL 验收使用 NVIDIA RTX 3070 Ti，
OpenGL 4.5。Python 使用 C:/Users/Administrator/AppData/Local/Programs/Python/Python312/python.exe；
PATH 的默认 Python 2.7 不适用。

通用命令通道存在 CET 启动问题，本次通过可用的本地 Node 子进程通道
调用工具链。MSBuild 在受限执行环境中的文件跟踪会报权限错误；验收
构建关闭跟踪、强制编译，并禁用与项目无关的机器级 vcpkg 接入。
该环境还缺少 PROCESSOR_ARCHITECTURE，导致 MSBuild 将 x64 主机编译器
回退到 x86，baregl 因此耗尽编译器堆。验收参数明确选择 AMD64/x64 后，
隔离 baregl 构建和真实组件预运行成功。没有修改系统设置或全局 Git 配置。

这些设置只作用于验收命令；不能把强制构建结果当作 R3 增量构建保证。
失败尝试的日志保留在各自的 Build/Tests/Reflection/r2-* 目录。
CI 已增加无 GPU 的 R2 入口，本次没有远端 CI 执行结果。

## 边界与交接

- MainThread 在应用 Main 初始化时捕获线程；所有组件访问必须复用
  registry.Main()。在工作线程自行创建一个新的 authority 不能代表应用 Main。
- ObjectRef/ValueRef 是本次调用期间的借用视图，不拥有对象，不检测销毁；
  不得跨帧/跨场景缓存。Any 不自动提供数据同步。
- 运行时按静态库/同一原生程序使用；跨 DLL token 身份、可卸载插件和
  面向不可信脚本的句柄/生命周期协议不属于 R2。
- Factory/Lookup 暂为描述元数据，模块注册不执行组件构造；通用方法调用
  仍推迟到 R6。没有扩展到 R4/R5 的生产注册或 Inspector/XML 迁移。
- 下一阶段为 R3：正式生成依赖图、清洁构建、增量失效、配置隔离、
  单写者、原子发布与失败阻断。

完成 R2 后停下，重开对话再继续 R3。

下一次可发送：

> 继续实现计划 v1 的 R3，先阅读 CLANG_REFLECTION_R2_VALIDATION.md、
> Tools/OvReflectGen/README.md 和 Tools/OvReflectGen/Schema/PROTOCOL.md。
> R2 三配置 MSVC/Clang 及真实 Bullet/RenderThread 验收已通过；
> CI 尚未远端验证。完成 R3 后停下并提醒我重开对话。
