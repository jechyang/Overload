# Frame Graph 收尾与交接档案

本档案记录主线实际完成的功能、验证证据和未覆盖的范围。最后更新：2026-10-06。
开发位置：`H:\overload\Overload`，分支 `main`；所有提交仅保存在本地，不 push。

## 已有核心与调试功能

F0–F6 已完成：固定顺序的 OpenGL 产品计划、资源与子资源访问契约、内容版本、
初始化/依赖校验、生命周期分析、物理目标复用、池预算与自动回收、GPU 退役保护，
以及 Editor/Game 与多线程集成。详见 `FRAMEGRAPH_F6_VALIDATION.zh-CN.md`。

Editor 已有依赖图、资源版本流、资源内容预览、GPU Pass 耗时与热力图、物理槽/
逻辑资源生命周期时间轴。此前最后一项提交：`b51a0be6`。

## 本次收尾进度

| 项目 | 实现 | 验证 | 状态 |
|---|---|---|---|
| CPU 各阶段耗时 | Compile、Realize、Prepare、Capture diagnostics、Record views、Record operations、Seal；成功录制后发布不可变快照 | Debug/Release，Main/Render 原生面板、冻结 resize、恢复、即时 Seal、完整发布、关闭；各 24 个暖帧样本 | 完成 |
| 内存来源定位 | 五种互斥池分类、逐目标尺寸/格式/年龄/pin、输出/历史/导入/临时的计划角色估算 | 分类/条目原子核对；旧尺寸在途、pins-only、预留/闲置、TrimIdle、冻结图时实时池 resize | 完成 |
| 大图定位与导出 | 折叠池控制、两种图扩大画布、名称/P#/R# 查找聚焦、文件与剪贴板 JSON | 实际 ImGui 交互；两种模式各三份报告，由 Node 独立解析、转义/全图/CPU/池/GPU 绑定核对；写出错误反馈 | 完成 |

CPU 数据为生产线程各阶段墙钟耗时，包含同步等待的任务；Realize 含分配与 GL
资源所有者等待。阶段合计不包含 Extract/Compose、预览转换、提交/GPU/Swap，
也不等于整帧时间。Editor 最终编码在 RenderFrame::Seal 执行，单独标记 Deferred，
避免将尚未执行的编码计入零耗时；立即录制路径包含 Seal。
关闭诊断时不读取阶段计时器，不建立新诊断快照。

内存分类为 Active latest / Active older / Pins only / Idle reserve / Idle unused。
活跃 lease 与 pin 可能同时存在，只计在活跃类一次。Active older 可能为相同尺寸
的较早在途帧，不能直接判为泄漏。池明细在同一锁内复制成值，不查询 GL、不增加
lease，也不保留资源。输出/历史/导入只描述当前计划使用的逻辑资源，不包含其他
历史环/项目资源；这些数量不与池总量相加，不能当作全进程内存或实际 VRAM。

JSON schemaVersion=1，时间 ns / 内存描述估算字节。导出不受筛选影响，保留
完整 Pass/资源/版本访问/依赖、CPU 阶段、实时池类别与目标。GPU 数组只对应
相同快照对象，否则为 null；冻结快照与实时池通过 poolIsLive 明确区分。

## 验证与证据

Debug/Release 的 OvRendering、OvCore、OvEditor、OvGame 构建通过。最终验证：

```powershell
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --framegraph
F:/npm/node.exe Tests/Skinning/run-validation.mjs Release --framegraph
F:/npm/node.exe Tests/Skinning/run-validation.mjs Debug --framegraph-editor --framegraph-baseline
F:/npm/node.exe Tests/Skinning/run-validation.mjs Release --framegraph-editor --framegraph-baseline
```

四次均 exit 0，原 F0 基线完全相同。核心入口保留物理复用、回收、退役、取消/
失败、预览、GPU 时间戳和逐像素对照；编辑器入口保留 27 组原生/拥有型 UI、
产品视图/交互和所有已有图工具回归。预算 Apply 在下一产品边界消费，实际 UI
测试覆盖此延迟；内存 resize 使用 Edit 中实际渲染的 Asset View 后处理链。

证据目录为 `Build/JobsValidation/Skinning/` 中 Debug/Release 的
`*-framegraph-command-list/` 与 `*-framegraph-editor-command-list-editor-ui/`。
各目录 result.json 保存验证元数据；后者 current/run.log、framegraph-*-report.json、
framegraph-*-gpu-report.json 以及 memory/cpu-stages/expanded-canvas/located-resource
的原生 RGBA 图像保存具体证据。大体积构建/图像留在 Build，不提交进 Git。

最终原生 UI 24 暖帧的阶段中位数（54 Pass / 35 资源，单位 ms）：

| 配置与 consumer | Compile | Realize | 阶段合计 |
|---|---:|---:|---:|
| Debug Main | 1.935 | 0.134 | 11.639 |
| Debug Render | 1.924 | 0.202 | 11.669 |
| Release Main | 0.154 | 0.017 | 1.081 |
| Release Render | 0.139 | 0.025 | 1.051 |

这是带 UI 验证的小样本观察，配置同时运行，不能用作独占机器性能基准。
目前不引入结构缓存；Release 编译成本尚未证明值得承担缓存失效复杂度。

## 尚未包含的独立扩展

- 自动 Pass 裁剪与重排：目前保留产品既定顺序及其副作用；未宣称自动调度。
- 通用 Buffer 资源图及其别名/访问验证。
- Vulkan/D3D12 barrier、异步计算、多队列及跨队列同步。
- 图结构编译缓存：先测量编译成本与稳定性，再决定收益是否足够。
- 驱动实际 VRAM、全进程 CPU 内存和磁盘占用：Frame Graph 描述估算不覆盖这些。

## 后续方向

本轮 Frame Graph 三项收尾全部完成，没有承诺内的待办。上面的独立扩展仍未实现。
用户已选择下一方向为「实际场景渲染性能」；先在当前主线对 PerformanceLab 的
六组 A/B 场景独占采样，分别定位 Update/Extract/Prepare/Record/Execute/GPU，
把结果、优化优先级和未做项另存为 `RENDERING_PERFORMANCE_HANDOFF.zh-CN.md`。
