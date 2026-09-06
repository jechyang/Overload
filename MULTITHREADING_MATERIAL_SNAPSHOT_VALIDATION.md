# 阶段 2 第三增量：材质快照与 GPU 资源代际持有

日期：2026-09-06。前置提交：8cc2f8c（材质绑定与逐 uniform 准备/执行）。

## 实现范围

新增 Material::RecordSnapshot 和仅可移动的 Material::Snapshot，聚合选定 program、全部支持的活动属性值及实际纹理的持有句柄。录制只读取缓存的 shader 元数据并复制 CPU 值；Execute 绑定已选定的 program 并上传捕获参数，不再访问源 Material、Shader/Texture 资产包装对象。材质修改、资源重载和源对象销毁之后，已录制参数仍对应录制时的 GPU 资源代际。

Shader::RetainVariant 返回共享持有选定 program 的别名句柄，其控制块拥有整组 variants。ShaderLoader 成功编译新 variants 后，SetVariants 先完成新 pass/feature 元数据及代际分配，再交换发布并递增版本。Texture::RetainTexture 持有当前 baregl::Texture；现有 TextureLoader 的重载路径创建新 GPU 对象，再替换当前代际、递增版本。已有 GetVariant/GetTexture 借用接口及 loader 参数类型保留。

这是新增的可选材质录制接口。默认 ABaseRenderer 继续使用第二增量的逐属性即时路径，保留原签名缓存、异常前缀消费和分配成本；尚未把完整 draw 或编辑器帧切换为快照消费。几何/模型矩阵/蒙皮 palette、view/pass 常量及 target 的所有权仍须继续迁移。

## 录制事务与回放边界

- 遍历全部属性，选定变体不存在的 uniform 不捕获、不消费。数值类型检查与矩阵转置复用已有 PrepareUniformUpload。若材质缺少当前 shader 的活动属性（例如重载后尚未刷新），录制拒绝，避免继承其他 draw 的参数。
- 纹理资产自动持有解析后的 GPU 对象。裸 GPU 纹理必须匹配调用方提供的 owning shared_ptr；fallback 同样需要 owning 句柄。仅有裸指针或无控制块的别名句柄不足以证明所有权，录制拒绝。调用方仍须保证句柄确实拥有对象，不能用空 deleter 伪造持有。
- 活动 sampler 没有纹理也没有对应 fallback 时拒绝；不支持的活动 uniform 类型也拒绝。这些情形在旧即时路径可能沿用 GL 状态，新录制接口不把它们伪装为独立快照。原即时上传行为不变。
- 所有参数和资源持有成功后，才统一把此次使用的 singleUse 属性恢复默认值。类型、所有权或分配失败不发出 GL 上传，也不消费任何属性；与旧路径的成功前缀消费有意区分。
- 成功录制即消费一次，快照丢弃不会回滚；重复 Execute 不再次消费源属性。执行端 GL 失败也不是事务回滚。消费不改变既有 singleUse 标记和签名版本。
- Execute 每次绑定 program 并上传全部捕获属性，不依赖前一次回放。它会改变 GL 状态；回到即时 renderer 前必须使其签名缓存失效，例如开启新帧。不能在保留旧缓存的 DrawEntity 序列中任意插入回放，也不能放进尚未结束的 Material::Bind/UploadProperties 调用段。
- 移动后的空快照拒绝 Execute；最终释放持有后调用既有 baregl 析构回收资源。

## 所有权与尚未完成的工作

| 对象 | 本增量策略 |
|---|---|
| scalar/vector/matrix | 快照按值持有 GPU 上传载荷 |
| uniform 名称和 program | 持有整个 shader variant 代际，名称借用该代际的 program 元数据 |
| 资产纹理、裸纹理、fallback | 持有录制时解析出的 GPU 对象，资产包装对象可以随后销毁 |
| Shader/Texture 版本 | 资产内部单调计数，非全局唯一 ID；存活依据实际共享持有句柄 |
| 源 Material / PropertyMap | 不持有；成功录制后不再读取 |
| mesh/PSO/transform/palette/view/pass/target/feature buffers | 不属于此材质包，仍由外部调用链维护 |
| CPU/GPU 完成与线程归属 | 仍同线程同步消费，无 Render 线程、帧队列、GPU fence 或跨线程延迟销毁队列 |

获取、录制、回放及最终释放均限制在 GL 上下文所有者线程，并在上下文销毁前完成。这是接口契约，尚无通用线程归属运行时检查；shared_ptr 的引用计数不会使 program、纹理、资产包装对象或 Material 并发访问安全。也不能在快照有效期内绕过 loader 直接重链接已持有 program，或原地修改所持纹理内容。

代价是额外引用计数及每包参数/纹理 vector 分配；旧 shader 任一 program 被持有时，整组旧 variants 都存活。没有声称本增量提高帧率，也没有测量新录制接口的性能。为保证正确性暂不做缓存复用、纹理句柄去重或 arena 优化。Shader/Texture 内部布局改变，必须重建所有依赖模块；资产管理器本身仍为原生命周期模型，不修复其他组件长期保留的悬空资产裸指针。

## 可复现验收

先完整构建对应配置，再运行：

~~~bat
node Tests\Skinning\run-validation.mjs Debug --snapshots --vs "C:/Program Files/Microsoft Visual Studio/2022/Community"
node Tests\Skinning\run-validation.mjs Release --snapshots --vs "C:/Program Files/Microsoft Visual Studio/2022/Community"
~~~

输出为 Build/JobsValidation/Skinning/<配置>-snapshots/{baseline,current}/。旧 ABaseRenderer/Material 源码固定取自 8cc2f8c5a9d992809b95d317a70dfbcfc3928442；两程序均编译当前 Shader/Texture.cpp 并链接当前其余引擎库，以保证资源包装布局一致。因此旧/新精确对拍验证即时渲染语义，不能视作旧资源持有实现的生命周期对照。新快照 API 只在 current 运行，与同一真实 GL 上下文内的即时全量上传结果比较。

每配置检查：

| 检查 | 结果 |
|---|---|
| 原 42 组材质事务状态 | 45,003 字节精确一致 |
| 原材质实际 GL 顺序 | 11,186 字节精确一致 |
| 几何四分支与管线状态 | 256 条，19,456 字节一致 |
| 几何 RGBA | 256 帧，4,194,304 字节一致 |
| Mannequin / Standard / SceneRenderer | 24 帧，7,372,800 字节一致；22 次变化 |
| 完整材质快照回放 | 32 组实际 GL 调用和 GPU uniform 值与即时全量上传一致；先污染原 program 全部参数并改绑其他 program，再回放 |
| 录制与失败边界 | 无 GL 上传；单次/缺失属性消费、坏类型、未持有纹理、无 fallback 空纹理、分配失败、unsupported 类型、不完整属性表、移后空包拒绝 |
| 生命周期 | 8 轮成功重载/源对象销毁/最终释放；旧新代际交替回放 64 帧，逐像素一致，262,144 字节 |
| 失败重载 | shader 编译失败不发布新版本；已有 program 与持有仍有效 |
| 资源回收 | 弱引用在包存活时有效，最终包释放后过期；旧 program/texture 的 GL 对象检查确认回收 |

32 组快照覆盖 bool/int/float、vec2/3/4、mat3/4、2D/cube sampler、资产/显式裸纹理持有/null+fallback、pass/feature 选择及缺失回退，并核对源对象销毁后回放与变体缺失属性的消费边界。64 帧生命周期用例在重载前后分别产生即时渲染参考图，销毁源 Material 和两类资产包装后交替回放两个版本；不以无崩溃替代图像检查。

最终集成与构建：

- Debug/x64、Release/x64 完整解决方案均退出 0，0 警告、0 错误；最终日志后缀 snapshot-final。
- 新建 SkinningProject 三场景：96 次 Update，crowd 的 6,144 次评估全并行，fragmented 的 4,608 次全串行，rate_limited 的 1,536 次评估进入 24 批次；两配置一致。
- 真实 Editor Edit/Play/Pause/8 次单步/恢复/Stop、24 组视图消费顺序图像（11 次变化）、8 轮延迟切场景/尺寸变化两配置通过。这是默认即时路径及新资源内部表示的集成回归，不是快照渲染编辑器帧的验收。
- 编辑器、游戏和代表性模型/着色器 10 项复制逐字节一致，记录 snapshot-final-build-artifacts.json。

最终日志位于 Build/JobsValidation：snapshot-{debug,release}-final.log、snapshot-editor-{debug,release}-final.log、msbuild-{debug,release}-snapshot-final.log；成功标记为各模式 result.json。早先无 final 的日志是本次补严输入约束之前的检查，不能代替最终证据。测试期间存在并发构建/验收，输出的单实例时间不作为性能基线。本轮没有新增 ASan、Tracy 并行采集或跨帧线程测试。

## 下一增量

在这个材质持有接口上继续建立 draw 所有权：mesh/GPU geometry 代际、值拷贝 transform 与独占 palette，接入同线程真实 DrawEntity 命令消费并对拍。然后迁移 view/pass/target/feature buffer 和多视图顺序，统一处理缓存失效与帧完成/释放边界。完整帧数据闭合前不引入 Render 线程。
