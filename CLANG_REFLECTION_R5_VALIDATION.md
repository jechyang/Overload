# Clang Reflection R5 实施与验收记录

日期：2026-09-13。状态：R5 本地实现与三配置完整验收完成。
范围：计划 v1 的 R5（第一轮最后阶段），不进入 R6。
开始前已阅读 R4、R2 验收记录、工具协议与实现计划 v1。
本轮没有 Git 提交、推送或远端 CI 执行。

## 实现

- OvReflection 增加保持 C++ 类型的属性值副本访问与预校验，使用 std::any；
  枚举使用精确十进制字符串和底层类型范围校验。既有强类型和 ValueRef
  接口保留，运行时仍只依赖标准库，不引入 XML、UI、Actor、数学或 LLVM。
- OvCore 增加公共 ReflectionProperties/XML 和 ReflectionDrawer 适配。
  基础数值、字符串、枚举、FVector3/FQuaternion 均可接入；整数 XML 与
  文本控件不经过 double，double 控件范围按 double 解析。
- 类型/属性枚举使用生成描述与能力标志。自定义 Drawer、SerializeHook、
  DeserializeHook 仅在元数据中保存字符串 ID，通过调用点显式传入的钩子表
  解析；未知钩子明确失败，不回退为有损的通用行为。
- Missing = Keep / Default / Error 与只读/解析失败/访问失败分别报告。
  默认值是显式字面量，生成器验证类型与范围，不执行任意组件构造函数。
  默认值、非法 Missing、Step 和 Min/Max 错误带源码位置诊断。
- Box.Size 保留基类序列化和手写 Inspector；size 字段与 UI 范围/步长保留，
  公共访问仍调用 SetSize，更新真实 Bullet shape/body。
- Transform 标注 position/rotation/scale，XML 继续写四元数 x/y/z/w，
  EulerAngles Drawer 继续以角度制欧拉角显示和编辑。TransformTRS 钩子
  先完整解析再调用一次 GenerateMatricesLocal，避免逐项应用的中间状态。
- 缺失/部分数学字段保持旧 Serializer 的默认语义：position、scale、size
  为零向量，rotation 为 (0,0,0,1)。特别是缺失 scale 不改成构造时单位缩放。
  非法数值明确失败；非法 Transform 数据不会部分修改 TRS。
- UI Binding 仅保存带弱有效期检查的回调；每次先检查应用 Main 和有效期，
  再创建调用期间的 ObjectRef。组件移除/Actor 销毁在通知前使旧回调失效；
  控件销毁后，即使复制过 DataDispatcher，旧回调也不能再修改对象。
- Inspector 在结构变化时立即停用原控件并合并延迟刷新。视图/焦点有效期
  保护 Actor 信息、删除/排序、逃逸的添加菜单回调和延迟任务，防止目标
  切换或面板销毁后访问旧对象。默认事件与调度仍使用 EditorActions；
  显式回调注入供宿主/测试使用，使真实 Inspector 可单独验收。
- 生成器版本 0.6.0，Schema 仍为 1。其他复杂组件与 Lua 保留手写路径。

## 验收入口

~~~powershell
python Tools/OvReflectGen/reflection.py build
python Tests/Reflection/Integration/verify_r5.py --engine
python Tests/Reflection/Integration/verify_r0.py
python Tests/Reflection/Integration/verify_r1.py
python Tests/Reflection/Integration/verify_r2.py
python Tests/Reflection/Integration/verify_r3.py --fixtures-only
~~~

R5 默认构建完整 Debug/Release/Publish 解决方案，随后立即检查全部对象与
生成文件的内容及时间戳是否保持不变。保留正常 MSBuild 文件跟踪，不使用
ForceRebuild、TrackFileAccess=false、whole-archive 或业务对象替代实现。
源码摘要在测试开始时保存，结束时检查仍一致。

没有 --engine 时仍执行基础属性 XML/Drawer 与真实 Inspector 面板测试、
三配置 MSVC/Clang 编译运行，但不声称验证真实 GL/RenderThread 行为。
面板测试链接正式编辑器对象与 SDK 库，只注入调度队列及选择事件观察器；
没有替换 Inspector、组件、物理、数学、UI 或第三方类型。

## 行为矩阵

- 全部受支持基础类型/枚举从不同初值的目标执行 XML 往返，逐属性比较；
  signed 64 最小值、unsigned 64 最大值和超过 double 精度范围的整数编辑。
- bool、浮点、字符串、整数、枚举及只读控件实际 DataDispatcher 读写；
  枚举选择、整数溢出拒绝、double 极大 UI 范围、属性旧别名加载。
- Missing/Default/ReadOnly 状态区分、缺失必填字段、非法数字/NaN、窄枚举
  越界和 XML 不支持控制字符的失败；解析失败不部分提交待应用属性。
- 自定义 Drawer/Serialize/Deserialize 钩子实际修改和往返；未知钩子失败。
- 默认/RenderThread 两模式中，Size Drawer 与 XML 反序列化放大真实 Box
  后射线由不命中变为命中；恢复尺寸后重新不命中。Render owner 的属性
  UI Binding/XML 访问被拒绝，应用 Main 写入继续正常。
- Transform 四元数存档、欧拉显示和编辑；局部/世界矩阵往返、父子层级
  传播、旧缺失/部分字段默认值、非法 TRS 的整体不变性。
- 两个试点的 Prefab 保存/恢复；Scene::Play 后使用与 EditorActions Stop
  相同的场景备份/LoadSceneFromMemory 路径恢复原属性，旧目标回调失效。
- 真实 Inspector 属性编辑、关闭删除、立即停用旧控件、重复旧关闭回调、
  待处理刷新期间切换目标、面板销毁、场景替换及选择事件次数。
- R4 原生组件、旧场景/Prefab、未知 XML、Transform 唯一性和生命周期
  行为矩阵随 R5 三配置、两种渲染模式重新执行。

## 结果与证据

最终固定源码矩阵：Build/Tests/Reflection/r5-4m3bypg9/verification.json，全部通过。
源码摘要：Build/Tests/Reflection/r5-4m3bypg9/source-receipt.json，结束时一致。
生成收据：Build/Tests/Reflection/r5-4m3bypg9/generated-receipts.json，三配置各 14 个产物
与 generation-state 中的 SHA256 全部一致。

| 配置 | 完整解决方案 Build | 无修改 Build | 无修改重编译对象 |
|---|---:|---:|---:|
| Debug | 14.80 s | 2.16 s | 0 |
| Release | 2.36 s | 2.36 s | 0 |
| Publish | 254.70 s | 2.14 s | 0 |

上述为复用既有对象缓存的完整解决方案 Build，不是相同冷缓存条件下的
配置性能比较。每个配置均立即检查全部对象及生成文件的内容和时间戳；
无修改构建全部不变。

三配置全部通过 MSVC/Clang 属性夹具编译运行、Clang 生产适配对象编译、
真实 Inspector 面板测试、默认/RenderThread 引擎测试和 R4 原生兼容矩阵。
六类新增元数据反例均给出预期源码诊断。

前序回归证据：

- R0：Build/Tests/Reflection/r0-l63pl1gm/verification.json。
- R1：Build/Tests/Reflection/r1-r5ncelmr/verification.json，三配置及 51 个反例。
- R2：Build/Tests/Reflection/r2-ikjgpw36/verification.json，三配置 MSVC/Clang
  严格编译、访问规则、静态库保留和生产生成对象；真实物理行为由 R5 覆盖。
- R3：Build/Tests/Reflection/r3-pyvciar0/verification.json，增量失效、失败恢复、
  同配置/多配置并发及进程终止锁恢复。未重复全目录清理构建矩阵，既有
  清理验证见 R3 记录；本轮重复完整解决方案和无修改构建。

本机沿用 R4 的 Python 3.12、LLVM 18.1.8、MSVC 14.42.34433、SDK
10.0.22621.0 和 NVIDIA RTX 3070 Ti/OpenGL 4.5。通用命令工具仍有 CET
启动问题，通过本地 Node 子进程执行；未修改全局 Git 或系统设置。

预验收记录：

- r5-m1_ayh87 为早期 Debug 全套通过记录；最后修正了 double 控件范围
  经过 float 的问题，并加强属性等价和旧 Actor 信息回调断言。
- r4-yj2xnt16、r5-cyvthpg9 在开发期间发现源码/生成器变更引起对象或生成
  收据变化，因此无修改检查按预期拒绝这些结果；它们不是最终验收证据。
- 最终以 r5-4m3bypg9 的固定源码收据和全部通过结果为准。

## 边界

- CI 已接入无 GPU 的 R5 入口，尚未远端验证。
- UI 验收执行真实面板、控件、回调与延迟队列，未进行人工 GUI 点击验收。
  Play/Stop 验证正式场景备份/恢复路径，不声称启动了完整编辑器并点击工具栏。
- 通用 XML 在适配范围内预解析；既有基类处理和任意业务 setter 异常的
  外部副作用不提供通用事务回滚。Transform 单独保证整个 TRS 的恢复提交。
- 原组件/Actor 所有权、场景生命周期及 Main 线程约束继续适用；没有新增
  跨帧裸 ObjectRef 或可卸载插件句柄。复杂自定义钩子负责自身回调生命周期。

完成 R5 后停下，提醒用户重开对话。R0–R5 即计划 v1 第一轮，不自动开始 R6。
