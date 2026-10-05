# 当前渲染架构与 FrameGraph 数据流

核对日期：2026-10-06。代码基线：F5 `78c6d9f0` 加 F6 收尾改动。
本图对应当前产品链；历史 A0–A6 记录保留各阶段当时的实现与验收。

## 产品帧的形成

```mermaid
flowchart TB
    update[Main：更新场景、动画和产品状态]
    extract[ExtractFrame / Collectors<br/>复制场景、工具和交互输入]
    fragments[拥有型 SceneFrame 片段<br/>固定顺序与完整访问声明]
    compose[ComposeFrames<br/>映射片段句柄、归一化共享导入]
    ui[Main：提取 ImGui<br/>保留实际纹理代际]
    plan[唯一产品 RenderPlan<br/>场景、工具、Picking、UI、Present]
    compile[Compile<br/>验证生产消费、内容版本与存活区间]
    pool[RenderTargetPool.Realize<br/>冻结物理映射并取得帧租约]
    prepare[PrepareFrame<br/>只读场景准备、排序和绑定]
    record[RecordFrame + Seal<br/>CPU 独占槽编码、按计划顺序合并]
    frame[RenderFrame]
    execute[固定 GL owner<br/>Main 或 RenderThread]
    retire[GPU fence 完成后退役<br/>释放捕获、租约及外部 pin]
    update --> extract --> fragments --> compose --> plan
    ui --> plan
    plan --> compile --> pool --> prepare --> record --> frame --> execute --> retire
    resource[ResourceExecutor<br/>仅缺失资源交给 GL owner]
    pool -. 批量创建 .-> resource
    resource -. 同一 GL owner .-> execute
    jobs[JobSystem Workers<br/>Prepare / Record，无 GL 与 ECS 读取]
    prepare -. 独占输出槽 .-> jobs
    record -. 独占命令槽 .-> jobs
    retire -. Picking 完成结果与历史成功状态 .-> update
```

Editor 按 Asset → Game → Scene → Camera Preview → ImGui 保持 GPU 顺序。
Game 按场景 → 后处理/场景 UI → backbuffer blit → 调试 UI → Present 保持顺序。
CPU 任务完成顺序不会改变这些顺序。即时与提交模式都走这一计划，均受资源退役约束。

| 数据 | 范围与所有权 |
|---|---|
| RenderSceneSnapshot | Main 稳定读取期提取场景；保留材质、几何、纹理和程序代际，复制矩阵、灯光与 palette。 |
| SceneFrame | 可作为一个视口片段或组合后的产品准备输入；拥有 Views、RenderPlan、历史登记和准备结果。 |
| RenderPlan | 逻辑资源、描述、子资源访问、Load/Store、内容版本、固定依赖与独立操作节点。 |
| RenderResourceMapping | 编译后冻结的逻辑到物理快照映射；仅完全兼容且存活不重叠的 scratch 可共享。 |
| RenderCommandList | CPU 拥有型操作；上传、绑定、绘制、读回与合成在消费时执行。 |
| RenderFrame | 提交、呈现请求、成功/失败、GPU fence 和退役；持有池租约到 GPU 完成。 |

快照保留存储代际，不自动复制每一次动态 GPU 内容。内容依赖仍由计划和命令顺序保证。

## GPU 资源与消费者

```mermaid
flowchart TB
    shadow[ShadowCaster] --> depth[阴影深度]
    capture[ReflectionCapture faces] --> cube[跨帧 cubemap]
    cube --> mip[六面完成后 Mipmaps]
    mip --> scene[主视图 Opaque / Transparent]
    depth --> scene
    scene --> color[视口颜色输出]
    color --> post[Blit → Exposure → Bloom<br/>→ Tonemapping → FXAA → Blit]
    pool[产品池 scratch<br/>同帧、跨视口及退役后复用] <--> post
    history[viewport 曝光历史] <--> post
    post --> sceneui[SceneUI / OverlayUI]
    color -. 后处理关闭 .-> sceneui
    sceneui --> tools[Grid / Debug / Outline / Gizmo]
    tools --> output[持久视口输出]
    output --> imgui[ImGui 声明实际采样]
    output --> game[Game BackbufferBlit]
    picking[Picking Geometry / Icons] --> target[Picking 目标]
    target --> readback[Readback 节点]
    readback --> resolve[Main 检查场景 token / GUID<br/>应用选择、拖放与 Gizmo 输入]
    imgui --> present[Present 边界]
    game --> present
    retained[材质 / Texture Debugger<br/>仍引用上一代池存储] --> pin[外部导入持有独立 pin<br/>最后一个消费者退役后可回池]
    pin -. 防止提前覆盖 .-> pool
```

这些操作均在产品图内。后处理没有私有 scratch 缓存或图后的尾插消费者。
曝光、反射历史、视口输出与资产保持持久 owner，不作为瞬态别名目标。
关闭效果停止申请其临时资源；池中闲置缓存按预算淘汰。

## 线程与退役

```mermaid
sequenceDiagram
    participant M as Main
    participant W as CPU Workers
    participant R as GL owner
    participant G as GPU
    M->>M: 更新、提取全部片段及 UI，合成产品图
    M->>M: 编译一次，查询暖池映射
    opt 资源缺失
        M->>R: ResourceExecutor 批量创建
        R-->>M: 发布正确代际
    end
    M->>W: Prepare，独占范围结果
    W-->>M: 汇合终态并检查失败
    M->>W: Record / Seal，独占命令槽
    W-->>M: 按固定计划顺序汇合
    M->>R: Submit RenderFrame
    Note over M,R: RenderThread 最多两帧在途，队列满时背压
    R->>G: 上传、Draw、Readback、合成
    R->>R: Present 回调，插入 fence
    G-->>R: GPU 完成
    R->>R: 退役并释放租约
    R-->>M: Completion / Picking / 失败状态
```

Main 即时模式同步提交并等待退役；Main 命令模式也由 Main 执行。
RenderThread 模式要求启用帧提交。parallel_recording 在上述有效模式中控制同一
CPU 准备/录制实现；Worker 不访问面板、ECS 或 GL context。

Main 产品池预留一套，RenderThread 预留两套；默认闲置预算 256 MiB。
暖池只做 CPU 查询。尺寸、效果或容量变化可能需要 GL owner 创建资源。
当前图需要的备用目标更新 LRU 使用时间；过量缓存仍可淘汰。
首次出现 ImGui 字形仍需字体纹理上传，不能视为完全无 owner 等待的暖态。
取消、异常和关停不能以 CPU 录制结束代替 GPU 退役。

## 可检查的结果与边界

`CompiledRenderPlan::Dump` 显示节点、访问依赖、版本与逻辑存活区间；
`RenderResourceMapping::Dump` 显示物理槽、纹理及真实别名；池统计显示分配、
命中、淘汰、闲置/租用字节与峰值。实际纹理级别字节不等于全部驱动 VRAM，
Windows Private Bytes 也包含驱动缓存。

当前编译器验证固定顺序，不进行自动裁剪、Pass 重排、多队列同步或 Vulkan barrier
生成。已有 C++ 配置示例在 Tests/RenderingCommands/inspection_pipeline.h；
没有脚本 SRP SDK、流水线编辑器或新增渲染效果。

实现与验证见 [当前架构](RENDERING_ARCHITECTURE.md)、
[FrameGraph F5](FRAMEGRAPH_F5_VALIDATION.zh-CN.md)、
[FrameGraph F6](FRAMEGRAPH_F6_VALIDATION.zh-CN.md) 及
[测试入口](Tests/RenderingCommands/README.md)。
