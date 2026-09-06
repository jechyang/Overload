# 阶段 2 第四增量：几何持有与模型/蒙皮上传快照

日期：2026-09-06。前置提交：b1128a7（材质快照与 shader/texture 代际持有）。

## 生产路径的变化

标准 Mesh 的 VAO、顶点/索引缓冲及 bounds 移入共享的 GeometryStorage；Mesh::RetainGeometry 返回只读 IMesh 持有句柄。几何一经构造不再更新，Model 重载/销毁 Mesh 包装对象后，已持有的几何继续有效。保持 Mesh 不可复制；其内部布局改变，所有依赖模块必须重建。

ABaseRenderer::PrepareGeometryDraw 已在真实 DrawEntity 路径中持有标准 Mesh 的 GPU 存储，并让执行端引用该存储，而不是源 Mesh。PSO、primitive、instances 仍按值捕获。只对动态类型精确为 Mesh 的对象采用此策略；自定义 IMesh 和 Mesh 子类保留借用，以免绕过它们覆写的 Bind/count 行为。借用分支只允许原单次同步 draw 内使用，不能延迟到源对象销毁之后。

EngineBufferRenderFeature::OnBeforeDraw 接入 PrepareDrawUpload / ExecuteDrawUpload。CPU 载荷按值持有转置后的 modelMatrix 和原样 userMatrix；执行仍按原顺序上传 UBO 首尾两个区域。缺少 EngineDrawableDescriptor 时保持原 no-op 行为。场景解析本来就已按值保存模型矩阵，本增量进一步消除上传端对 Drawable/descriptor 的读取。

SkinningRenderFeature 新增 RecordPalette / ExecutePaletteSnapshot。快照独占复制已经转置的矩阵，不持有组件数组、Drawable、Material 或 poseVersion 借用键。缺少有效 palette 时保留 identity 行为；未请求 skinning 时保留 no-op。记录输入须在复制期间稳定。

真实 OnBeforeDraw 仍按原组件地址/count/poseVersion 决定是否上传和绑定；需上传时先捕获独占 palette，再消费这份载荷。缓存命中不复制或分配 palette。独立 ExecutePaletteSnapshot 则始终上传/绑定捕获数据，并在触碰 GL 前清除原借用缓存，防止插入回放后错误命中旧组件缓存。它与即时路径共用 UploadPalette 执行函数。

## 本增量的边界

- 几何、模型参数和 palette 的新边界均已接入现有生产链路，保持 feature 前处理、材质即时上传、几何执行和后处理顺序。材质快照仍是上一增量的可选接口，生产尚未统一录制完整 draw。
- 新几何持有只覆盖标准 Mesh；用户自定义 IMesh 没有隐式所有权保证。Shader/Texture 重载继续使用上一增量协议。
- 模型/palette 执行仍使用目标 feature 实例拥有的 UBO/SSBO，未捕获 view/projection/camera/time、目标 framebuffer、反射/阴影/lighting buffers。部分缺失描述符按旧行为不覆盖相应状态，并非完整独立帧数据。
- 场景过滤仍使用 SceneDrawableDescriptor 的 Actor 引用；SkinningDrawableDescriptor 在录制之前仍为借用数组。没有把旧 Drawable 浅拷贝作为可跨帧快照，也没有改变 Parse/Filter 与 Scene 更新的稳定期。
- 获取几何持有、GL 执行和最终几何释放均在 GL owner 且早于上下文销毁。独占 CPU 矩阵本身不依赖 GL；生产录制仍同线程。无 Render 线程、跨帧队列、GPU fence 或跨线程删除队列。
- 几何持有增加共享控制块及引用计数；palette 上传 miss 增加独占数组复制，暂不对外发布可变缓存。图像等价不等于整体性能提升，性能测量单列。
- 资源/分配失败沿现有异常路径传播；palette CPU 复制失败前不发出 GL 上传，不更新原上传缓存。不是 GPU 写入失败回滚协议。

## 验收入口与基准

先完整构建对应配置，再运行：

~~~bat
node Tests\Skinning\run-validation.mjs Debug --draw-inputs --vs "C:/Program Files/Microsoft Visual Studio/2022/Community"
node Tests\Skinning\run-validation.mjs Release --draw-inputs --vs "C:/Program Files/Microsoft Visual Studio/2022/Community"
~~~

输出 Build/JobsValidation/Skinning/<配置>-draw-inputs/{baseline,current}/。baseline 的 ABaseRenderer、Material、EngineBufferRenderFeature、SkinningRenderFeature 源码固定为 b1128a77b55b9d96c554aad63e743056e481f2cf，current 为工作树。两份都使用当前 Mesh/Shader/Texture 内部表示和当前其他模块，因而旧/新对拍验证提交/上传行为，不是旧 Mesh 布局二进制的对拍。

ABaseRenderer 的嵌套 GeometryDrawCommand 布局发生变化，baseline 使用该提交对应的 ABaseRenderer.h 覆盖头；renderer 实例字段仍按相同当前 Mesh 布局编译。此匹配也用于旧 --geometry/--material/--snapshots 模式，保持原提交基准。生产源码没有测试宏。

新增隐藏 GL 测试覆盖：

- 24 帧、72 次真实 DrawEntity，model/user UBO 上传、palette 分配/上传/绑定序列及 RGBA 在旧/新程序间精确比较；交替静态、identity、有效 palette、重复缓存命中和 poseVersion 变化。
- 当前程序额外执行 8 轮源 Mesh、Material、Drawable 及 palette 数组销毁；组合已持有的几何、材质、模型与 palette 命令，在 GPU 参数被覆盖后回放 32 帧，并与销毁前参考图逐像素比较。
- 每轮检查最后一份几何持有释放后的 weak_ptr、VAO/VBO/EBO 回收；录制不发出缓冲调用、palette 分配失败在 GL 前结束；快照回放后相同组件地址/版本的即时上传能恢复正确内容。
- 保留上一增量的 32 组材质快照、8 轮 shader/texture 重载销毁、64 帧代际交替回放，以及原 42 组材质、256 组几何、24 帧 Mannequin/Standard 对拍。
- 真实 Editor 三场景、模式切换、多视图和延迟切场景使用 --editor。

## 最终结果

Debug/Release 均通过：

| 检查 | 每配置结果 |
|---|---|
| 新增 UBO/SSBO 真实调用顺序及上传内容 | 16,860 字节精确一致 |
| 新增 24 帧 draw 输入 RGBA | 98,304 字节精确一致，全部可见 |
| 8 轮销毁源数据后的 32 帧 draw 回放 | 131,072 字节与即时参考逐像素一致，VAO/VBO/EBO 最终回收 |
| 原材质状态 / GL trace | 45,003 / 11,186 字节精确一致 |
| 原几何 draw / RGBA | 19,456 / 4,194,304 字节精确一致 |
| 24 帧 Mannequin/Standard | 7,372,800 字节精确一致，22 次画面变化 |
| 材质快照及 shader/texture 代际 | 32 组参数对照、8 轮重载销毁、64 帧回放通过 |
| 完整 Debug/Release 解决方案 | 各退出 0，0 警告、0 错误 |
| 真实 Editor | Edit/Play/Pause/8 次单步/恢复/Stop；24 组视图顺序图像一致、11 次变化、8 轮延迟切场景/resize 通过 |
| 程序及资源复制 | 两配置共 10 项逐字节一致 |

Editor 三场景统计不变：96 次 Update，crowd 的 6,144 次评估全部并行；fragmented 的 4,608 次评估串行；rate_limited 的 1,536 次评估进入 24 批次。额外运行原 Release --geometry，使用阶段 1 基准头/源码，旧入口仍通过全部对拍。

独占微基准先关闭所有 GL 拦截，在同配置的旧/新/新/旧顺序运行；两个稳定的 45 骨骼数组交替制造 miss，预热 32 组、测量 256×32 次 SkinningRenderFeature::OnBeforeDraw，无并发构建/测试、未连接 Tracy：

| 配置 | 旧路径中位数（两次） | 新路径中位数（两次） |
|---|---|---|
| Release | 0.463 / 0.456 μs | 0.659 / 0.659 μs |
| Debug | 1.241 / 1.925 μs | 2.256 / 2.378 μs |

Release 每次实际上传约增加 0.2 μs；Debug 有更大的复制/分配成本与测量波动。命中路径不新增 palette 复制。此数据是 CPU/驱动上传成本，不含实际 GPU draw，也不是整帧加速结果；后续聚合 frame packet 时再依据整体测量考虑复用独占容量或 arena，不共享可变数组。

最终证据在 Build/JobsValidation：

- 构建：msbuild-{debug,release}-draw-inputs-stage2.log；之后仅调整接口注释及测试/文档，生产执行逻辑未再修改。
- 最终对拍：draw-inputs-{debug,release}-final.log；各 <配置>-draw-inputs/result.json 为成功标记。
- Editor：draw-inputs-editor-{debug,release}.log；旧入口：draw-inputs-legacy-geometry.log。
- 复制：draw-inputs-build-artifacts.json；独占微基准：draw-inputs-palette-perf.json 及各配置 perf-{0,1,2,3}/run.log。

以最终日志及成功标记为本次验收结果。上述证据未新增 ASan 或跨线程/跨帧压力检查；在途 GPU 完成及完整帧资源协议仍属后续阶段。已有用户工作树改动未混入此阶段提交。

## 下一增量

把已有几何/材质/模型/palette 部分聚合为有明确适用范围的生产 draw packet，梳理反射/阴影等前处理产生的纹理、UBO 和其它 pass 资源持有。再迁移 view/pass/target/lighting，定义完整帧提交、失败及释放边界；在原渲染功能和多视图顺序对拍闭合前，不引入 Render 线程。
