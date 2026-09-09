# 渲染架构 A1：主相机静态不透明切片

日期：2026-09-09。静态拥有型命令切片实现并验证；A1 全项仍未验收（即时入口首帧差异待闭合）。

## 数据与线程归属

- RenderFrontend 是独立的薄入口，通过 Extract、Prepare、Record 连接模块；不继承旧 Renderer，不提供 Descriptor/Feature 查询，也不持有 Driver。
- SceneRenderExtractor 在主线程场景稳定期复制相机参数、对象身份、矩阵、可见性与世界包围球，并持有 Mesh 几何和 Material::PreparedSnapshot 的资源版本。ECS、源相机、Material、模型借用均不越过 Extract。
- RenderSceneSnapshot 和 PreparedFrameData 的 builder 为函数局部独占对象；只在成功时转移为 shared_ptr<const>，不公开可写别名。ViewPreparer 根据快照计算矩阵、裁剪、排序、对象常量及管线状态；纯数据实现不创建 GPU 资源、不调用旧 Renderer。
- 按旧源材质地址排序的相对组号只在 Extract 产生；后续不保留可解引用材质地址。等 key 保留原收集顺序，裁剪保留旧 max(scale, 0) 和旋转球心规则。
- StaticOpaqueCommandRecorder 只消费发布结果及显式输出目标，返回现有 RenderCommandList；RenderFrame 仍负责提交、GPU fence 和退役。缓冲区、fallback 纹理由既有 baregl/ResourceExecutor 创建，录制期间无 GL，常量只在 GL owner 执行时上传。

## 实际任务依赖与失败

Extract（Main）→ ViewPreparer（Main 或直接提交的一个 Any Worker）→ 成功检查/发布（Main）→ Record（Main）→ 原提交后端。

串行和 Worker 调用同一个 Prepare 函数。Worker 不查询服务、不嵌套提交、不等待主线程或 GL owner。主线程使用 WaitAllPassive；Result 显式保存值/异常，不能从 JobHandle 的完成状态推断成功。失败和取消抛出前先汇合，未发布部分准备输出；没有准备结果时不能调用 Record。A1 只有一个准备任务，不宣称多个准备任务重叠，也不宣称新 Record 已在 Worker 执行。

## 职责迁移与范围

本次接通主相机静态 Opaque 的独立纵向切片，旧 SceneRenderer 的 BeginFrame 不再是该请求提取/准备的必经入口。新入口支持标准静态 Mesh、稳定材质属性、默认 technique、实例数、透视/正交、相机裁剪及 frustum override；保留 Opaque 原有无条件 stencil 设置。

调用者明确选择整个 StaticOpaqueRequest，不能同一视图再调用旧 SceneRenderer 绘制这些对象。首片拒绝可见 Transparent/SceneUI、活动蒙皮、灯光、探针、后处理以及 shadow/reflection receiver 和 singleUse 属性；不消费失败输入后自动回退。缺材质/无效材质、非活动或不满足 GEOMETRY 可见性的对象沿用跳过行为。材质 override/fallback、其余 Stage 和完整功能绑定属于 A2–A5。

已有 Game、GameView、SceneView、AssetView 仍使用完整旧路径，本次没有自动切换产品默认配置。它们不是通过新前端内部的旧模式分支运行。新入口首片的直接调用及已有后端集成由专项测试验证，完整产品迁移尚未完成。

## 验证

已完成：

| 检查 | Debug | Release |
| --- | --- | --- |
| 完整解决方案构建（含 Game、Editor） | 通过，exit 0 | 通过，exit 0 |
| --frontend 三路严格对照 | 通过 | 通过 |
| Worker/无 GL/失败/取消/恢复 | 通过 | 通过 |
| 源及目标生命周期、空视图、拒绝不支持输入 | 通过 | 通过 |

每种配置、每模式 8 帧，共 196608 字节 RGBA 和 54208 字节绘制 trace；旧拥有型命令、新串行、新 Worker 逐字节一致，不使用容差。专项使用 /W3 /WX；MSVC 14.42.34433、Windows SDK 10.0.22621.0，实机 NVIDIA RTX 3070 Ti / OpenGL 4.5。

构建先执行 Dependencies/premake5/bin/premake5.exe vs2022，再通过 MSBuild/Current/Bin/amd64/MSBuild.exe 构建 Overload.sln，两个配置均使用 /t:Build /p:Platform=x64 /p:ForceRebuild=true /p:PreferredToolArchitecture=x64 /p:CLToolPath=<VS>/VC/Tools/MSVC/14.42.34433/bin/Hostx64/x64/ /p:TrackFileAccess=false /p:VcpkgApplocalDeps=false /m:2 /nr:false /p:CL_MPCount=2。沙箱账号缺 Known Folder，关闭 FileTracker 并强制重新编译；显式 Hostx64 避开旧 HostX86 编译器内存不足。早期失败尝试不作为验收证据，最终两次全量构建与专项顺序执行，没有复用旧生产对象冒充重编译。

本地证据：Build/a1-debug-final.log、Build/a1-release-final.log；Build/JobsValidation/Skinning/{Debug,Release}-frontend-command-list/result.json 及 current/run.log、compile-args.json、frontend-{0,1,2}.{rgba,draws}。构建后的 Game/Editor 未做新前端产品入口切换，不据此宣称三视口运行迁移完成。

新增入口：node Tests/Skinning/run-validation.mjs Debug --frontend（Release 同理，可追加 --vs）。输出独立放在 Build/JobsValidation/Skinning/<配置>-frontend-command-list/current/，不改任何历史 baseline。

专项在相同进程中为旧 SceneRenderer 拥有型命令路径（无新 Prepare）、新串行、新 Worker 分别新建场景、相机、材质、模型、目标和前端，每模式 8 帧；使用真实 Standard.ovfx 和单一材质，精确比较像素、draw 参数、对象/视图/user 常量、材质属性及关键管线状态。旧 Feature 的墙钟时间从 trace 排除；当前 Standard 默认变体不使用时间，新路径时间字段单独对显式请求值断言。没有比较地址、GPU 句柄或 signature。

Debug/Release 的 --frontend-immediate 均以 exit 1 结束：首次初始化的第 0 帧有 98 个像素与上述三种命令路径不同；第 1–7 帧一致，所追踪 draw 状态、矩阵、uniform 和 sampler 内容一致，原因未闭合。本次仅验收既有拥有型命令后端到新前端的切片，不宣布 A1 全项完成；没有修改旧实现或放宽像素容差。下一会话继续 A1，先调查即时入口首帧差异。失败日志和原始对照位于 Build/JobsValidation/Skinning/{Debug,Release}-frontend-immediate-command-list/current/，失败运行没有 result.json 成功记录。

测试专用 ViewPreparer 源副本只在入口和首次私有 draw 生成后插入观察/故障钩子；有超时闩锁验证无当前 GL、实际 Worker 身份、失败/取消先退出后释放最后输入。故障发生时已经存在部分准备结果，仍不能发布；随后正常请求必须成功。生产入口不接收任意 Worker 回调。SystemCalls 的测试副本只将日志 AppData 路径指向输出内的 profile，避开测试账号 Known Folder 缺失；不是引擎行为修改。

## 尚留适配器与下一会话

- StaticOpaqueRequest/SceneRenderExtractor 是场景输入适配器，首片外功能继续留在旧产品入口；A2–A5 扩展数据契约并迁移调用者，A6 删除旧继承链及无人使用适配代码。
- StaticOpaqueCommandRecorder 是有限后端桥，复用 RenderCommandList/RenderFrame；A3 将其最小绑定/录制职责并入通用 DrawPacketBuilder/CommandRecorder，不建立第二套提交后端。
- 下一会话先读 A0 与本文，继续 A1：复现并定位即时 oracle 第 0 帧差异，补充严格对照，满足完整验收后再评估 A2。本会话不推进 A2。
