# 渲染架构与命令回归

当前测试只编译新 RenderFrontend、显式 View/Stage、RenderPlan 和拥有型命令实现。A6 移除了旧 Renderer/Feature 链及内嵌历史 oracle；历史代码和验收过程保留在 Git 935423e 及根目录 A0–A5、MULTITHREADING_* 记录中。

先生成项目并完整构建相应配置，再从仓库根目录运行：

~~~bat
node Tests\Skinning\run-validation.mjs Debug --command-list
node Tests\Skinning\run-validation.mjs Debug --bindings
node Tests\Skinning\run-validation.mjs Debug --auxiliary
node Tests\Skinning\run-validation.mjs Debug --postprocess
node Tests\Skinning\run-validation.mjs Debug --pipeline
node Tests\Skinning\run-validation.mjs Debug --parallel-recording
node Tests\Skinning\run-validation.mjs Debug --editor --command-list --editor-ui --product-loop
node Tests\Skinning\run-validation.mjs Debug --editor-stress
node Tests\Skinning\run-validation.mjs Debug --game-thread
~~~

Release 使用相同命令，将 Debug 替换为 Release。可用 --vs 指定 VS2022 安装目录。运行器在 Build/JobsValidation/Skinning 下产生独立编译参数、编译日志、运行日志、捕获数据和 result.json；失败重跑先移除旧成功标记。窗口隐藏，编辑器测试使用隔离的项目和配置目录。

| 入口 | 主要行为检查 |
| --- | --- |
| --command-list | 64 状态组合、256 个真实 GL 绘制分支；材质/资源代际；目标 resize/销毁、cube face；命令取消/失败/历史；真实蒙皮、动态阴影/反射/曝光/Bloom/FXAA |
| --frontend | 静态场景冷启动；源销毁、无 GL 准备；完整 UBO 绑定检查与污染恢复 |
| --stages / --bindings | 明确的阶段排序/override/fallback 期望；蒙皮/灯光/绑定；范围 Prepare/Record 的双 Worker 交错、逆序、失败/取消/部分提交与汇合 |
| --auxiliary / --postprocess | 主视图、阴影及全部 cubemap mip 的串行/双 Worker/单 Worker 精确对照；渐进捕获、曝光历史、失败恢复、源及目标生命周期 |
| --pipeline | inspection_pipeline.h 的双视图配置；选择 Opaque/Transparent/SceneUI；声明多个附件；冻结配置、源销毁、无效资源/视图/Stage/尺寸诊断 |
| --parallel-recording | 延迟编码依赖图和共享材质压力；串行、单 Worker、多 Worker，确定命令顺序与资源退役 |
| --editor --command-list --editor-ui --product-loop | Scene/Game/Asset 三视口、Grid/Outline/Gizmo/调试对象、真实 Picking、对象删除/场景替换、完整 UI 和产品循环 |
| --editor-stress / --game-thread | 实际产品 Main/Render owner 对照；在途帧、resize/最小化、场景/模式切换、关闭和呈现失败；Prepare 与 Record 在不同 Worker 上实际重叠 |

命令/资源后端的独立入口继续有效：--geometry、--material、--snapshots、--target-state、--ui-commands、--render-frame、--render-thread、--log-dispatch、--resource-lifetime、--texture-registry、--imgui-lifetime、--gpu-profiling、--resource-executor。它们分别验证底层接口，不是第二套场景准备路径。原蒙皮 --parallel、--asan 和 --mutex-deque 入口仍使用统一渲染前端。

--effects-oracle、--frontend-immediate、--draw-inputs、--draw-packets、--frame-inputs 已退役；相应的有效行为检查由上表新入口承担，旧 Feature 白名单/RTTI 注册等已删除接口的拒绝测试不再保留。

## 图像对照和线程观测

A5 的同机精确基线由 a5-image-baseline.json 固定大小和 SHA-256。运行器比较本次输出文件，并同时要求串行/Worker 的所有像素与逻辑绘制数据一致；不从旧 Git 编译第二套实现。CPU/GL 环境变化造成差异时应定位原因，不能重写基线或放宽像素容差以通过测试。该清单是历史证据，不代替源所有权、无 GL Worker、失败汇合与 GPU 退役检查。

测试探针仅插入 Build 下的当前源码副本；不修改生产调度/准备算法。产品探针在 PrepareViews 和 RecordViews 的实际 Worker 入口设置有超时的双任务闩锁，并检查线程身份和 GL context。Prepare/Record 完成顺序不决定 GPU Pass 顺序；后者由固定计划决定。图像一致和重叠计数不表示性能提升。

## 管线配置示例

inspection_pipeline.h 是小型 C++ 配置实例：先在主线程选择阶段并提取两个视图，声明 Pass 的输出附件，再调用 ComposeFrame、PrepareFrame、RecordFrame 和已有 RenderFrame。无需更改任务协调器、场景提取核心或执行后端。ComposeFrame 拒绝需要未提供历史 owner 的配置；阴影/反射/后处理历史使用现有 ExtractFrame 入口。

当前没有流水线编辑器、脚本 SRP SDK 或新渲染效果。模块与 owner 边界见根目录 RENDERING_ARCHITECTURE.md；A6 最终状态以 RENDERING_ARCHITECTURE_A6_VALIDATION.md 为准。
