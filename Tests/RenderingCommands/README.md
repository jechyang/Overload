# 同线程几何命令回归（阶段 2 首增量）

复用蒙皮测试的 Windows/VS2022 编译、链接和隐藏窗口设施：

```bat
node Tests\Skinning\run-validation.mjs Debug --geometry
node Tests\Skinning\run-validation.mjs Release --geometry
```

先构建对应配置的完整解决方案；必要时添加 `--vs "C:/Program Files/Microsoft Visual Studio/2022/Community"`。输出为 `Build/JobsValidation/Skinning/<配置>-geometry/{baseline,current}/`。

两份独立程序仅替换 ABaseRenderer.cpp：baseline 从阶段 1 提交 `4f8da08b854a7d57e3a2b0ce08f3d8a05039d7d9` 读取，current 取工作树。两者使用当前头文件（新增嵌套类型/方法不改变对象布局）、当前蒙皮与 Scene 源码、同配置其他引擎库。旧路径没有新命令调用。原蒙皮测试的 baseline 仍然固定为原始提交，未被此模式替换。

直接链接 ABaseRenderer.cpp 会把其中遗留的静态默认相机构造提前到库初始化之前；两份测试均先编译/链接当前 FVector3、FQuaternion、FMatrix3、FMatrix4 源码，保证其所需数学常量先初始化。没有改写旧渲染器源码或关闭 Debug 检查。测试另将引擎断言输出到 stderr 后终止，避免进入 Windows JIT 调试流程。

- 新路径在创建 GL 上下文之前准备 64 种 stateMask 命令。mesh 的全部虚函数都抛错，确认准备阶段不绑定/读取 GPU 网格；修改并销毁 Drawable/Material 后检查捕获值、原 pass 状态与借用 mesh 身份。缺失 mesh/material、零/负实例数必须拒绝。
- 在真实隐藏 GL 上下文中执行 64 种状态组合 × 4 种 index/instance 分支，交替 TRIANGLES/LINES/POINTS 并修改共享材质 uniform。非索引分支通过 IMesh 复用有效 VAO，避免触发现有 Mesh::Upload 对空 index buffer 的限制。拦截实际 GL 绘制入口、记录顺序/参数/关键管线状态，再转发给真实驱动；检查四分支各出现 64 次、无 GL 错误、画面不是全部空白。
- `draws.bin` 的 256 条记录和 `geometry-frames.bin` 的 256 张 64×64 RGBA 在旧/新程序间逐字节一致。记录不包含不稳定的 GPU 句柄，也不声称捕获了所有 GL 状态。
- 复用真实 Mannequin/Standard/SceneRenderer：`frames.bin` 的 24 帧 320×240 RGBA 完全一致，动画图像变化且绘制不推进姿态版本。

同线程集成检查另运行 `Debug/Release --editor`，验收真实新建项目、共享资源、多视图消费顺序、暂停/逐帧/恢复/切场景。此检查是新路径的集成回归，并非旧编辑器与新编辑器逐帧对拍。

该增量仍同步消费借用 mesh，shader/texture/uniform/palette/viewport/target 由外层原调用链设置。测试没有在源 mesh 销毁后执行命令，也不支持跨帧排队、后台录制或 Render 线程。详细所有权审查与剩余工作见根目录 `MULTITHREADING_RENDER_COMMAND_VALIDATION.md`。

## 材质绑定与 uniform 命令（阶段 2 第二增量）

```bat
node Tests\Skinning\run-validation.mjs Debug --material
node Tests\Skinning\run-validation.mjs Release --material
```

输出为 `<配置>-material/{baseline,current}/`。旧 ABaseRenderer/Material 源码取自 `50194bfe7ee94eac37d63bbf84779c7c68b390e4`，当前取工作树；**旧材质副本只将未初始化的 MaterialSignatureSet seed 归零**，使其成为有确定缓存语义的对照。原几何/蒙皮模式的基准不变。测试脚本开始时清除该模式的旧 result.json，失败重跑不会留下过期成功标记。

- 42 组材质事务：缓存命中、stable/single-use 分离、其他材质切换、pass/feature 覆盖和回退、当前变体缺失 uniform、bool/int/float/vec2/vec3/vec4/mat3/mat4、2D/cube sampler、纹理资产/裸 GPU 纹理/null/fallback。
- 拦截并转发真实 GL 绑定及 uniform 上传函数，记录 `material-trace.bin`；按明确字段记录 GPU uniform 读回与 CPU 属性状态到 `material-state.bin`。两份文件必须逐字节一致，GPU 资源 ID 映射为测试逻辑标签，不比较地址或签名数值。
- 检查上传筛选和中途错误的前缀消费顺序。新路径检查准备不产生 GL 调用、不消费属性；销毁源 Material 后，用仍存活的 shader/texture 执行已复制参数，检查矩阵和纹理解析结果。
- 随后执行原几何 256 组和真实蒙皮 24 帧对拍；真实编辑器模式/多视图/切场景继续使用 `--editor`。

独占材质微基准：完整回归编译后，在 `Build/<配置>` 工作目录运行对应 baseline/current 的 `skinning-validation.exe <另建输出目录> --benchmark`。先完成材质自检，再关闭所有 GL 拦截；预热 32 组，测量 256×32 次 SetProperty/Bind/10 uniform 上传/Unbind，输出中位数与 p95。需串行交替跑旧/新程序，不能与构建、编辑器回归或其他 Tracy 客户端同时运行；这不是完整编辑器帧率。

## 材质快照与资源代际（阶段 2 第三增量）

~~~bat
node Tests\Skinning\run-validation.mjs Debug --snapshots
node Tests\Skinning\run-validation.mjs Release --snapshots
~~~

先完整重建对应配置：Shader/Texture 包装布局已改变，不能混入旧 loader 或消费者对象文件。可用已有 --vs 参数指定安装目录。输出为 <配置>-snapshots/{baseline,current}/。

旧 Material/ABaseRenderer 源码取自 8cc2f8c5a9d992809b95d317a70dfbcfc3928442；两程序均编译当前 Shader/Texture.cpp 并链接当前其余库，以保持资源表示一致。原材质/几何/蒙皮文件逐字节对拍；新 Snapshot API 只在 current 运行，并在内部与真实即时全量上传对照。此模式不改原 --material/--geometry/蒙皮模式基准。

- 32 组完整材质快照：支持的全部数值类型、2D/cube、资产/显式裸纹理持有/fallback、pass/feature 与缺失回退。录制不产生 GL 上传；消费结果与即时路径全量上传一致；销毁源 Material、污染原 program 全部参数、改绑别的 program 后，回放 GL 调用和 GPU 数值仍与参考一致。
- 8 轮成功重载/资产包装销毁/资源回收，旧新代际交替回放 64 张 32×32 RGBA（snapshot-frames.bin），逐像素等于各自的即时参考图。弱引用及旧 GL program/texture 对象检查验证最终回收，另验证 shader 编译失败不发布代际。
- 类型、未持有纹理、无 fallback 的空 sampler、unsupported 类型和分配失败均在录制时拒绝，不消费 singleUse 属性；移动后空快照拒绝回放。

快照持有整个 shader variant 代际和实际 GPU 纹理，尚不包含 geometry、transform/palette、view/pass/target 或 feature buffers。获取、录制、回放和最终释放仍在 GL owner；回到即时 renderer 必须使其签名缓存失效，不得夹入尚未结束的 Bind/UploadProperties 调用段。默认 renderer 尚未采用完整快照消费，--editor 是新资源表示及原即时链路的集成回归。详细协议、成本和最终证据见 MULTITHREADING_MATERIAL_SNAPSHOT_VALIDATION.md。

## 几何持有与模型/palette 上传（阶段 2 第四增量）

~~~bat
node Tests\Skinning\run-validation.mjs Debug --draw-inputs
node Tests\Skinning\run-validation.mjs Release --draw-inputs
~~~

先完整重建，Mesh 内部布局已改变；VS 路径可用 --vs 指定。输出 <配置>-draw-inputs/{baseline,current}/。旧 renderer/material/EngineBufferRenderFeature/SkinningRenderFeature 源码固定为 b1128a77b55b9d96c554aad63e743056e481f2cf，两程序均使用当前 Mesh/Shader/Texture 内部表示。旧 renderer 编译时使用匹配的 ABaseRenderer.h 嵌套命令定义；此兼容处理也用于以前的 geometry/material/snapshots 模式，旧基准提交保持不变。

24 帧/72 次 DrawEntity 的实际缓冲分配、上传内容、绑定顺序以及像素精确比较。当前路径另组合几何、材质、模型/palette 命令，销毁源 Mesh/Material/Drawable/矩阵数组后覆盖 GPU 状态再回放 32 帧，并检查最终 VAO/VBO/EBO 回收和即时 palette 缓存恢复。另检查无 palette 的 identity、禁用 skinning 的 no-op、无模型描述符和自定义 Mesh 子类；保留原材质/几何/蒙皮及材质资源生命周期回归。

需要检查新增上传成本时，在 Build/<配置> 工作目录串行交替运行 baseline/current 的 skinning-validation.exe <另建输出目录> --benchmark。用 45 骨骼、两个稳定数组交替制造缓存 miss，32 次预热，256×32 次实际 palette 上传；计时期间关闭 GL 拦截，不能与构建或其它验证并发。它衡量 CPU/驱动上传开销，不代表整帧或 GPU 渲染时间。

独立几何持有仅适用于标准 Mesh，自定义 IMesh/派生 Mesh 在生产几何命令中仍借用。模型/palette 命令仍依赖执行 feature 的 UBO/SSBO 及外部 view/pass/target 状态，材质快照尚未统一接入完整 draw。详见 MULTITHREADING_DRAW_INPUT_VALIDATION.md。

## 同次 draw packet（阶段 2 第五增量）

~~~bat
node Tests\Skinning\run-validation.mjs Debug --draw-packets
node Tests\Skinning\run-validation.mjs Release --draw-packets
node Tests\Skinning\run-validation.mjs Debug --editor --draw-packets
node Tests\Skinning\run-validation.mjs Release --editor --draw-packets
~~~

先完整重建对应配置（CompositeRenderer 布局和 feature 虚表已变）；可追加 --vs 参数。输出为 <配置>-draw-packets/current/ 和 <配置>-draw-packets-editor/current/，与旧模式隔离。--draw-packets 仅可与 --editor 组合，不可与旧基准/ASan/parallel 模式混用。

packet_validation.cpp 在真实 CompositeRenderer 上切换即时/录制：12 组反射常量/cubemap、定向光深度/矩阵、模型/palette、singleUse 缓存，逐字节比较图像及实际 GL draw 的顺序/管线/模型/tint；检查未知 feature、已批准 feature 的派生类、自定义 Mesh、未持有 raw sampler 拒绝和下一帧恢复、附件最终回收。随后比较 24 帧真实 Mannequin/Standard/SceneRenderer 图像。生成纹理的测试只放宽 CReflectionProbe 私有字段访问权限，不改变布局或生产路径。

--editor --draw-packets 将真实 Scene/Game/Asset 三个 renderer 全部开启录制，保留自动曝光/Bloom 后处理，执行既有项目、模式、同视图消费顺序和切场景/resize 验收。默认产品模式仍关闭录制。

为了让早期旧源码基准链接当前 CompositeRenderer/后处理，兼容副本补充缺失的新材质辅助函数和 BlitWithRetainedTextures 非虚接口；旧 Bind/UploadProperties/DrawEntity 保留，基准提交不变。基类新增声明不改变旧实例/虚表布局，几何嵌套定义仍匹配基准。

本阶段 packet 只在同次 draw 同步消费，资源持有不冻结动态纹理，也未持有完整 view/pass/target/lighting 状态。详细失败协议、证据和后续工作见 MULTITHREADING_DRAW_PACKET_VALIDATION.md。

## 相机/时间与灯光输入（阶段 2 第六增量）

~~~bat
node Tests\Skinning\run-validation.mjs Debug --frame-inputs
node Tests\Skinning\run-validation.mjs Release --frame-inputs
node Tests\Skinning\run-validation.mjs Debug --editor --frame-inputs
node Tests\Skinning\run-validation.mjs Release --editor --frame-inputs
~~~

先完整重建 EngineBufferRenderFeature / LightingRenderFeature 的消费者，必要时添加 --vs。输出 <配置>-frame-inputs/current/ 与 <配置>-frame-inputs-editor/current/；该模式包含 packet 回归，不与旧基准或并行/ASan 模式混用。

frame_input_validation.cpp 在创建 GL 上下文前准备相机/时间与灯光载荷；销毁源 Camera/Transform/Light/Frustum 后检查 GPU 字节，覆盖 UBO 上传范围与时间保留、灯光 frustum/空集合/分配失败、buffer 脱离 feature 的存活与最终释放。24 组相机切换场景对比即时与录制图像；录制分支故意污染相机/时间 UBO 和灯光绑定，确认 draw 还原捕获值并保留显式灯光覆盖。另运行原 packet 与真实 Mannequin 图像对拍。

--editor --frame-inputs 在实际三视图开启录制，保留调试假灯光与后处理，运行既有模式/多视图/切场景/resize。LightingDescriptor 仍有 shadow 等帧内借用者，LightBindingCommand 不冻结 buffer 内容，详见 MULTITHREADING_FRAME_INPUT_VALIDATION.md。

## 目标、viewport 与 clear（阶段 2 第七增量）

使用 Debug/Release --target-state 及 --editor --target-state（可追加 --vs）。先完整重建 baregl 和 renderer 使用方。新测试包含无当前 GL 上下文捕获、旧目标 resize/销毁后清理、cubemap 六面读写选择、共享附件/采样状态、最终回收及真实 draw 目标/viewport 污染恢复对拍，并复用 frame-inputs/packet/Mannequin 回归。Editor 新增 UI 纹理 ID 与当前附件一致性断言。

为了施加 Driver 缓存能识别的管线状态，专项测试仅放宽 Driver 私有访问并同时编译当前 Driver.cpp；不改变生产 ABI。历史 renderer 副本追加当前 PassStateCommand 成员以匹配实例布局，旧嵌套几何类型、虚表和即时绘制逻辑不变。

目标快照只固定配置/存储身份，不冻结像素；当前仍同线程消费。协议、测试入口和证据见 MULTITHREADING_TARGET_STATE_VALIDATION.md。
