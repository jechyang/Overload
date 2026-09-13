# Clang Reflection R6 实施与验收记录

日期：2026-09-13。范围：计划 v1 的 R6 C# 原生 API 绑定生成扩展。
状态：R6 本地实现、三配置完整验收和 R0–R5 回归全部完成。
开始前已阅读实现计划 v1、R5 验收记录和现有生成协议。
本轮没有 Git 提交、推送、远端 CI 或全局工具链设置变更。

## 实现

- 明确并实现 [Script ABI 1](Tools/OvReflectGen/Schema/SCRIPT_ABI.md)：
  Windows x64/cdecl、固定宽度标量、成员复制的 Vector3/Quaternion、UTF-8
  字节长度与 native release、Main 线程、异常状态转换、借用对象句柄。
- OvReflection 增加 native Context。上下文与对象使用不复用的 64 位 ID；
  对象条目携带弱有效期，每次先检查 Main、有效期、类型和 const 权限。
  Context 关闭、组件删除和场景替换后旧句柄不能继续访问对象。
- OvCore::Scripting::PublishComponent 复用 Actor 在删除前失效的组件 epoch，
  不增加 GC/Actor 所有权，不向常驻注册表保存可卸载程序集或委托。
- Script 属性/方法生成类型化 C ABI 函数表和 C# 包装。未标 Script 的成员
  不导出，只读属性不生成 setter。普通反射描述仍保留其既有用途。
- 新增 Return = Copy、Ownership = Borrowed、Nullable 和 Overload 策略，
  检查引用复制、对象可空性、精确重载、线程、mutable ref 与名称冲突。
  C++ 访问器使用精确成员函数指针保留 const/ref/noexcept 与 friend 授权。
- 模块产物新增 Script.generated.h/.inc/.cs 和 script-abi.json；纳入既有
  staging、manifest、SHA256 收据、失效清理与内容不变不更新时间戳流程。
  C# Api 构造时检查 ABI 版本、函数表大小和完整签名摘要。
- 生成基础数值/数学属性的批量读取，返回已完成前缀，遇错误停止；
  不把批量接口隐式扩展为通用容器或事务性批量写入。
- 生产试点：Box.Size、Transform position/rotation/scale 与 TranslateLocal。
  调用仍进入现有 setter/method，保留物理和层级矩阵副作用。
- 补齐 FQuaternion 的显式默认复制赋值，解决 Clang 严格编译警告。
- 真实场景回归发现 FTransform 临时对象析构未移除父级通知，导致父级随后
  通知已销毁对象；修复析构和重新挂父级的解绑。原生 C# 引擎测试覆盖
  临时参考 Transform 结束后继续删除组件、替换和销毁真实场景的路径。
- 生成器版本 0.7.0；Schema 保持 1。标准托管项目与说明位于
  Examples/ReflectionScript，CI 增加无 GPU 的 R6 验收入口。

## 验收入口

~~~powershell
python Tools/OvReflectGen/reflection.py build
python Tests/Reflection/Integration/verify_r6.py --engine
python Tests/Reflection/Integration/verify_r0.py
python Tests/Reflection/Integration/verify_r1.py
python Tests/Reflection/Integration/verify_r2.py
python Tests/Reflection/Integration/verify_r3.py --fixtures-only
python Tests/Reflection/Integration/verify_r5.py --skip-build --engine
~~~

R6 默认执行完整 Debug/Release/Publish 解决方案 Build，立即验证无修改
Build 的全部对象及生成文件内容/时间戳。保持正常 MSBuild 文件跟踪，
不使用 ForceRebuild、TrackFileAccess=false 或 whole-archive。
--skip-build 仅供复用已完成构建的行为回归；--engine 启用真实 GL/Bullet。

## 行为覆盖

- MSVC/Clang 严格编译 C++、生成 C 头的纯 C 编译、静态反射与 DLL 链接。
- 真正通过 CoreCLR 调用两种编译器生成的 DLL：bool、8/16/32/64 位有符号
  与无符号整数极值、float/double、64 位枚举、Vector3/Quaternion。
- UTF-8 中文、emoji、内嵌 NUL、空串、大字符串及 5000 次拥有型输出释放；
  拒绝非法 UTF-8、非空长度空指针、C# null 字符串与未配对 UTF-16 代理项。
- 显式重载、引用返回复制隔离、方法/private field、只读写入和错误类型。
- 对象参数/返回与属性、可空/不可空策略、未发布对象、过期对象、对象替换
  不复用 ID、Context 关闭与新 Context 下旧句柄拒绝。
- Main/工作线程与真实 Render owner 的访问拒绝；标准及非标准 C++ 异常
  转换为状态，C# BridgeException 可捕获，未越过 ABI 展开栈。
- ABI 版本、大小、签名不匹配；修改 C# 签名后构造 Api 即失败。
- 十轮 collectible AssemblyLoadContext 加载、执行、卸载，使用 WeakReference
  与 GC 验证可卸载程序集没有被原生注册表保留。
- 批量前缀失败语义与结果校验；预热后测量单次 getter、方法和 256 项批量
  的实际调用开销，结果求和防止测量只执行空循环。
- 真正由 C# 通过生产生成表修改 Box.Size，观察 Bullet 射线命中/不命中；
  更新 Transform TRS、TranslateLocal、比较父子层级 world matrix；删除组件
  与替换真实 Scene 后旧句柄失败，新句柄可用。两种渲染模式均执行。
- R0–R3 生成/访问/失效回归与 R4/R5 旧存档、Inspector、生命周期行为回归。

## 最终结果

最终固定源码矩阵：Build/Tests/Reflection/r6-3a6n95b4/verification.json，114 项检查全部通过。
源码摘要：Build/Tests/Reflection/r6-3a6n95b4/source-receipt.json，652 个文件在开始与结束时一致。
生成收据：Build/Tests/Reflection/r6-3a6n95b4/generated-receipts.json，三配置各 22 个产物与
generation-state SHA256 全部一致；OvCore 的 ABI 签名在三配置间一致。

| 配置 | 完整解决方案 Build | 无修改 Build | 无修改重编译对象 |
|---|---:|---:|---:|
| Debug | 3.23 s | 1.73 s | 0 |
| Release | 268.30 s | 1.77 s | 0 |
| Publish | 252.78 s | 1.70 s | 0 |

这是复用既有缓存的完整解决方案 Build，不是统一冷缓存下的配置性能比较。
Debug 预验收已经完成了大部分编译，最终固定源码轮只需更新剩余对象。

| 配置 / native 编译器 | getter ns/次 | method ns/次 | batch ns/项（256 项） |
|---|---:|---:|---:|
| Debug / MSVC | 245.81 | 242.60 | 111.56 |
| Debug / Clang | 258.84 | 257.06 | 117.56 |
| Release / MSVC | 32.14 | 31.78 | 6.36 |
| Release / Clang | 34.56 | 33.75 | 7.39 |
| Publish / MSVC | 32.53 | 31.24 | 5.98 |
| Publish / Clang | 34.22 | 34.46 | 8.20 |

原始测量见各配置的 MSVC-coreclr.log/Clang-coreclr.log；汇总见
Build/Tests/Reflection/r6-3a6n95b4/benchmarks.json。每条路径预热后测量 262144 次读取/调用，
批量宽度 256；校验实际结果总和。数值只反映此机器、此夹具和本轮负载，
不是物理 setter、所有对象分布或完整游戏帧的性能保证；不与 Debug 构建
时间作因果比较。

前序回归证据：

- R0：Build/Tests/Reflection/r0-jzxwo0q3/verification.json。
- R1：Build/Tests/Reflection/r1-h1hug6z2/verification.json，三配置及 51 个反例。
- R2：Build/Tests/Reflection/r2-f7hejw60/verification.json，三配置 MSVC/Clang
  严格编译运行、静态库保留和全部新产物失败失效清理。
- R3：Build/Tests/Reflection/r3-gnwuu7it/verification.json，直接/间接/宏/文件
  变化、失败恢复、收据破坏、同配置/多配置并发、进程终止锁恢复。
- R4/R5 Debug：Build/Tests/Reflection/r5-_kbv4rwq/verification.json。
- R4/R5 Release：Build/Tests/Reflection/r5-a3s0idnj/verification.json。
- R4/R5 Publish：Build/Tests/Reflection/r5-8n1qfydb/verification.json。
- 补充 emitter 检查：Build/Tests/Reflection/r6-extra-cs1fduxc/verification.json，
  非法名称/批量名称冲突带源码诊断并清除旧输出，枚举/可空性/线程策略
  改变 ABI 摘要。入口为 Build/Tests/Reflection/r6-script-emitter-checks.py。

开发期记录：

- r6-r6kwm9si：严格编译发现局部命名遮蔽；修复后重新验收。
- r6-6t2wxy5u：MSVC/CoreCLR 已通过；Clang 指出旧 FQuaternion 隐式复制赋值
  警告，补上显式默认赋值。
- r6-5i3a9o41：开发中变更生成器，使无修改构建正确拒绝对象变更。
- r6-oqr5z14_：夹具通过，真实引擎的场景销毁暴露 FTransform 失效回调；
  修复后最终 r6-3a6n95b4 在两种渲染模式及三配置全部通过。

以 r6-3a6n95b4 为最终 R6 证据，不将开发期成功片段拼作固定源码矩阵。

## 边界

- 这是原生 API 绑定生成扩展。引擎中的 hostfxr/CoreCLR 启动、用户 C#
  Behaviour 元数据、调度、热重载状态迁移和 Lua 替换没有在 R6 中接入。
- 本机使用 Python 3.12、LLVM 18.1.8、MSVC 14.42.34433、Windows SDK
  10.0.22621.0、Visual Studio Roslyn 和已有 CoreCLR 6.0.15。没有 .NET SDK；
  验收使用真实 C# 编译器与运行时，不伪造托管调用。net8.0 SDK 示例项目
  已提供，但本机未执行其 dotnet build；不把 CoreCLR 6 的验证声称为 .NET 8。
- CI 工作流已接入，但尚未远端运行。没有人工点击完整编辑器的验收声明。
- 原生 DLL 必须在所有包装和调用结束前保持加载；不能把进程内 ABI 指针
  当作不可信脚本的安全沙箱。对象仍由 native owner 在 Main 维护有效期。
  native 业务函数若返回早已悬空的指针，生成器不能推断其历史对象身份。
- 通用方法异常转换不提供业务副作用回滚；批量读取失败保留已完成前缀。
- R3 本轮重复失效/恢复/并发夹具，完整清理构建矩阵沿用 R3 验收记录；
  R6 本轮已重新执行完整解决方案 Build 和无修改 Build。
