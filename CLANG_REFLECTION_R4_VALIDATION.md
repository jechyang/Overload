# Clang Reflection R4 实施与验收记录

日期：2026-09-12 至 2026-09-13。阶段：R4 本地实现、三配置完整构建与真实行为验收全部完成；R5 尚未开始。
开始前已阅读 R3、R2 验收记录、实现计划 v1 和工具协议。
本轮没有 Git 提交、推送或远端 CI 执行。

## 实现

- 为 19 个原生组件标注类型元数据：17 个具体类型、CLight 与 CPhysicalObject
  两个抽象基类。名称从 AST 获取，旧 ComponentTraits 字符串作为显式 Alias。
- OvCore 生成单一的 GetOvCoreComponents 表。固定组件适配层仍在 OvCore；
  OvReflection 不引入 Actor、UI、XML 或 LLVM 依赖。生成版本 0.5.0，Schema 1。
- ComponentRegistry 显式注册 builtin/OvMaths/OvCore 类型并冻结。Actor 在 Main
  构造时、CreatedEvent 前初始化，保证静态库的生成描述和工厂被链接保留。
- Factory = AddComponent 通过强类型适配调用 Actor::AddComponent<T>()；
  Lookup = Existing 复用 Transform；抽象类型不创建实例，也不进入添加菜单。
  可添加性保留原来的按类型唯一性和物理家族互斥。反序列化仍兼容同一 Actor
  上有不同物理形状的旧数据，不误套用 Inspector 的更严格菜单限制。
- Actor 旧类型判断链和 Inspector 的 16 项重复表已删除，统一使用生成注册表。
  Inspector 的显示名、顺序、再次检查可添加性和现有事件刷新行为均保留。
- ComponentTraits 的 constexpr 兼容接口和各组件写档逻辑继续保留；生成的
  static_assert 要求其名称属于标注 Alias，防止二者静默分叉。注册与菜单
  不再读取手写 Traits 来枚举类型。属性 Drawer、XML 字段和 Lua 未迁移。
- 工厂、lookup 和可添加性统一检查应用注册表的 Main 权限，底层回调封装为
  private。只读类型查询可用于工作/渲染线程，不缓存 Actor 或组件指针。
- 未知类型、抽象类型节点、缺少 type/data 的节点保留完整 XML。数据独立于
  调用者文档生命期，重复加载不会复制积累；Actor/Scene/Prefab 保存均保留。
  未知节点写在已知组件之后，保留结构、属性、注释、CDATA，非逐字节格式保存。
- 修复验收发现的旧漏洞：类型擦除 RemoveComponent(AComponent&) 及基类模板
  删除可以移除 Transform，留下悬空引用。现在这些入口也保护 Transform；
  既有 Inspector 与 RemoveComponent<CTransform>() 的限制保持不变。

## 验收入口

~~~powershell
python Tests/Reflection/Integration/verify_r4.py --engine
python Tests/Reflection/Integration/verify_r0.py
python Tests/Reflection/Integration/verify_r1.py
python Tests/Reflection/Integration/verify_r2.py
python Tests/Reflection/Integration/verify_r3.py --fixtures-only
~~~

最终验收不使用 --skip-build，构建完整 Debug/Release/Publish 解决方案并
立即检查无修改重建的全部对象和生成文件内容/时间戳。保留正常 MSBuild
跟踪，不使用 ForceRebuild 或 TrackFileAccess=false。环境使用 R3 已验证
的工作区临时目录、标准系统目录变量、x64 主机工具和禁用节点复用设置。

本机 Python 3.12、LLVM 18.1.8、MSVC 14.42.34433、SDK 10.0.22621.0；
真实渲染上下文为 NVIDIA RTX 3070 Ti / OpenGL 4.5。
通用命令工具仍有 CET 启动问题，使用本地 Node 子进程执行工具链。

## 行为矩阵

- 19 个类型及旧别名查询、注册冻结、原 16 项菜单显示名和顺序。
- 16 类菜单添加/删除/恢复可添加性，重复添加不产生重复事件。
- Transform 复用、重复存档 Transform 唯一性、类型擦除和基类删除保护。
- 抽象工厂、策略缺失/非法 lookup、旧别名冲突必须给出源码诊断。
- 物理继承查询、菜单互斥、旧档多种物理形状兼容、基类删除。
- 17 个具体组件使用真实构造、OnSerialize/OnDeserialize，保存读取结果一致；
  Box 尺寸、Camera FOV、Transform 位置有非默认值断言。规范 ID 输入继续
  写旧名称，避免格式升级。
- 未知/缺失结构 XML 在输入文档销毁、重复加载/保存、Prefab 保存实例化后
  保留属性、嵌套数据、注释与 CDATA。
- 16 组 playing/sleeping/active/removal 组合，对比原 AddComponent 路径与
  工厂适配路径的构造、添加、Awake/Enable/Start、删除和析构顺序。
- 旧场景片段加载，Scene 相机/派生 Light 快速索引随事件正确更新。
  旧格式 Prefab 两次实例化、GUID/父子映射、保存重载和 Play 中实例化。
- 默认与真实 RenderThread 两种模式：组件操作仍在 Main；worker/render
  上工厂/读取可添加性拒绝访问，但只读描述查询有效。线程模式按正式 Context
  安装 baregl::ResourceExecutor，覆盖 ReflectionProbe 的真实 GPU 资源创建。
- R2 真实 Bullet 回归：修改 Box.Size 改变射线命中，恢复后重新不命中；
  RenderThread 上属性读写仍被拒绝。

夹具来源见 Tests/Reflection/Fixtures/R4/README.md：场景保留迁移前文件的
两个原始 Actor；Prefab 使用其旧格式载荷构造父子夹具，不声称为外部历史
Prefab 文件。另对全部具体原生组件执行真实序列化往返。

## 结果与证据

最终 R4 矩阵：Build/Tests/Reflection/r4-n3ih6pv7/verification.json，全部通过。
固定源码摘要：Build/Tests/Reflection/r4-final-source.json。

| 配置 | 完整解决方案 Build | 无修改 Build | 无修改重编译对象 |
|---|---:|---:|---:|
| Debug | 296.09 s | 2.25 s | 0 |
| Release | 113.97 s | 2.16 s | 0 |
| Publish | 12.78 s | 2.06 s | 0 |

表中为固定源码版本、保留既有对象缓存的最终完整解决方案 Build，
不是三个配置相同冷缓存条件下的性能比较。每个配置均检查全部对象与
生成文件的内容和时间戳，无修改构建完全不变。

三个配置全部通过 MSVC 原生组件/旧场景/Prefab/生命周期及 Bullet 行为
测试，每项均包含默认/RenderThread 两模式；Clang 生产生成对象编译通过。
四类非法组件策略均产生带源码位置的预期诊断。运行测试直接链接正式
静态库，不使用 whole-archive 或开发时覆盖对象。最终源码摘要无变化，
三配置全部已发布生成文件的 SHA256 与 generation-state 收据一致。
源码摘要另归档于最终矩阵目录的 source-receipt.json。

已完成的回归：

- R0：Build/Tests/Reflection/r0-jxd_aq_u/verification.json。
- R1：Build/Tests/Reflection/r1-i9zkok7b/verification.json，包含 51 个反例。
- R2：Build/Tests/Reflection/r2-we1n6z2i/verification.json，三配置 MSVC/Clang
  运行时和生产生成对象编译通过；真实物理行为纳入最终 R4 矩阵。
- R3：Build/Tests/Reflection/r3-52zwk3u3/verification.json，增量/失败恢复/
  同配置与多配置并发/进程终止锁恢复矩阵通过。本次未重复 R3 完整清理矩阵；
  其既有证据见 R3 验收记录。

早期三配置完整解决方案均已成功，耗时分别为 Debug 247.48 s、Release
252.81 s、Publish 263.30 s；随后仍有最后的保护与封装调整，这些耗时
仅作开发记录，最终结果以固定源码版本验收为准。

预验收中的异常与处理：

1. 首次组件测试触发原有 Transform 可删除漏洞，已修复并增加两个删除入口
   的回归断言。
2. 早期 RenderThread 测试未安装资源执行器，ReflectionProbe 分配缓冲失败；
   按正式 Editor/Game Context 补齐 ResourceExecutor 后，两模式测试通过。
3. 最终结果以正式静态库重建后的验收为准，不使用开发时的 Actor 覆盖对象。

## 边界与 R5 交接

- CI 已接入无 GPU 的 R4 入口，本轮未在远端执行。
- Inspector 验收涵盖完整编译、实际注册表菜单和对应添加事件；未做人工 GUI
  点击验收。专用组件 Inspector 回调和延迟刷新逻辑保留到 R5 审核。
- 未知组件保留解决的是数据不丢失，不会执行其行为；重新启用类型需重新加载。
- 当前原生注册适配针对 OvCore；动态插件/卸载、多模块用户组件注册不是 R4 范围。
- 下一阶段 R5：PhysicalBox.Size 与 Transform 的通用 Inspector/XML 试点，
  保留物理 setter 副作用、Transform 四元数存档/欧拉显示及整体 TRS 恢复。

完成 R4 后停下，重开对话继续 R5。

下一次可发送：

> 继续实现计划 v1 的 R5，先阅读 CLANG_REFLECTION_R4_VALIDATION.md、
> CLANG_REFLECTION_R2_VALIDATION.md 和 Tools/OvReflectGen/Schema/PROTOCOL.md。
> R4 三配置完整构建、无修改构建、原生组件/旧场景/Prefab/未知 XML、
> 生命周期及默认/RenderThread 行为均通过，CI 尚未远端验证。
> 完成 R5 后停下并提醒我重开对话。
