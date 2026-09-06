# 阶段 2 首增量：同线程几何命令与所有权审查

日期：2026-09-06。前置基线：阶段 0 `30eb8fe`、阶段 1 `4f8da08`。

## 范围与调用顺序

本增量把 ABaseRenderer 的最终几何提交拆为 `PrepareGeometryDraw` / `ExecuteGeometryDraw`，仍在原线程、原次 DrawEntity 调用内立即消费。它是阶段 2 的第一个独立边界，**不是完整可交接帧，也不代表阶段 2 已全部完成**。

实际顺序保持为：

1. CompositeRenderer 检查 drawable，按既有 feature 顺序执行 OnBeforeDraw；其中仍可能上传 UBO/蒙皮 SSBO、修改 PSO 和材质临时属性。
2. ABaseRenderer 准备几何命令，按值固定合成后的 PipelineState、primitiveMode、正实例数；借用 IMesh。
3. 原 Material::Bind、signature 判定、UploadProperties 和 signature 缓存更新。
4. ExecuteGeometryDraw 只读命令调用 Driver::Draw；Driver 按原顺序设置 PSO、Bind mesh、读取 index/vertex count、选择普通/实例化绘制、Unbind mesh。
5. 原 Material::Unbind，然后 CompositeRenderer 的 OnAfterDraw。

Material::Bind / UploadProperties / Unbind 不修改 GPUInstances，因此提前捕获实例数不改变当前同步可观察行为。没有提前读取 mesh count，以免改变自定义 IMesh 的 Bind/查询顺序。命令字段为 const，准备/执行方法保持 protected；没有容器、排队、帧间缓存或新增任务提交。

准备函数拒绝缺失 mesh/material 及非正实例数，防止负实例数转换为无符号数。生产 DrawEntity 仍要求先满足 IsDrawable（包括有效 shader、相机投影支持）；CPU 准备函数不是完整材质/视图有效性检查。

## 当前所有权与后续迁移边界

| 数据/状态 | 本增量的所有权及有效期 | 完整帧交接前仍需完成 |
|---|---|---|
| PSO、primitive、instances | 栈上 GeometryDrawCommand 按值持有，准备后不可变 | 可以纳入更上层 draw packet |
| IMesh 与 VAO/VBO/IBO | 命令借用 IMesh；原资源所有者负责同次提交内存活、GPU 数据稳定；执行仍读取 count | 保留资源版本/句柄及在途持有；明确更新与延迟销毁协议 |
| Material / Shader / Texture | 外层 Drawable 借用 Material；Bind/Upload 同步消费共享属性、程序和纹理；单次属性可能重置 | 生成独立材质绑定与参数快照；保留 shader 变体/纹理版本；执行端不再查活 Material |
| Actor / Transform | SceneDrawableDescriptor 仍持有 Actor 引用；筛选、feature 等仍读 Scene/Actor | 在稳定期内复制世界变换及所需 Actor 标量；删除后无活对象引用 |
| 骨骼矩阵/count/version/bounds | SkinningDrawableDescriptor 仍借用组件矩阵；SkinningRenderFeature 在当前线程上传并维护每 renderer 缓存 | 帧拥有一致 palette 快照/分配区及版本；与姿态发布/组件销毁解耦 |
| Camera / view / pass 常量 | FrameDescriptor 及 feature 保持原借用与 UBO 更新；几何命令不包含这些常量 | 按 view/pass 固定常量和更新命令，避免多个视图覆盖共享可变状态 |
| framebuffer / viewport / clear / blit | 仍由同步 BeginFrame、pass、Blit 调用管理；命令依赖执行前的绑定环境 | 表达目标/尺寸/清除/转换/读回的有序命令及资源有效期 |
| transparent / reflection / shadow / override | 保留现有 pass 与 multimap 顺序、材质替换及 feature 回调位置 | 逐类迁移；保留等值排序、fallback/override、反射内 Filter 语义 |
| UI / debug / gizmo / picking | 使用原 UI/编辑器调用链；经过 ABaseRenderer 的 draw 自动使用该边界 | 单独审查纹理、选择 ID、读回时机及 UI 数据所有权 |

几何命令不保存 Drawable/Material，但仍依赖外部已绑定 shader、纹理、uniform、palette、viewport 和 target，不能保存起来换一次 draw 再执行。`const` 引用不会延长 mesh 生存期，也不防止其他线程修改资源。

本次 CPU 消费完成点是 ExecuteGeometryDraw 返回，随后原调用链清理绑定、局部命令销毁；**不是 GPU 完成标记**。未来帧 arena 回收、GPU 资源释放与上传区复用需要分别建立消费完成/设备完成保证。当前不新增跨帧在途引用，也不因此引入 fence。

## 可复现验收

入口与覆盖细节见 `Tests/RenderingCommands/README.md`；测试保留阶段 1 ABaseRenderer 源码作为旧路径，未在生产代码里增加测试分支。

| 检查 | Debug / Release 结果 |
|---|---|
| 准备阶段无 GL/mesh 虚调用，源 Drawable/Material 修改和销毁后状态独立 | 各 64 组通过；缺失 mesh/material、零/负实例数拒绝 |
| 真实 GL 提交参数与关键状态顺序 | 各 256 条记录逐字节一致，19,456 字节；四种 index/instance 分支各 64 次，无 GL 错误 |
| 64 种 stateMask × 4 分支图像 | 各 256 张 64×64 RGBA 完全一致，4,194,304 字节；各 108 张有非背景像素 |
| 真实 Mannequin / Standard / SceneRenderer | 各 24 帧 320×240 RGBA 完全一致，7,372,800 字节；22 次画面变化，绘制不推进姿态 |
| 真实 Editor 模式/共享资源/视图 | 两配置均通过 Edit/Play/Pause/8 次单步/恢复/Stop；24 组同视图消费顺序图像精确一致，11 次动画变化 |
| 真实项目延迟切场景、尺寸变化和还原 | 两配置各 8 轮通过 |
| 完整解决方案 x64 | 两配置均退出 0，0 警告、0 错误 |
| 编辑器/游戏输出及代表性资源复制 | 10 项均逐字节一致 |

项目仍按用户授权生成在 `Build/JobsValidation/Skinning/<配置>-editor/current/SkinningProject/`，包含 crowd、fragmented、rate_limited 三个场景。每配置连续 96 次 Update：crowd 6,144 次评估全部进入并行批次；fragmented 4,608 次评估串行；rate_limited 1,536 次评估进入 24 个批次。与阶段 1 收尾统计一致。

最终证据：

- `Build/JobsValidation/geometry-{debug,release}.log` 及 `Skinning/<配置>-geometry/result.json`、baseline/current 下的 compile.log、run.log、draws.bin、geometry-frames.bin、frames.bin。
- `Build/JobsValidation/geometry-editor-{debug,release}.log` 与 `<配置>-editor/current/` 的项目、统计和图像。
- `Build/JobsValidation/msbuild-{debug,release}-geometry-stage2.log`。
- `Build/JobsValidation/geometry-stage2-build-artifacts.json`。

最初的 Debug 独立测试在 main 前超时：把 ABaseRenderer.cpp 直接链接后，其遗留静态默认相机先于库内数学常量初始化。测试现对旧/新两份程序都先编译/链接所需数学常量源码，不修改旧渲染器、不关闭断言。非索引用例也改为通过 IMesh 复用有效 VAO，避免现有 Mesh 上传对空索引缓冲的限制。最终两配置均使用修正后的同一测试源通过；早期超时及早期夹具输出不作为验收证据。

这次验证覆盖同步几何边界及上述编辑器场景，不是所有 GL 状态/全部 pass 的录制证明，也没有新增跨帧、ASan 或 GPU 完成协议验收。验收期间存在并发构建/测试，附带 SetTime 耗时不用于性能结论。未改动的阶段 1 算法、调度阈值和性能结论继续以原报告为准。

## 下一个独立增量

先拆 Material 的 CPU 参数/变体选择与 GL 绑定上传，定义有所有权的材质参数及纹理引用，保持单次属性重置和跨 draw signature 语义。之后按依赖迁移 Actor transform、palette、view/pass 常量和目标命令。每一类都先同线程对拍并提交；资源在途持有和消费完成协议闭合之后，才进入 Render 线程。
