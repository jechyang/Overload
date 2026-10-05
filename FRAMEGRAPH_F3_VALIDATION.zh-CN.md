# FrameGraph F3：临时目标池和 GPU 租约

2026-10-05，沿用 F0 同机环境，Debug/x64。

RenderTargetPool 消费唯一 RenderPlan 的编译结果。按首次使用排序，选择描述
完全兼容且上一次使用结束早于本次开始的物理槽；同一节点的输入和输出不复用。
纹理格式、尺寸、mip、层、采样与用途、filter/wrap、可变存储格式均参与兼容判断。
支持实际 Texture2D、Cube 分面附件及深度/模板 Renderbuffer。没有底层 heap 别名。

ResourceExecutor 在 GL owner 上批量实现目标，发布不可变映射。独立节点的
prepareOperation 在 Main 解析逻辑句柄，Worker 只编码拥有型不可变输入。
RenderFrontend 在 PrepareViews 开始前编译并实现资源。映射及物理 ID 可转储。

映射是帧租约，命令列表合并/移动时传递，直到既有 RenderFrame fence 退役才
释放。取消和编码失败先汇合任务；执行、呈现或 fence 失败沿用现有排空协议。
带租约的即时 Execute 使用局部 RenderFrame 提交/等待，不建立第二套 fence
系统；无租约的普通命令保持原即时行为。CPU 命令完成仍与 GPU 完成分开。

默认闲置预算为 256 MiB，可调整；仅淘汰已无租约的最早闲置项。返还/淘汰
使用链表转移，不在租约析构时分配。分配失败回滚先前取得的项。Close 不破坏
仍持有的租约，但禁止继续申请。管理资源的删除域身份必须相同；新 context
使用新池。无删除队列的旧路径仍要求 owner 释放和显式关停，产品不会跨 context
使用该池。统计分别记录实际创建、池命中、帧内别名、淘汰、闲置和租约估算字节。

验证入口：

```powershell
F:/npm/node.exe Build/FrameGraphValidation/build-native.mjs Debug
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --framegraph
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --render-frame
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --command-list
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --postprocess
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --pipeline
```

新增真实 GL 验证覆盖：非重叠目标共享同一纹理 ID、同节点重叠分离、格式/
尺寸/filter/wrap/mip 不兼容、复用开关精确像素一致、预热后零新增创建、GPU
超时期间另一帧取得不同存储、即时退役、Worker 并行编码及 owner 删除队列、
取消/编码/执行/fence 创建/等待失败、部分申请回滚、预算淘汰、带租约关池及
删除域重建。pooled-plan.txt 记录资源区间和物理映射。

Debug 六个 native 项目构建通过。新增图/池用例、既有 RenderFrame 的尾部/
呈现/fence/等待失败与 UI 所有权用例、完整命令列表的 256 draw/24 动态帧/
材质/历史/resize 回归通过。后处理与管线结果以本阶段冻结的 result.json 和
原 A5 哈希检查为准，验收目录为 Build/FrameGraphValidation/F3。

这一阶段完成通用池机制；生产效果的私有临时缓冲在 F4 迁移，产品帧跨视口
统一编译在 F5 完成。尚不能将池能力测试视为已经降低生产后处理内存。
