# FrameGraph F4：后处理临时资源迁移

2026-10-05，主线，Debug/x64；沿用 F0 的 MSVC 14.42 / SDK 22621、RTX 3070 Ti / NVIDIA 616.92 / OpenGL 4.5 环境。

PostProcessPipeline 删除私有 ping-pong；Bloom 删除全尺寸输出及十个采样缓存；
AutoExposure 删除构造时分配的亮度缓冲。开启效果时才创建逻辑临时目标，由唯一
RenderPlan 编译生命周期、RenderTargetPool 实现物理纹理/FBO。两个 1×1 曝光
历史继续属于视口，取消/失败沿用原历史 epoch 和 GPU 退役机制。

FullscreenCommandRecorder 使用逻辑输入、输出和具名 sampler。Main 按原顺序
准备并消费材质 single-use 属性，资源实现后绑定拥有型纹理；Worker 仅编码
不可变快照。导入目标冻结附件代际，纹理 owner 代际变化会拒绝绑定。保留 RGBA32F、
filter/wrap、亮度 mip、Bloom Karis/downsample/Load additive upsample/合成、
曝光计算、进出 blit 和效果顺序，没有改 shader 算法或图像容差。

Standalone Record 可注入共享池或关闭复用；生产 Build 使用 RenderFrontend
的池。默认每个前端仍拥有独立的 256 MiB 闲置预算，跨视口产品帧组合属于 F5。

## 验证

```powershell
F:/npm/node.exe Build/FrameGraphValidation/build-native.mjs Debug
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --framegraph
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --postprocess
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --pipeline
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --command-list
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --framegraph-memory
```

六个 native 项目构建通过。图/池契约及故障注入通过；16 种效果组合 × 1×1、
细长、奇数、正常尺寸，复用与完全不复用输出逐字节一致，默认预算下预热后零
新增分配。已提交效果链在视口 resize 后保持旧输出，取消新帧不释放旧帧租约。
既有后处理三种录制调度、14 帧主视图/阴影/cube 全 mip 精确一致，九个历史
A5 哈希匹配；自定义管线四种配置 × 三种调度精确一致，七种无效输入诊断通过。
命令列表的材质、历史、动态帧及 resize 回归结果保存在验收目录。

验收证据在 Build/FrameGraphValidation/F4；只冻结日志、结果、纹理清单和必要
读回数据，不再复制可执行程序、编译对象或 PDB。首次新增 resize 用例误用了
不存在的 Clear 方法，改为 Cancel 后通过；内存夹具的 getenv 在 /WX 下触发
弃用诊断，改用 Windows 环境查询后通过。未沿用失败运行的成功标记。

## 实际内存

与 F0 相同，创建三个生产后处理 owner、一个 1920×1080 目标，无场景副本。
实际查询 GL 各 mip 尺寸和通道位宽；下面是纹理存储，包含输出、fallback 和
曝光历史，不是全部 VRAM。进程 Private 受驱动/CRT 缓存影响，原始值另存 CSV。

| 阶段 | F0 私有缓冲 MiB | F4 默认独立池 MiB | 共享池、64 MiB 闲置预算 MiB |
|---|---:|---:|---:|
| 刚创建三个 owner | 48.0 | 0.00017 | 0.00017 |
| 全部效果关闭，1080p | 174.6 | 126.6 | 63.3 |
| 开 Bloom | 396.0 | 348.0 | 73.8 |
| 开曝光和 Bloom | 412.0 | 412.0 | 73.8 |
| 再关闭效果 | 412.0 | 412.0 | 73.8 |
| 显式将闲置预算清零 | — | 31.6 | 31.6 |
| 销毁目标/owner/池 | 0 | 0 | 0 |

默认独立池总闲置 398,870,448 字节，所有测量点退役后租约为零。共享 64 MiB
实验的闲置 44,232,000 字节、单帧租约峰值 132,956,816 字节；预算淘汰会导致
后续重新分配（累计 23 次创建、16 次淘汰），不能同时宣称这个小预算零分配。
该共享实验是三个 standalone 链依次执行/退役，不是产品 Editor 多视口一帧。

F4 已消除禁用效果的提前分配，但默认三池仍能保留三套完整工作集，因此不能
把共享实验的降幅当作当前 Editor 已实现的收益。F5 必须统一 Asset/Game/Scene/
Preview/工具/Picking/ImGui 的产品计划、声明最终消费者并跨视口复用；F6 再做
Debug/Release、产品配置矩阵、性能和内存验收。整个 FrameGraph 尚未完成。
