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

## 帧命令序列（阶段 2 第八增量）

Debug/Release --command-list 覆盖拥有资源的序列、源对象销毁后消费、录制与执行失败/取消、普通 Mannequin 及动态阴影/反射/曝光/Bloom/Tonemapping/FXAA 对拍。可追加 --vs；先完整重建 renderer/feature/pass 使用方。该增量当时只覆盖 SceneRenderer；第十增量已增加 --editor --command-list，见下文。

动态用例验证自动曝光设置确实关闭渐变，并将即时重复、同次 packet、延迟帧逐字节比较。失败/取消只终止 GPU 命令，不回滚全部 CPU 准备历史；正式帧交接前仍需闭合该协议。完整范围见 MULTITHREADING_COMMAND_LIST_VALIDATION.md。

## 动态历史恢复（阶段 2 第九增量）

--command-list 增加 History 顺序依赖、取消/录制失败/执行异常/未消费析构、跨历史失效传播、即时路径拒绝超车，以及移动/源游标销毁回归。真实动态场景每帧注入失败，开启渐变曝光、ON_DEMAND + ONE_FACE 反射，恢复后的 24 帧与即时基准严格逐字节比较。

先完整重建使用 CReflectionProbe/AutoExposureEffect 的模块。GPU 所有权约束未放宽；后继必须丢弃并重新准备。协议与证据见 MULTITHREADING_TEMPORAL_HISTORY_VALIDATION.md。

## 编辑器整帧与拾取（阶段 2 第十增量）

Debug/Release --editor --command-list 现在运行真正的视图整帧收集与延迟消费。先完整重建对应配置，再通过 --vs 指定 VS2022（若自动发现不可用）。输出与旧 --editor --draw-packets 模式隔离。

editor_command_validation.inl 验证真实 DebugSceneRenderer 的六组调试/拾取即时对拍、源对象销毁、实际 Actor/Gizmo 解析、场景/删除/坐标拒绝、失败帧不发布、零录制读回及 PBO/pack 状态恢复。原 editor_validation.cpp 在新模式下增加每次真实视图的即时/延迟对照；Asset View 固定渐变曝光与反射时间输入。见 MULTITHREADING_EDITOR_COMMAND_VALIDATION.md。

## 统一帧提交与 GPU 回收（阶段 2 第十三增量）

先完整构建，再运行 Debug/Release --render-frame、--game-frame，以及 --editor --command-list --editor-ui；必要时添加 --vs。前两个模式独立运行，分别输出 <配置>-render-frame/current 和 <配置>-game-frame/current。Game 测试链接生产 Game 对象，仅隔离项目目录与隐藏窗口；Editor 模式增加实际三视图加 UI 的整帧图像比较。

测试覆盖整帧完成、GPU fence 保留/回收、失败/取消/析构/移动、嵌套历史及后继无需等待前帧 GPU 回收、实际 UI/场景/目标销毁后的回放。旧 UI 与动态命令回归继续使用 --ui-commands、--command-list。协议、确定性输入与验收范围见 MULTITHREADING_RENDER_FRAME_VALIDATION.md；没有跨线程许可或性能收益断言。

## 产品帧循环与交互（阶段 2 第十四增量）

Debug/Release 使用 --editor --command-list --editor-ui --product-loop，或 --game-frame --product-loop；先完整构建并按需追加 --vs。输出目录在原组合后追加 -product-loop。链接真实 Application/Editor/Game 对象，先完成保存项目和整帧回归，再运行实际 Application.Run、模式/配置切换与准备后退出。

测试覆盖八帧动态模拟/视图对拍、一帧一次 fence、输入清理、重复与帧内切换拒绝、帧完成后延迟操作；Editor 另有对应帧鼠标/readback/Gizmo/材质拖放与场景/目标删除检查。头文件覆盖只放宽测试访问权限；私有方法通过实际 UI 事件触发，不修改生产链接名。详细协议与范围见 MULTITHREADING_PRODUCT_FRAME_VALIDATION.md。

## 固定 Render 线程与有界队列（阶段 3 第一增量）

先重新生成工程并完整构建，再运行 Debug/Release --render-thread（可追加 --vs）。该模式独立运行，输出 <配置>-render-thread/current。隐藏窗口由主线程创建并解除 GL 上下文，RenderThread 在固定线程创建 Driver 并执行资源、拥有型帧与清理。覆盖八组真实 UI 图像对拍、GPU 在途背压、任务容量、关闭唤醒、异常/取消/后继失效、上下文退出；保留 --render-frame 原回归。它尚未启用产品双线程，详细边界和后续工作见 MULTITHREADING_RENDER_THREAD_VALIDATION.md。

## 日志与主线程通知（阶段 3 第二增量）

Debug/Release --log-dispatch 独立运行，覆盖拥有型主线程事件、六生产者日志顺序、history 并发快照、file 写入、队列积压报告和真实 Render 初始化/任务日志。输出 <配置>-log-dispatch/current。--editor --command-list --editor-ui --product-loop 增加真实 Console 在 PreUpdate 分发的检查；--game-frame --product-loop 增加实际 Game 事件泵验证。先完整构建，按需追加 --vs。详见 MULTITHREADING_LOG_DISPATCH_VALIDATION.md。

## 原生资源最后释放（阶段 3 第三增量）

先重新生成工程并完整构建全部使用方，再运行 Debug/Release --resource-lifetime（可追加 --vs），输出 <配置>-resource-lifetime/current。验证七类实际 GL 对象的 32 轮跨线程最后释放、空闲唤醒、帧 storage 保留、停机 ID 失效和旧 CPU 包装晚释放；保留 --render-thread 与 Editor/Game --product-loop 回归。产品线程接入、ResourceTracking 和 ImGui 后端共享 GPU 代际仍需后续完成，见 MULTITHREADING_RESOURCE_LIFETIME_VALIDATION.md。

## 纹理注册表与主线程面板（阶段 3 第四增量）

先完整构建，再运行 Debug/Release --texture-registry（可追加 --vs），输出 <配置>-texture-registry/current。验证固定 Render owner 创建/分配/512 次 resize 与并发稳定查询、storage 回收、legacy 跨线程持有拒绝、域关闭过滤及包装地址复用。Editor --command-list --editor-ui --product-loop 增加真实 TextureDebugger 主线程刷新、后台误用拒绝、注册代际注入与关闭面板释放；临时转移既有唯一 Driver 的上下文，未创建第二个 baregl Context。详见 MULTITHREADING_TEXTURE_REGISTRY_VALIDATION.md。

## ImGui GPU 代际与 loader（阶段 3 第五增量）

重新生成工程并完整构建后运行 Debug/Release --imgui-lifetime（可追加 --vs），输出 <配置>-imgui-lifetime/current。覆盖 32 组真实 UI 精确图像、managed backend/atlas/快照无上下文析构、owner 实际删除、旧快照释放与新后端重叠、编译/链接失败回收/状态恢复和域关闭后的零惰性上传。ImGui 现在复用引擎 GLAD，不独立初始化或关闭 loader。协议、限制及原 UI/Render/产品回归见 MULTITHREADING_IMGUI_LIFETIME_VALIDATION.md。

## Tracy GPU 查询与 TLS 退出（阶段 3 第六增量）

完整构建后运行 Debug/Release --gpu-profiling（可追加 --vs），输出 <配置>-gpu-profiling/current。当前 Driver 测试副本仅插入 profiling 初始化前/后两个回调，安装 GL hook 并注入失败，不改变正常生命周期逻辑或 ABI。验证 8 次主线程/8 次 Render 重启的查询名称匹配、TLS 归零、重复创建/销毁及两类初始化失败；正常产品回归链接未插桩的 Driver。生成的 exe 可接收输出目录和 --capture，等待本机 Tracy 连接并验证尾部时间戳收集。见 MULTITHREADING_GPU_PROFILING_VALIDATION.md。

## 资源调用与 UI 后端分派（阶段 3 第七增量）

重新生成工程并完整构建后运行 Debug/Release --resource-executor（可追加 --vs），输出 <配置>-resource-executor/current。主线程无 GL 上下文，通过显式 ResourceExecutor 同步调用真正的资源类与 UIManager/Canvas；GL hooks 检查固定 owner。覆盖七类资源、上传/读回/resize 旧存储、shader 诊断及跨帧 uniform 指向、UI 像素对拍与源销毁、作用域/异常/关闭。协议及产品接入边界见 MULTITHREADING_RESOURCE_EXECUTOR_VALIDATION.md。

## Game 实际双线程（阶段 3 第八增量）

完整构建后独立运行 Debug/Release --game-thread（可追加 --vs），输出 <配置>-game-thread/current。新建保存项目，比较实际 Application/Game 的主线程与 Render Context 32 帧姿态/场景/UI；验证自然交叠、两帧容量、重载/退出/重建/失败。测试 Window 副本只在 Present 之前插入 backbuffer 读回 hook。生成 exe 可接收输出目录及 --capture，等待 Tracy 连接并验证消费线程 GPU scope。Game.ini 的 render_thread=true 要求 render_commands=true，暂不默认开启；Editor 接入和最终性能/延迟验收仍待完成，见 MULTITHREADING_GAME_THREAD_VALIDATION.md。

## Editor 实际双线程与在途交互（阶段 3 第九增量）

完整构建后独立运行 Debug/Release --editor-thread（可追加 --vs），输出 <配置>-editor-thread-command-list-editor-ui/current。创建保存项目并运行旧 Editor 回归，再比较实际 Application 的同线程/Render Context 16 帧三视图、完整测试画布与姿态。覆盖两帧积压交互、拾取/材质拖放、源视图覆盖、场景替换、四次应用重建、实际 Run 退出及呈现失败。生成 exe 可接收输出目录和 --capture，独占连接 Tracy 检查 owner 查询。固定画布图像 oracle 显式排空；自然交叠单独验证，性能/完整 UI/窗口/输入延迟仍待后续验收。见 MULTITHREADING_EDITOR_THREAD_VALIDATION.md。

## 完整 UI、窗口与在途输入（阶段 3 第十增量）

完整构建后运行 Debug/Release --editor-stress（可追加 --vs），输出 <配置>-editor-stress-command-list-editor-ui/current。先复用双线程项目/图像验收，再运行 96 帧真正完整 Canvas、循环面板/视图/场景/窗口尺寸、五种微小 Bloom 目标对拍和 Render 积压时的产品 Gizmo 输入对照。--game-thread 与 --editor-stress 共用 window_minimize_validation.inl，分别执行六次真实离屏无激活 Minimize/Restore，验证状态和在途帧排空。产品 GL hooks 增加 ImGui BaseVertex 绘制。该压力不是性能基准；范围和后续计量见 MULTITHREADING_WINDOW_UI_VALIDATION.md。

## Tracy Unicode 线程名（阶段 3 第十一增量）

--gpu-profiling 额外运行每配置 224 次真实外部线程名查询，在 C locale 下检查 ASCII、中文、补充平面字符、255 字节边界、超长/空/不存在的线程回退。原 GPU owner/query/TLS 重启与失败检查保持。详见 MULTITHREADING_TRACY_THREAD_NAME_VALIDATION.md。

## 产品帧性能、队列与延迟（阶段 3 第十二增量）

完整构建后独占运行 Debug/Release --game-benchmark 或 --editor-benchmark（可追加 --vs）。不得同时跑另一个引擎、性能测试、编译或 Tracy 客户端。输出分别为 <配置>-game-benchmark/current 和 <配置>-editor-benchmark-command-list-editor-ui/current。每次运行重新生成测试项目；基准包含实际 Application/Update/PostUpdate、场景/视图与 UI，按正序/逆序交替运行即时、同线程命令、双线程命令三种模式。

普通负载每组 384 帧，前 64 帧预热；三次重复。Editor 另对同线程/双线程命令执行真实拾取读回负载，各 224 帧，前 32 帧预热。计时 hook 只存在于当前生产源码的测试副本中，稳态不增加 glFinish；GPU 查询在整批排空后读取。Debug 完整基准可能超过五分钟，入口给性能模式预留十分钟，其他回归仍使用原超时。

~~~text
node Tests/RenderingCommands/analyze-product-benchmark.mjs <输出目录>/product-frame-metrics.csv
node Tests/RenderingCommands/capture-product-benchmark.mjs game
node Tests/RenderingCommands/capture-product-benchmark.mjs editor
tracy-csvexport -u -f Benchmark <capture.tracy> > <events.csv>
node Tests/RenderingCommands/analyze-product-tracy.mjs <events.csv>
~~~

采集先完成项目夹具，再等待 TRACE READY 连接；连接后的计量另存 product-frame-metrics-connected.csv。两个分析器分别验证时序/帧 ID/两帧容量/交叠，以及完整 Tracy case/线程身份/真实 Update 与 Render 的交叠。GPU 原始时间戳用 BigInt 处理。正式未连接数据与连接数据不得混算；GPU 时间线跨度不是忙碌占用率，呈现返回不是屏幕显示，交互观察不是物理输入设备延迟。指标定义、默认配置决定及结果见 MULTITHREADING_FRAME_PERFORMANCE_VALIDATION.md。

## 多 Pass/View 并行命令录制（阶段 4）

完整构建 Debug/Release 后运行：

~~~bat
node Tests/Skinning/run-validation.mjs Release --parallel-recording
node Tests/Skinning/run-validation.mjs Debug --parallel-recording
node Tests/Skinning/run-validation.mjs Release --parallel-recording --asan
node Tests/Skinning/run-validation.mjs Release --parallel-recording --mutex-deque
node Tests/Skinning/run-validation.mjs Release --recording-benchmark
~~~

专项输出在 <配置>-parallel-recording-command-list/current/；ASan 和 MutexDeque 使用独立后缀目录。测试包括原动态完整帧回归、128 draw 门槛、私有槽顺序、worker 无 GL context、部分提交/汇合失败后的排空、跨视图 history/completion、1024 份源销毁材质输入及 8 个 GPU 代际。三模式（旧录制、准备后串行、准备后并行）各 12 帧、每帧 4 pass × 192 draw，逐像素及 GL draw 状态精确对拍。当前测试材质有 27 个 active uniform（24 个额外矩阵），用于检查有足够编码工作的负载；不能代替普通材质的整帧结论。

--recording-benchmark 使用真实 Editor 三视图、64 个动画实例、固定 Render owner，在三种录制方式之间交替运行；每种方式三轮，每轮 192 帧丢弃前 32 帧，共 1440 个统计帧。输出在 Release-recording-benchmark-command-list-editor-ui/current/product-frame-metrics.csv。用 analyze-product-benchmark.mjs 分析该 CSV。--editor-stress 和 --game-thread 的模式配置显式开启 parallel_recording，可用于 Debug/Release 产品生命周期回归。

所有性能运行保持独占，不与构建、其他引擎测试或 Tracy 客户端并发。未连接 Tracy 的专项数据用 analyze-parallel-recording.mjs 分析 parallel-recording-metrics.csv；完成 Release 专项编译后，单独运行 capture-parallel-recording.mjs。随后用 tracy-csvexport.exe -u -f Record 导出 Build/JobsValidation/tracy-parallel-recording-release.tracy，传给 analyze-parallel-recording-tracy.mjs；分析器检查完整 36 组、真实 worker 区间重叠及封口前结束，不使用固定 sleep 作为工作量。

生产项目设置 parallel_recording=true 才启用新录制准备/编码；默认 false，适用于 render_commands=true 下的同线程或固定 Render owner。低层 renderer 通过 SetCommandRecordingJobs 提供池，以 SetParallelCommandRecordingEnabled(false) 保留准备后串行 oracle。历史 material/snapshot/draw-input 基准只补充链接所需新辅助函数及基类新增尾部字段，旧 Bind/Upload/DrawEntity 和基准提交保持不变。详细所有权、失败边界和测量结果见根目录 MULTITHREADING_PARALLEL_RECORDING_VALIDATION.md。


## 渲染架构 A1：静态不透明前端

完整构建 Debug/Release 后分别运行：

~~~bat
node Tests/Skinning/run-validation.mjs Debug --frontend
node Tests/Skinning/run-validation.mjs Release --frontend
node Tests/Skinning/run-validation.mjs Debug --frontend-immediate
node Tests/Skinning/run-validation.mjs Release --frontend-immediate
~~~

可追加 --vs 指定 VS2022。输出独立放在 <配置>-frontend-command-list/current/，链接新构建的生产 RenderFrontend、提取器和录制桥。保持全部历史模式的 baseline 不变；新 manifest 单独记录 architectureBaseline=620ef10fb56c287581bbd55b0c8904ef9cca26b7 和 oracle 类型。旧 SceneRenderer/材质/Feature 实现与 A0 固定源码相同。

专项使用真实 Standard 材质、静态三角形和已声明的受限场景配置。旧拥有型命令路径、新 Prepare 串行、新 Prepare 单 Worker 各 8 帧，精确比较 RGBA、绘制参数/顺序、矩阵、材质 uniform/sampler 内容和关键 GL 状态。覆盖透视/正交、裁剪、稳定等 key、实例、源变更/销毁、目标 resize/销毁、前端销毁后 RenderFrame 回放，以及空视图和未支持输入拒绝。

新 Prepare 与 Record 在无当前 GL 上下文时运行。测试副本只在 Prepare 入口和首次私有 draw 生成后插入观察/故障钩子，用有超时的闩锁验证 Worker 身份、失败/取消、半成品不发布和最后输入在汇合后释放。生产接口没有任意 Worker 回调；另一个测试源副本仅隔离 AppData 日志目录。

--frontend-immediate 使用冻结的旧即时入口作为 oracle，输出到 <配置>-frontend-immediate-command-list/current/。两种入口现在都运行三模式 × none/staged 共 6 个独立冷启动进程，子目录为 cold-<0|1|2>-<none|staged>；每个进程 8 帧，全部像素逐字节一致，staged 的三份非空 draw trace 逐字节一致。result.json 的 frontendComparison 记录数量、路径和严格比较结果，失败不会留下旧成功 manifest。

原首帧 98 像素差异由测试同步回读活引擎 UBO 触发。现在 Draw 前将 GPU 常量复制到独立暂存槽，全部 Draw 后回读，保留原 trace 内容及时间断言。none 不安装 draw hook；其像素必须与 staged 完全一致。测试程序参数为 <输出目录> [0|1|2] [staged|none|live]；live 仅供原回读干扰复现，单模式进程只负责采集，跨进程比较由 runner 完成。手动调用时工作目录仍为 Build/<配置>，输出目录须预先建立。

本轮没有修改生产代码或 A0 oracle，也没有丢弃首帧或放宽容差。A1 完整验收仍保留活跃反射 UBO 绑定核对，详见根目录 RENDERING_ARCHITECTURE_A1_VALIDATION.md；暂不进入 A2。

完成两配置的 --frontend-immediate 后，可运行 node Tests/RenderingCommands/reproduce-frontend-readback.mjs。它分别冷启动三模式的 live 探针，记录与正式 none/staged 的逐帧像素差及 trace 等价性，输出 Build/JobsValidation/A1FirstFrame/diagnostic.json；这只是机器相关的故障复现，不要求其他驱动也出现 98 像素差异，不改正式验收结果。
