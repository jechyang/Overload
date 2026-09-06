# 阶段 2 第二增量：材质绑定与 uniform 命令

日期：2026-09-06。前置提交：`50194bf`（同线程几何命令）。

## 实现范围

Material::Bind 拆为 CPU `PrepareBinding` 与 GL `ExecuteBinding`。准备阶段选择 pass/feature shader 变体、计算签名和是否需要绑定；执行只消费选定 program 与绑定标志。原 Bind 包装保留 m_programInUse 的设置时机，Unbind 和 renderer 的 signature 缓存逻辑保持原位置。

Material::UploadProperties 的每个 uniform 拆为 CPU `PrepareUniformUpload` 与 GL `ExecuteUniformUpload`。准备阶段查已缓存的 shader 元数据、按值捕获参数、检查数值类型、转置矩阵，并将纹理资产/空纹理解析为实际 baregl::Texture 指针。执行只读命令，不访问源 Material、PropertyMap 或纹理资产包装对象。

仍按原 unordered_map 遍历顺序逐属性准备、立即执行，再恢复单次属性默认值。这样保留以下细节：

- stable/single-use 上传标志各自筛选；当前变体未使用的属性不上传、不消费。
- 单次属性完成一次上传后只恢复 value，不清 singleUse、不增加签名版本；下一次缓存命中可以跳过上传。没有改变这一既有语义。
- 类型错误发生在同一属性的 GL 上传之前：已成功上传的前缀已消费，失败项和后缀未消费。
- 不支持的 uniform 类型继续不发出上传；已找到的单次属性仍按原行为恢复。
- 矩阵转置顺序与实际 GL 参数不变；空纹理优先使用对应 2D/cube fallback，二者均空时不绑定、不写 sampler。

本次还修复 `CalculateSignature` 的 `MaterialSignatureSet signature` 未初始化问题：HashCombine 在第一次合并前读取 seed，必须先将签名集合归零。修复后相同材料/程序/版本/空纹理能可靠命中缓存。

## 所有权与限制

| 命令数据 | 当前持有方式 |
|---|---|
| 绑定 signature / bindRequired | 值拷贝 |
| bool/int/float、向量、矩阵参数 | 值拷贝；矩阵准备时已转置 |
| uniform 名称 | 借用所选 ShaderProgram 的元数据字符串，未借用 Material 属性 key |
| ShaderProgram | 借用；从准备到执行不得重编译、重链接或销毁 |
| 纹理 | 准备时解析后的 baregl::Texture 借用指针；GPU 对象与内容须保持稳定 |
| 源 Material / PropertyMap | 命令不保留；原同步包装仍负责遍历和逐属性消费 |

准备/执行不新增 draw 级 vector、属性名字符串分配或全局暂存区，不改变 Material 对象布局。上传载荷直接使用可平凡复制的 baregl 数值类型，并以 static_assert 约束；向量显式构造、矩阵按浮点数组复制，避免重复复制引擎矩阵 variant 及原来的类型强转。命令可在源 Material/参数销毁后消费，前提是借用的 program、名称元数据和纹理仍有效，而且执行时绑定的是对应 program。

**尚不是完整材质 draw packet，也没有资源在途持有协议。** 生产路径仍逐 uniform 同线程立即消费，不跨 draw/帧缓存命令，不提交 worker GL 任务。scalar 快照不使资源变成线程安全；整个帧录制仍需后续聚合参数、保留资源版本、定义单次属性消费/失败边界和完成通知。

## 可复现验收

入口：`node Tests\Skinning\run-validation.mjs Debug --material`，Release 同理；VS 安装发现失败时使用已有 `--vs` 参数。先构建对应配置完整解决方案。

两份独立程序的 ABaseRenderer/Material 源码分别来自 `50194bfe7ee94eac37d63bbf84779c7c68b390e4` 与工作树。**旧 Material 对照副本仅把签名 seed 归零**，避免以未定义栈内容作为缓存语义基准；旧上传和绑定代码未拆分。两者链接相同其余引擎库，数学常量仍先于旧静态默认相机初始化。生产源码没有测试宏。

测试包括 42 组缓存/属性/变体/纹理事务，实际 GL 程序绑定、纹理绑定及支持的 uniform 上传序列，GPU uniform 读回与源属性状态；单次属性筛选、缺失属性保留、中途类型错误前缀消费；新命令的无 GL 准备和源 Material 销毁后参数隔离。另复用 256 组几何提交/图像、24 帧真实蒙皮对拍及真实编辑器项目回归。

Debug/Release 的最终参数载荷实现已通过：

| 检查 | 每配置结果 |
|---|---|
| 42 组材质事务，GPU uniform 与 CPU 属性状态 | 45,003 字节逐字节一致 |
| 实际 GL program/texture 绑定和 uniform 上传顺序 | 11,186 字节逐字节一致 |
| 参数准备无 GL、源 Material 销毁后的快照隔离 | 通过；包含矩阵转置和纹理资产预解析 |
| 几何提交/管线状态 | 256 条，19,456 字节一致 |
| 几何 RGBA | 256 张，4,194,304 字节一致 |
| Mannequin / Standard / SceneRenderer | 24 帧，7,372,800 字节一致；22 次画面变化，姿态版本不变 |

独占微基准关闭所有 GL 拦截后，以旧/新/新/旧顺序串行运行两遍；每次预热 32 组、测量 256×32 次更新 scalar、Bind、10 uniform 上传、Unbind，Tracy 未连接：

| 配置 | 旧路径中位数（两次） | 新路径中位数（两次） |
|---|---|---|
| Release | 0.741 / 0.684 μs | 0.678 / 0.675 μs |
| Debug | 4.688 / 4.719 μs | 5.353 / 5.856 μs |

Release 在本用例下同属约 0.7 μs，不据此宣称加速；Debug 仍有约 0.6～1.2 μs/10 uniform 的快照成本。首稿直接复制 MaterialPropertyType 时 Debug 为约 7.1 μs，已改为可平凡复制的 GPU 载荷。该测量只用于识别本增量成本，不代表复杂材质、所有硬件或整帧性能；p95 有抖动，原始记录见 `Build/JobsValidation/material-stage2-perf-payload.json` 与 `<配置>-material/perf-payload/`。

最终集成验收：

- Debug/x64、Release/x64 完整解决方案均退出 0，0 警告、0 错误。日志为 `Build/JobsValidation/msbuild-{debug,release}-material-final.log`。
- 新产物的真实 Editor Edit/Play/Pause/8 次单步/恢复/Stop 两配置均通过；各 24 组同视图消费顺序 RGBA 一致，11 次动画变化，8 轮延迟切场景/尺寸变化/还原通过。日志为 `material-editor-{debug,release}-final.log`。
- 新建 SkinningProject 三个场景统计保持一致：96 次 Update 中 crowd 的 6,144 次评估全部并行；fragmented 的 4,608 次评估串行；rate_limited 的 1,536 次评估进入 24 批次。
- 编辑器、游戏输出及代表性模型/着色器的 10 项复制均逐字节一致，见 `material-final-build-artifacts.json`。

对拍日志：`Build/JobsValidation/material-{debug,release}.log` 与 `Skinning/<配置>-material/{baseline,current}/`；result.json 是该轮成功标记。最终测试、构建和编辑器验收均使用可平凡复制的 GPU 载荷版本。首稿的构建/编辑器/性能日志仅保留作诊断，不替代上述最终结果。本轮没有新增 ASan、跨帧资源回收或 Render 线程验收；已有用户改动仍留在工作树，未混入此阶段提交。

## 下一增量

在已拆开的准备/执行入口上，定义完整 draw 参数快照及资源版本持有；为批量录制明确单次属性的提交/消费边界。随后继续迁移 transform、palette、view/pass 常量和目标命令。在所有权与消费完成协议闭合前，保持同线程，不引入 Render 线程。
