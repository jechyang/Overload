# FrameGraph F1：资源契约和独立节点

2026-10-05，沿用 F0 Debug/x64、RTX 3070 Ti / NVIDIA 616.92 环境。

同一个 RenderPlan 增加作用域/代际资源句柄、精确描述、子资源访问、独立拥有型
操作和编译结果。没有新建另一套图或场景调度器。编译结果只是依赖边/存活区间/
名称等元数据，不复制场景、纹理内容或录制操作；Record 消费已经编译的计划。

描述覆盖 2D/cube 纹理、深度/模板 renderbuffer、extent、格式、mip、layer、samples
以及纹理过滤/wrap/可变存储配置。首版只接受后端支持的组合；对象复用必须完全
兼容。支持 Sample、附件、Copy、Mipmap、Readback、Export、Present，Clear/Load/
DontCare、Store/Discard 和清除值；部分写入不会被当成完整初始化。

固定顺序建立 RAW/WAR/WAW 和显式依赖。校验外图/过期句柄、未初始化读取或 Load、
Discard 后读取、aspect/mip/face、附件/尺寸、拷贝尺寸、非法反馈和逆序依赖。
实际 cubemap mip 操作必须涵盖全部面；重复导入相同存储代际归一，不能改变初始
有效性/历史归属。导入身份可跨场景共享，原有场景专属资源约束继续保留。
场景目标深度/模板 renderbuffer 已按实际 Clear/Load 登记，而非隐式留给 framebuffer。
文本转储包含图作用域、Pass/资源名称、存活区间、依赖与临时资源存活峰值估算。

```powershell
F:/npm/node.exe Build/FrameGraphValidation/build-native.mjs Debug
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --framegraph
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --postprocess
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --pipeline
```

Debug baregl、Rendering、UI、Core、Editor、Game 构建通过。新契约测试全部通过。
实际 GL 拷贝节点无 View，通过 ComposeFrame → PrepareFrame → RecordFrame → RenderFrame
执行；录制前源和目标 resize/销毁，保留代际的输出仍精确相同。
后处理串行/双 Worker/单 Worker 主图、阴影和所有 cube mip 精确一致，匹配 A5 SHA256。
四种自定义管线 × 三种调度的两个输出和无效输入诊断通过。

过渡状态明确保留：旧场景颜色访问仍兼容 face mask；后处理和反射 mipmap 隐式
字段在 F2 删除。逻辑临时资源在此阶段只编译，不分配；资源池与 GPU 租约属于 F3。
后处理私有缓存尚未清除，不将 F1 作为内存降低成果。F0 原始数据与旧像素基线保留。
