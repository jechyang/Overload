# Clang 静态反射实现计划 v1

日期：2026-09-12。适用仓库：H:/overload/Overload。
版本：v1，类型标注统一为 OV_TYPE，自动提取名称，Id 可选，不使用 GENERATED_BODY。
状态：R0–R6 已完成本地实现与各阶段验收。R6 已通过三配置完整解决方案/无修改构建、MSVC/Clang C ABI、真实 CoreCLR C# 调用、默认/RenderThread 生产组件行为及 R0–R5 回归（2026-09-13）。CI 已接入但未远端执行。R6 范围是原生 API 绑定生成；引擎 CoreCLR 宿主启动、C# Behaviour 调度及 Lua 替换仍为后续独立工作。详见 [R0](CLANG_REFLECTION_R0_VALIDATION.md)、[R1](CLANG_REFLECTION_R1_VALIDATION.md)、[R2](CLANG_REFLECTION_R2_VALIDATION.md)、[R3](CLANG_REFLECTION_R3_VALIDATION.md)、[R4](CLANG_REFLECTION_R4_VALIDATION.md)、[R5](CLANG_REFLECTION_R5_VALIDATION.md)、[R6 验收记录](CLANG_REFLECTION_R6_VALIDATION.md)。已实现协议见 Tools/OvReflectGen/Schema/PROTOCOL.md 与 Tools/OvReflectGen/Schema/SCRIPT_ABI.md。

## Git 提交归档（2026-09-13）

R0–R6 实施期间没有逐阶段创建 Git commit，各阶段改动累积在同一工作区，生成器与引擎接入文件在后续阶段持续演进。本次按用户要求将当前已完成验收的 R0–R6 实现统一归档为一个本地提交，附带全部阶段验收记录；这不是七个历史阶段快照。各验收记录中的“本轮未提交”描述的是当时实施轮次。本次不 push。

## 1. 目标与架构决策

以 Clang LibTooling 构建独立的 OvReflectGen 工具，分析显式标注的 C++ 声明，生成可由现有 MSVC / Clang 编译的反射描述与访问代码。引擎及游戏发布包不依赖 LLVM。

第一轮完成：可靠的代码生成构建链、基础类型与属性反射、组件注册迁移、两个代表组件的 Inspector / 序列化试点。C# 代码生成作为后续扩展，不阻塞第一轮验收。

固定决策：

- 保留 C++20、Premake 和 Windows MSVC 主构建；分析器使用固定版本 LLVM/Clang，不强制切换引擎编译器。
- 使用独立 LibTooling 工具，不使用编译器插件，不将 AST dump JSON 作为正式分析接口。
- 显式标注决定公开范围；不自动导出所有 public 字段和方法。
- 公开类型宏统一为 OV_TYPE()；class / struct / enum 类别、类名、命名空间和基类由 Clang 自动提取，不要求填写包名或类型名。第一版只提供一个类型标注入口，不提供按声明类别拆分的同义宏。
- 类型 Id 可选：默认采用规范化的 C++ 完整类型名，重命名以显式 Id 或历史别名迁移。
- 第一版不引入 OV_GENERATED_BODY，不向类体注入生成成员；类型查询采用类外 TypeOf<T>() 和注册表。
- 分析器、语言无关的元数据模型、各输出生成器分离。
- 提供按 C++ 类型查询的类外 Traits / TypeOf<T>()，以及按 ID 查询的运行时只读注册表；高频访问使用生成的强类型调用。第一版不承诺所有描述及容器均可在 constexpr 上下文使用。
- 属性支持数据成员与 getter/setter 两种访问方式。组件优先使用逻辑属性。
- 生成合法 C++ 访问表达式，由目标编译器决定布局；不固化分析器计算的成员偏移。
- 原生组件和资源继续由 C++ 管理；反射不改变对象所有权、生命周期或线程模型。
- 保留专用 Inspector、序列化和批量更新钩子；通用反射不是强制替代所有手写实现。

## 2. 当前代码接入点

| 位置 | 当前行为 | 计划变化 |
|---|---|---|
| Sources/OvCore/include/OvCore/ECS/Components/AComponent.h | ComponentTraits 基础声明 | 兼容保留，逐步由生成元数据提供类型身份 |
| 各组件头文件 | 手写 ComponentTraits 名称 | 增加稳定类型标注与旧名称别名 |
| Sources/OvCore/src/OvCore/ECS/Actor.cpp | 按类型名逐项判断并创建组件 | 接入显式注册的组件工厂适配 |
| Sources/OvEditor/src/OvEditor/Panels/Inspector.cpp | 手写可添加组件列表 | 从组件描述读取名称、顺序、类别与添加策略 |
| Sources/OvCore/src/OvCore/ECS/Components/CPhysicalBox.cpp | Size 的序列化、Inspector 都调用 getter/setter | 首个逻辑属性试点，保留基类处理和物理副作用 |
| Sources/OvCore/src/OvCore/ECS/Components/CTransform.cpp | 四元数存档、欧拉角编辑、整体恢复 TRS | 验证自定义 Drawer 与批量反序列化钩子 |
| Sources/OvCore/src/OvCore/Scripting/Lua/Bindings | 手写脚本接口 | 第一轮保持不变，后续作为绑定覆盖清单 |
| premake5.lua / Sources/*/premake5.lua | 编译参数、源文件与项目依赖 | 增加生成任务、参数导出与输出依赖 |

## 3. 模块和输出布局

建议新增以下目录，实施时遵循仓库既有命名风格：

```text
Tools/OvReflectGen/
  Frontend/          Clang AST、标注提取、源位置、依赖收集
  Schema/            元数据模型与校验
  Emitters/          C++ 描述、注册、后续绑定输出
Sources/OvReflection/
  include/           标注宏、描述类型、注册表、访问协议
  src/               最小运行时支持
Tests/Reflection/
  Fixtures/          正例与必须失败的反例
  Integration/       实际组件、编辑器和场景回归
Build/Generated/Reflection/<target>/<config>/<module>/
  schema.json
  Reflection.generated.cpp
  Reflection.generated.h
  manifest.json
```

OvReflection 是轻量基础模块，不依赖 OvCore、UI、tinyxml2、sol2、CoreCLR 或 LLVM。组件工厂与 XML 适配放在 OvCore，Inspector 适配放在合适的现有编辑器/GUI 层。只有 OvReflectGen 依赖 LLVM。

每个模块使用确定的聚合输出文件名，避免 Premake 在生成工程时无法发现尚未产生的源文件。生成文件不手工修改；默认忽略 Build 下产物，并保证干净检出能够生成。

Reflection.generated.h 只供类型查询消费者及生成实现包含；业务类型头文件只包含固定的标注/访问授权头，不包含对应的生成头文件。OvReflectGen 的扫描翻译单元不依赖生成输出。需要 TypeOf<T>() 的消费者显式包含相应模块生成声明，注册表仅按 ID 查询的调用方不必包含该模块全部类型。

## 4. 第一版协议

### 4.1 标注

面向使用者的类型标注统一为 OV_TYPE；成员标注为 OV_FIELD、OV_PROPERTY、OV_METHOD。OV_TYPE() 仅表示类型参与反射，具体声明类别由 Clang AST 判断。标注分析模式展开为 Clang annotate，普通编译模式展开为空；括号仅包含可选策略与元数据。私有成员访问另行采用显式 friend 访问器，friend 声明在分析和正式编译中一致存在；不使用修改访问控制的预处理技巧。

| 标注 | 用途 | 分析器校验 |
|---|---|---|
| OV_TYPE | class / struct / enum / enum class 声明 | 按 AST 节点分派，保留声明类别，校验该类别允许的元数据；枚举提取底层类型、枚举项和值 |
| OV_FIELD | 实际数据成员 | 校验访问权限与字段类型 |
| OV_PROPERTY | getter/setter 表达的逻辑属性 | 校验配对与读写类型 |
| OV_METHOD | 明确导出的方法 | 校验签名、重载选择与支持范围 |

拟议使用形式（宏位于符合 Clang 属性语法的声明位置）：

```cpp
class OV_TYPE(Component)
CPhysicalBox : public CPhysicalObject
{
public:
    OV_PROPERTY(Setter = SetSize, Serialize, Inspect, Name = "size")
    OvMaths::FVector3 GetSize() const;

    void SetSize(const OvMaths::FVector3& value);
};

struct OV_TYPE() FVector3;
enum class OV_TYPE() ProjectionMode;
```

以上仅展示标注放置方式；实际生成完整属性/枚举描述需要找到对应定义。分析器对 OV_TYPE 标注的 CXXRecordDecl 处理类/结构体，对 EnumDecl 处理枚举，并保留各自的声明类别。统一类型入口不合并各类别的分析和生成规则；元数据模型按类别与能力生成不同输出。类标注只启用类型发现，不自动公开私有成员，不自动生成默认构造工厂；第一版不提供 GENERATED_BODY，组件工厂继续使用显式创建策略。

Component 等参数表示引擎语义，须与声明类别、继承关系及工厂策略一起校验；不因 struct 关键字自动认定值类型或 ABI 可直接复制，也不因 class 关键字自动认定引擎对象。第一版仍不支持 union，标注 union 必须报错。

标注只能增加元数据，不得令分析模式与正式编译模式具有不同的成员、基类或函数声明。首版生成访问代码和描述均在类外，不要求类体注入或业务头文件包含生成头。

类型完整名称从语义声明中提取；普通类可仅写 OV_TYPE()。仅当业务需要固定身份时，使用 OV_TYPE(Id = "game.Player", ...) 覆盖默认名称，不重复声明包名。字段默认名称为成员名；逻辑属性按约定从 GetX / IsX 提取 X，无法推导或出现冲突时要求 Name 显式指定。上例 Name = "size" 用于保留已有 XML 键，不是必填类型标识。类型签名、继承关系和字段底层类型均由 AST 推导，不在宏参数中复写。

OV_FIELD 标记真实字段，OV_PROPERTY 标记 getter/setter 逻辑属性；这是本版自定义协议，与 UE 的 UPROPERTY 命名含义不完全相同。无需为追求 UE 形式而引入 UHT 的类体注入约束。

属性标注表达：稳定 ID、存档名、旧别名、getter/setter 配对、Serialize / Inspect / Script 权限、只读状态、线程亲和、编辑器展示信息。编辑器范围与业务验证分开，不能自动把 UI 范围变成所有写操作的裁剪规则。

方法标注表达：稳定 ID、完整签名、导出名、线程亲和与参数适配规则。R1/R2 先建立签名模型，通用方法调用及脚本桥接推迟到 R6。

### 4.2 元数据模型

- Type：持久化逻辑 ID、C++ 完整名称、类别、基类、Schema 版本、历史别名、源位置。
- Property：持久化逻辑 ID、类型、访问策略、存档名、别名、能力标志、编辑元数据与可选定制器。
- Method：逻辑 ID、返回值、参数、const/ref 限定、明确选定的重载及边界策略。
- Component 扩展：显示名、排序、类别、创建/取得策略、唯一性与编辑器可添加性。

首版以可追溯的逻辑字符串作为持久化身份：类型未指定 Id 时使用规范化的完整 C++ 类型名，属性未指定 Id 时使用声明类型身份与逻辑属性名组成的键；显式 Id 可覆盖默认值。默认名称在类或命名空间改名时会变化，不能称为跨重命名稳定的身份。需要兼容存档时必须保留原 Id 或添加历史别名，并通过旧资源夹具验证。

可生成确定性数值索引用于运行时加速，但必须检查冲突，不能用数组下标或 std::hash 的结果充当存档协议。Clang USR、typeid 名称、源代码行号和成员偏移不作持久化 ID。重复 ID、别名冲突、继承属性冲突都必须产生定位到源码的诊断。生成器自身的 Schema 版本与业务存档版本分别管理。

### 4.3 支持范围

第一版支持 bool、固定宽度整数、float/double、字符串、枚举、FVector3 / FQuaternion 等有专用适配的值类型，普通数据字段、单继承组件和非模板 getter/setter。整数不统一转成 double。

首版不提供自动处理：任意模板、容器、联合体、位域、虚继承/多继承、任意裸指针、函数指针、回调、拥有关系和任意重载方法。遇到已标注但不支持的声明必须报错，不能静默跳过。未标注依赖可以正常解析，不要求生成。

资源引用、Actor 引用和后续集合通过显式类型适配器扩展。组件引用不能被生成器直接视为可长期保存的裸指针。

## 5. 分阶段实施

### R0：冻结协议与打通真实头文件分析

任务：

1. 确认本机与 CI 可用的 LLVM/Clang 版本、SDK 与 MSVC 头文件，固定生成器工具链版本。
2. 从 Premake 的已解析目标配置导出分析参数，覆盖 include、宏、目标三元组、C++20、MSVC 兼容选项与 Windows SDK；区分 Debug / Release / Publish。
3. 建立模块扫描清单和反射专用翻译单元，只纳入第一版目标头文件。避免为每个消费者源文件重复分析同一组声明。
4. 对 FVector3、CPhysicalBox、CTransform 的真实头文件运行语法分析；直接报告依赖或兼容错误，不伪造第三方类型来绕过错误。
5. 定义 Schema 版本、默认名称推导、可选 Id、legacy alias、类外查询及生成目录契约。

交付：可构建的 OvReflectGen 骨架、固定工具链说明、参数导出与三个真实类型的分析报告。

验收：全新输出目录可完成分析；每个配置参数可追溯；分析失败返回非零退出码与源码位置。此阶段不改变引擎运行行为。

### R1：标注提取、语义模型和诊断

任务：

1. 添加轻量标注头，定义统一 OV_TYPE 与成员标注的首版语法。按 AST 声明类别分派，校验标注对象及各类别允许的策略；例如枚举不得使用 Component。
2. 使用 Clang AST 提取显式标注的类型、枚举、基类、字段及方法签名。保留 canonical 类型和展示名称，避免把 typedef 文本误当独立底层类型。
3. 校验默认/显式 ID 唯一性、属性名称推导、属性配对、访问权限、继承冲突和类型支持范围。
4. 输出排序稳定的 schema.json，保留源位置用于诊断，但不要把绝对机器路径写进持久化身份。
5. 收集实际 include 依赖及输入摘要，作为后续增量判断依据。

交付：可独立验证的 Schema 和正反例夹具。

验收：同样输入产生同样语义输出；覆盖 class / struct / enum / enum class 上的 OV_TYPE() 空参数、类别专属策略合法性、嵌套命名空间、同名不同命名空间、显式 Id 和历史别名。非法 setter、重复 ID、不支持类型、非法私有访问均明确失败；重复包含同一声明不会重复注册。

### R2：最小反射运行时和 C++ 生成

任务：

1. 添加 OvReflection 的 TypeDesc、PropertyDesc、类外 Traits / TypeOf<T>()、显式注册入口与初始化后只读的注册表。
2. 生成强类型访问器和受控的类型擦除适配；访问前验证类型与访问权限，不依赖未经检查的 void* 转换。
3. 为数据成员生成合法成员访问，为逻辑属性生成 getter/setter 调用，为继承关系生成合法 C++ 转换。私有字段要求显式 friend 授权给该类型的访问器特化；固定前置声明不依赖生成头，生成的访问器只处理已标注成员。
4. 提供稳定的模块聚合输出；生成代码由现有目标编译器编译。
5. 显式调用 RegisterOvCoreTypes 等模块注册函数，确保静态库链接保留生成描述。

交付：基础值类型、枚举和 CPhysicalBox.Size 的可查询描述与访问入口。

验收：Debug / Release / Publish 编译通过；读取与设置 Size 经过现有方法；错误类型、只读写入、重复注册被拒绝。覆盖现有默认渲染配置及开启 RenderThread 时的 Main 线程访问规则。

运行时约束：只读元数据可共享，元数据中的写操作不会因此成为线程安全；首版组件 getter/setter 仍在 Main 执行。不把对象裸指针缓存为可跨帧或跨场景访问的反射句柄。

### R3：正式构建与增量生成

任务：

1. 在 Premake 项目依赖图中加入独立生成目标，保证生成完成后才编译消费者。不能只依赖 gen_proj，也不能让每个消费者同时改写同一输出。
2. 将间接头文件、宏参数、目标配置、生成器版本、Schema 版本和扫描清单纳入失效规则。
3. 使用配置隔离、单写者、临时输出与原子发布；失败时使当前构建失败，不继续使用旧结果。
4. 内容不变不改输出时间戳；仅清理 manifest 所属生成目录内的陈旧文件。
5. 验证业务类型头文件、扫描翻译单元在没有任何生成产物时仍可解析；类型查询消费者依赖生成任务。工具自身构建不依赖引擎生成产物，解决首次构建依赖环。

交付：可在完整解决方案中使用的增量生成链路。

验收矩阵：干净构建、无修改重建、直接/间接头修改、宏修改、文件新增/删除/重命名、生成错误、并行项目构建、不同配置并行构建。无变化时不得引起无关源码重编译。记录生成耗时与重编译范围，不预设未经测量的性能结论。

### R4：组件注册迁移

任务：

1. 为现有原生组件添加 OV_TYPE(Component, ...) 标注，由 AST 提取名称；仅按需要填写显式 Id 或历史别名，不要求同时迁移其全部属性。
2. 从元数据生成反序列化查找与组件工厂适配，调用现有 Actor::AddComponent<T>()。
3. Transform 使用取得已有实例的专用策略；抽象/不可添加类型不得进入普通添加菜单。
4. Inspector 从组件注册表构建列表，保留原显示名称、顺序、添加限制和事件行为。
5. 原 ComponentTraits 字符串作为 legacy alias；过渡期继续写兼容格式，避免同时改变注册系统与存档格式。

交付：组件注册信息只有一个主来源，替换 Actor 类型判断链和 Inspector 重复列表。

验收：旧场景/Prefab 加载、组件添加删除、Transform 唯一性、继承类型、事件和生命周期顺序一致。未知类型须保留原始存档数据或明确阻止有损保存，不能悄悄丢失。

### R5：Inspector 与序列化试点

任务：

1. 实现基础类型/枚举的通用属性 Drawer，以及与现有 XML Serializer 的适配层。
2. 先迁移 CPhysicalBox.Size：保留基类序列化与 Inspector，继续通过 SetSize 修改物理对象。
3. 再迁移 CTransform 的属性描述：rotation 存档保持四元数，Inspector 保持欧拉角，反序列化仍走整体 TRS 恢复钩子。
4. 显式区分缺失值、默认值、只读值与失败结果；默认值不能依靠执行任意组件构造函数来推导。
5. 提供自定义 Drawer / Serialize / Deserialize 钩子，复杂组件暂保留手写实现。元数据只指定定制器标识，不直接依赖 UI 类型。
6. 检查面板刷新、目标销毁和场景替换，防止 UI 回调继续操作失效对象。

交付：两个代表组件使用反射驱动的公共能力，旧 XML 语义保持兼容。

验收：保存后读取属性等价、旧格式加载、缺失字段策略、物理形状更新、Transform 层级矩阵更新、旋转编辑与显示、Prefab 恢复、Play/Stop 和场景切换。需要比较有意义的结果，不能只验证反射表中存在预期字符串。

R5 完成即为第一轮完成；不自动要求把所有组件迁移到通用序列化。

### R6：C# 绑定生成扩展（已完成本地验收）

前置：R0–R5 完成，且 CoreCLR 桥接已明确句柄、生命周期、ABI、异常与 Main 线程协议。R6 已在 Tools/OvReflectGen/Schema/SCRIPT_ABI.md 固定并实现这一调用边界；不把引擎内运行时宿主和 Behaviour 调度并入绑定生成阶段。

任务：

1. 为 Script 标注的属性和方法生成 C ABI 函数表及 C# 包装代码，未标注声明不导出。
2. 支持基础值、枚举、显式布局的数学桥接结构和对象句柄；字符串定义编码、长度及释放策略。
3. 显式指定重载、引用返回值复制策略、可空性与资源所有权；复杂容器/回调逐个扩展。
4. 生成 ABI 版本与签名摘要，加载阶段检测原生/托管产物不匹配。
5. 只生成原生 API 包装；用户 C# Behaviour 的元数据由加载期反射或后续 Roslyn 生成器处理。

验收：产物匹配校验、对象销毁后拒绝访问、线程检查、异常转换、数值/字符串往返，以及批量与高频接口的实际开销。禁止在常驻注册表里保留可卸载脚本程序集的旧委托。

## 6. 迁移与风险控制

- 每阶段独立可验收；改动之前保留旧场景/Prefab 作为兼容夹具。
- 类型注册迁移与存档格式升级分开，不同时修改所有组件。
- 通用能力采用按类型/属性接入；复杂渲染、物理、动画与资源接口保留专用逻辑。
- 不因引入反射改变 Scene 更新顺序、脚本调度、Render owner 或 JobSystem 稳定读写边界。
- 生成器诊断必须指向用户标注/声明；生成代码编译错误应尽量被 Schema 校验提前发现。
- 工具链升级通过固定夹具与真实头文件回归后再切换，不追踪 LLVM main。
- 不承诺自动生成网络复制、通用深拷贝、任意对象图存档或跨语言所有权；这些都需要独立协议。

## 7. 第一轮完成标准

1. 干净检出可按文档完成生成与引擎构建，发布运行不需要 LLVM。
2. 标注、Schema、生成代码和运行时模块职责分离；空参数类型标注可用，无必填包名/类型 ID。
3. 不要求 GENERATED_BODY 或业务头文件包含生成头，公开属性与授权私有字段均能通过类外生成代码访问。
4. 不支持的声明、冲突和构建环境不匹配明确失败。
5. 组件注册与编辑器添加列表由共同元数据驱动，旧资源兼容。
6. PhysicalBox 与 Transform 试点保留原有可观察行为。
7. 增量/并行构建可靠，输出稳定，不意外触发大范围重编译。
8. 有代表性的行为回归和构建验收记录；没有仅凭生成成功声称功能等价。

## 8. 执行顺序

第一轮：R0 → R1 → R2 → R3 → R4 → R5。
后续扩展 R6 已按用户明确请求完成；当前停在 R6。

最先实施 R0：建立独立分析器、对齐实际 Premake 编译环境、让三个真实头文件通过分析。暂不改组件注册、场景格式或 Lua 绑定。

## 9. 技术依据

- Clang 工具接口选择：https://clang.llvm.org/docs/Tooling.html
- LibTooling：https://clang.llvm.org/docs/LibTooling.html
- 编译数据库格式：https://clang.llvm.org/docs/JSONCompilationDatabase.html
- annotate 属性：https://clang.llvm.org/docs/AttributeReference.html
- MSVC 兼容性：https://clang.llvm.org/docs/MSVCCompatibility.html
- 后续 Roslyn 增量生成器：https://github.com/dotnet/roslyn/blob/main/docs/features/incremental-generators.md
