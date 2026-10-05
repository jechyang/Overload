# FrameGraph F0：输出与内存基线

2026-10-05；起点 327601d0。Debug/x64，MSVC 14.42.34433，SDK 10.0.22621.0，
RTX 3070 Ti / NVIDIA 616.92 / OpenGL 4.5。已有工作区修改保留在
Build/FrameGraphValidation/F0/worktree.txt，未清理用户文件。

## 已定位的占用

生产代码只有 RenderPlan；RenderSceneSnapshot 和 PreparedFrameData 禁止复制，
后者通过 shared_ptr 保留提取结果。每个 View 的矩阵/候选物体和 draw 列表是
独立视角输入，几何、纹理、材质代际和骨骼调色板使用拥有型共享引用。
测试运行器在 Build 下生成的源码副本是编译夹具，不是进程中的另一套 FrameGraph。
目前没有证据支持“两个独立 FrameGraph 常驻”的假设。

确认的问题是 PostProcessPipeline 每视口私有缓存，效果关闭后仍保留最大尺寸。
AutoExposureEffect 构造即分配 1024² RGBA32F 的亮度纹理；Bloom 留存十个采样
目标和一个全尺寸输出；管线另外持有两个全尺寸 ping-pong。效果关闭不回收。

以下隔离测试只创建三个生产后处理管线和一个 1920×1080 目标，无场景/物体副本。
实际查询每个 GL 纹理已存在的 mip 尺寸/通道位宽；统计不含 renderbuffer、驱动
对齐或其他 GPU 对象，因此是纹理存储字节，不能称为全部 VRAM。

| 阶段 | 纹理 MiB | 进程 Private MiB | Working set MiB |
|---|---:|---:|---:|
| 空 context | 0 | 244.8 | 141.9 |
| 三个管线刚创建 | 48.0 | 407.1 | 209.8 |
| 三管线，所有效果关闭，1080p | 174.6 | 677.8 | 210.2 |
| 开 Bloom | 396.0 | 1018.1 | 211.7 |
| 开曝光和 Bloom | 412.0 | 984.2 | 246.8 |
| 再次关闭全部效果 | 412.0 | 984.2 | 246.8 |
| 销毁目标和管线 | 0 | 588.0 | 241.3 |

原始字节/逐纹理清单位于 F0/memory/memory.csv、textures.csv；保留当时可执行
文件及编译参数。销毁后纹理归零，进程 Private 未立即回到启动值，说明驱动/CRT
缓存影响进程统计；不能把二者差值直接当成引擎泄漏。这次隔离测量确认常驻缓冲
问题，不代表用户具体场景的全部增长原因，也不说明某个历史提交的增长比例。

## 固定生产顺序和所有权

| 链 | 内容与目标 | 生命周期/外部消费 |
|---|---|---|
| Shadows | 每方向投影灯深度纹理；Clear 后绘制 | 灯 owner；主视角采样 |
| ReflectionCapture | cubemap 指定 face，最后一面生成完整 mip | Probe owner；渐进 history，主视角采样 |
| MainView | ViewportSurface RGBA32F + 深度/模板 renderbuffer | 视口持有；后处理、工具、最终 UI |
| PostProcess | 入口 blit → 曝光 → Bloom → tonemapping → FXAA → 出口 blit | 当前隐式块；私有目标未纳入完整生命周期 |
| Exposure | 1024² 亮度+mip → 1² 曝光适应 → 全尺寸补偿 | 两个 1² 曝光历史属于视口，临时亮度不应常驻 |
| Bloom | 全尺寸复制 → 逐级 downsample → additive Load upsample → 合成 | clamp-edge RGBA32F，采样链每级两轴减半/至少 1 |
| SceneUI/CanvasUI/OverlayUI | 既有场景 UI，保留主目标内容 | 采样材质/UI纹理；颜色和深度/模板作用 |
| EditorOverlay/Picking | Grid/Outline/Gizmo/调试对象、Picking、读回 | 最终场景色、深度/模板、独立 Picking 目标 |
| Editor product | Asset → Game → Scene → camera preview → ImGui | 输出纹理跨帧持有，隐藏视口保留旧代际 |
| Game product | 场景链 → UI → back-buffer blit/present | 显式外部输出，现有 RenderFrame fence 退役 |

计划以现有 RenderPlan 演进，不并存独立图。资源分配移到发布 Worker 输入之前，
按固定 GPU 顺序编译访问/生命周期；保留格式、采样状态、历史、mip/face、进出
blit 和 single-use 属性顺序。租约沿用 RenderFrame 的 GPU 退役协议。

## 基线验证和后续指标

Debug --postprocess：串行/双 Worker/单 Worker 均通过，14 帧主视图、阴影和
全部 cube mip 精确相同，历史 A5 SHA256 基线匹配；原文件复制冻结至 F0/postprocess。
Debug --pipeline：四种管线配置 × 三种调度，两个目标像素精确相同，配置、源与
目标生命周期及七种无效输入诊断通过；冻结至 F0/pipeline。

Debug --editor --command-list --editor-ui --product-loop：实际 Picking、三视口、
完整 UI、Edit/Play/Pause、resize、源销毁、失败/取消与产品循环内部精确对照
均通过；运行器最终与历史 A5 editor-command-frames.bin 的 SHA256 对照失败。
这发生在 FrameGraph 改动之前，当前 UI 已有多轮改动；未重写历史清单，也未宣称
该完整入口通过。当前产物单独冻结到 F0/editor-product，后续同时比较这份基线。

测试夹具对反射私有序列化方法保留 MSVC 访问限定 ABI，修复旧访问覆盖造成的
LNK2005，不更改生产逻辑。正式内存入口：

```powershell
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --framegraph-memory
```

后续记录逻辑请求、物理分配、复用、空闲/在途字节、估算存活峰值、resize分配、
CPU编译/录制耗时。对比保持相同格式/尺寸/效果/线程模式；图像不改历史基线，
禁用复用使用同一图路径。尚未实现的指标和尚未运行的配置不标记通过。
