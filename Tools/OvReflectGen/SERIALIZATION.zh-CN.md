# 注解驱动的组件序列化

C++ 使用 Clang LibTooling 的语义 AST，C# 使用 SDK 自带 Roslyn 的增量源码生成器。
两者生成属性描述和合法的读写代码；运行时共享 XML 编解码与 Inspector 绑定。
它们不读取对象内存偏移，也不会把 Clang 或 Roslyn 带入游戏运行时。

## C++

组件在 `OV_TYPE` 中声明注册身份和创建方式，将头文件加入所属模块的
`scan-modules.json`。属性可以声明在成员字段或者已有 getter/setter 上：

```cpp
OV_PROPERTY(Setter = SetIntensity, Name = "intensity", Serialize, Inspect,
            Thread = Main, DisplayName = "Intensity", Step = 0.005, Order = 1)
float GetIntensity() const;
```

`Serialize` 进入场景保存和加载；`Inspect` 进入默认 Inspector；`Script`
另外生成供托管代码使用的原生 API。私有成员需要显式授权
`friend struct OvReflection::Access<YourComponent>;`。

`AComponent` 提供默认的 `OnSerialize`、`OnDeserialize` 和 `OnInspector`，
因此常规组件无需重复实现这些方法。默认入口通过生成的组件注册表借用确切
类型，包含继承属性，并检查主线程。Inspector 回调还检查组件的存活凭证。
保留自定义 `OnInspector` 可以继续使用颜色选择器、衰减预设、投影联动等 UI。

本次迁入统一入口的组件为 Transform、Camera、物理 Box/Sphere/Capsule、
Point/Spot/Directional/Ambient Box/Ambient Sphere Light，以及其物理、灯光基类。
Model Renderer、Audio Source、Material Renderer 和 Skinned Mesh Renderer
也使用默认入口，不再逐字段手写 XML。
旧 XML 键名保留，基类属性仅写一次。加载会先解析并验证全部属性，再调用
setter；非法输入不会先改动前面的字段。

缺少属性时默认保留当前值。历史上缺省为零的属性显式使用
`Missing = Default, Default = "0"`（向量例如 `"0,0,0"`）。Camera 的
`projection_mode` 缺失时保留投影模式。Transform 使用已有 `TransformTRS`
自定义加载钩子一次性应用 TRS，保留旧场景的零缩放行为。
`Id`/`Alias` 可保留属性改名的兼容关系，详细约束见 [协议](Schema/PROTOCOL.md)。

资源使用有类型的 `Resources::ResourceReference<T>` 值适配：

```cpp
OV_PROPERTY(Serialize, Name = "model", Setter = SetSerializedModel,
            Missing = Default, Default = "?", Order = 0)
Resources::ResourceReference<OvRendering::Resources::Model> GetSerializedModel() const;
```

Clang 校验实际模板类型；生成器按属性类型生成编解码适配表。支持
Model、Material、Sound、Texture、Shader 引用以及容量 1–4096 的
`std::array<ResourceReference<T>, N>`。数组必须指定 `ItemName = "material"`
这样的子元素名；`std::vector`、裸指针及其他类型不会被默默当成资源。
资源值目前只允许 `Serialize`；Inspector 使用带组件存活凭证和主线程检查的
`DrawResourceReference<T>`，沿用已有选择器和拖放操作。

XML 仍保存资源路径，空引用写 `?`。找不到资源时保留路径，重载场景可以
再次解析；显式清除引用才删除路径。材质槽保留空位、顺序和最后一个非空
槽之前的全部项，不依赖当前模型；缺少列表保留原值，空列表清除全部槽。
超出 255 槽、错误子元素、非法字段值都会中止整批加载。所有 XML 先验证，
之后统一通过资源管理器解析，最后才写入组件。路径输入限制为 4096 字节。
资源管理器继续负责所有权和渲染线程资源调度。

动画的播放、循环、速度、包围盒倍率、求值频率、时间、资源和片段名都由
注解生成。`SkinnedMeshState` 加载钩子在属性恢复后统一规范化并重建骨骼
绑定，避免每恢复一个字段就重建状态。

后处理的 Auto Exposure、Bloom、Tonemapping、FXAA 设置，以及原生 UI 的
RectLayout，使用 `OV_TYPE(Record)` 描述嵌套值。Record 必须是可复制的聚合，
其全部实例字段声明公共、可写的 `OV_FIELD`；继承基类也必须是 Record。
不支持 Record 内的注解 getter/method、组件钩子或 Script 值签名。

```cpp
struct OV_TYPE(Record) BloomSettings : EffectSettings
{
    OV_FIELD(Serialize, Inspect, Step = 0.1, Order = 1) float intensity = 1;
};
OV_PROPERTY(Serialize, Inspect, Flatten, Prefix = "bloom_",
            Setter = SetBloomSettings, DisplayName = "Bloom")
const BloomSettings& GetBloomSettings() const;
```

常规 Record 保存为嵌套 XML；`Flatten` 将字段写入父节点，`Prefix` 保留
`bloom_intensity` 这样的旧键。别名声明在 Record 叶字段上，前缀同时用于
正式键和别名。Clang 会拒绝继承、平铺字段和别名的实际 XML 键冲突。
自动曝光兼容历史保存的 `auto_exposure_luminance_min/max` 和旧读取代码使用的
`auto_exposure_min/max_luminance`，修复曝光上下限无法正确重新加载的问题。

Record 解析先修改拥有所有权的副本；全部嵌套字段验证通过后才解析资源并
调用组件 setter。Vec2/Vec4 沿用旧 XML 坐标节点，缺失坐标保留原值。
`Validate = "Positive"` 和 `Validate = "EnumChoice"` 显式声明 Canvas 的业务
约束，避免在部分字段已写入后才由 setter 发现非法值；Min/Max 仍只控制 UI。

Inspector 从 Record 的 Inspect 描述生成可折叠分组。子字段编辑每次重新读取
当前聚合，避免外部替换设置后持有悬空成员引用；稳定分组身份保留折叠状态，
搜索临时展开匹配组。复制、重置、多选编辑、撤销和父级只读状态继续生效。
`Drawer = "Color"` 给 Vec4 保留颜色选择器，32 位整数使用拖动输入，64 位
整数继续使用精确文本输入。

RectTransform、Canvas、UI Image、Canvas Group、Rect Mask 2D 已移入默认
序列化入口。RectTransform 保存 authored 布局：活跃布局驱动的临时值不写入
场景；加载更新驱动器保存的用户值，释放驱动时恢复新值。CanvasRenderer 的
托管 Graphic 网格、材质槽、字体页等继续作为临时输出，加载时清除。
Reflection Probe 尚保留自定义编解码。

普通项目构建会先运行 `OvReflectionGen`。手动仅构建某个库时先执行：

```powershell
python Tools/OvReflectGen/reflection.py ensure --config Debug
```

生成物位于 `Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug/<模块>`，
包括可查看的 `schema.json`、`Reflection.generated.h/.cpp`。OvPhysics 和
OvRendering 的反射枚举分别由其所属模块生成、链接与注册。

## C#

```csharp
[SerializeField, FieldId("speed"), FormerlySerializedAs("old-speed"),
 InspectorName("Movement Speed")]
private float velocity = 2;
```

Behaviour 的公共实例字段以及带 `[SerializeField]` 的非公共实例字段进入
编译期描述。静态、常量、只读、`[NonSerialized]` 字段和属性不进入。
不要求类声明为 `partial`，不会调用用户的属性 getter。
生成的 `UnsafeAccessor` 直接读取或写入私有字段；结构体写入使用盒装对象
的引用，不会只改动一份副本。继承字段由声明它的程序集提供描述。

重复的字段 ID/别名，包括继承冲突，以及空白或控制字符身份，在编译时产生
`OVSG001` 错误。可序列化类型与 XML 格式仍由托管宿主的既有编解码器检查，
包含枚举、数学类型、Actor/Asset/Component 引用、数组、List 和 Serializable
记录。记录保持原有公共字段和排序格式。

Editor 的 `BuildGameScripts`/`BuildCSharp` 自动向现有工程注入分析器，新建
工程模板也包含独立构建入口。已有工程如果直接在 IDE 中构建，可导入
`Sources/OvManaged/Serialization.targets`。需要查看生成源码时设置
`EmitCompilerGeneratedFiles=true`，输出放在工程的中间构建目录。
生成器引用固定 SDK 的 Roslyn，无需下载 NuGet 分析器包。

预编译旧程序集继续通过兼容描述加载。无法从生成代码合法访问的私有嵌套
类型或泛型声明会报告 `OVSG002`，并保留兼容发现和字段数据。用于兼容测试
的工程可设置 `OverloadGenerateFieldSchema=false`。托管项目描述只由其会话
持有，不加入进程全局缓存，以允许旧项目在热重载后卸载。

## 验证

```powershell
node Tests/Scripting/Integration/verify_generated_fields.mjs --native
node Tests/Editor/run-panel-validation.mjs
python Tests/Reflection/Integration/verify_resource_schema.py
python Tests/Reflection/Integration/verify_record_schema.py
node Tests/UI/run-layout-validation.mjs --runtime
```

托管字段测试验证真实 Roslyn 生成物、私有和继承字段、稳定身份、结构体、数组、
64 位枚举、编译错误、旧程序集兼容和程序集卸载，并通过真实原生宿主验证
场景/Prefab 保存加载、字段改名、缺失脚本数据保留与 Inspector。
面板测试在普通与独立渲染线程下运行真实 Editor 面板，并覆盖 C++ 组件的 XML
往返、旧字段名、继承去重、缺省策略、失败时不调用 setter 和过期回调，
以及实际模型/音频资源、缺失路径、材质槽边界和动画恢复，
并覆盖后处理 Record、曝光别名、Vec2/Vec4、Canvas 业务校验、驱动布局的
用户状态、嵌套字段操作及折叠/搜索行为。Clang 专项验证检查资源模板类型、
子元素名、数组容量、Record 约束、平铺键冲突和非法注解的诊断。
布局测试验证 C# 合约、原生驱动器、实际场景序列化及热重载，并比较普通与
独立渲染线程的 26 帧实际画面是否逐字节一致。
