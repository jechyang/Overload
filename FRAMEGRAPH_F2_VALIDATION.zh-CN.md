# FrameGraph F2：显式后处理和 mip 节点

2026-10-05，沿用 F0/F1 的 Debug/x64 和同机 GPU 环境。

PostProcessPipeline::Build 将后处理逐操作追加到现有 RenderPlan。独立 Record
接口也构建同一个计划并执行这些节点。删除 postProcessAfter、postProcessReads/
Writes、m_postProcessAfterView、m_postProcessCommands 和反射专用 mip 尾插字段。
RenderFrontend 按固定计划顺序拼接场景与独立节点，没有第二条后处理执行路径。
实际拼接共用 RenderPlan::RecordOperations，原 GPU 效果计时作用域在 consumer
上按节点区间恢复，异常仍会在 GPU 退役前关闭。失衡作用域在编译阶段拒绝。

节点包括进/出 blit、亮度、亮度完整 mip、曝光历史初始化/适应/应用、Bloom
复制/downsample/upsample/合成、Tonemapping、FXAA 和反射完整 cube mip。
每个节点声明全部动态采样输入与输出。Bloom 上采样明确 Load 已有目标再相加，
不会把附件加载当成采样反馈或完整覆盖。全屏覆盖检查 viewport/state，部分
scissor、stencil、depth、线框或颜色屏蔽状态不能初始化未知内容。

全屏录制只使用已准备的目标，尺寸不合时失败，不再调用 Resize。
管线和 Bloom 的尺寸准备发生在 owner 边界；亮度固定尺寸无需重复 resize。
现有私有缓冲作为资源导入，池化尚属下一阶段。

曝光 History 仍在推进计时/双缓冲前登记。首次或失败恢复显式初始化前一曝光
纹理：原 shader 即使关闭 progressive 仍采样该纹理，图因此不能声明它已有内容。
该初始化没有改变最终输出。取消/Prepare/Record/Execute 失败仍使既有 epoch 失效。

```powershell
F:/npm/node.exe Build/FrameGraphValidation/build-native.mjs Debug
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --framegraph
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --postprocess
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --pipeline
```

Debug baregl、Rendering、UI、Core、Editor、Game 构建通过。原后处理 14 帧主图、
阴影和全部 cube mip 的串行/双 Worker/单 Worker 精确对照通过，九个文件仍匹配
历史 A5 SHA256。曝光/反射失败、取消、Worker 逆序、部分提交汇合与源/目标
销毁恢复包含在原入口。冻结数据位于 Build/FrameGraphValidation/F2/postprocess。

新测试通过 16 个效果组合 × 1×1、1×32、63×17、96×64；覆盖 Bloom 1/10 层
请求边界、Load 混合、显式 mip/历史节点、无效句柄/依赖和实际有限 GL 像素。
可检查 postprocess-plan.txt 的每个资源区间和节点。独立 GL 拷贝生命周期仍通过。
自定义管线的独立节点缺少拥有型操作仍被拒绝，替代已删除隐式字段的拒绝测试。

此阶段没有更改 RGBA32F、采样/wrap、效果算法、mip/face、进出 blit 或像素容差。
未宣称降低常驻内存；接下来的资源池、GPU 租约和私有临时字段迁移会单独验证。
