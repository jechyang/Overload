# 渲染架构与命令回归

当前测试只编译新 RenderFrontend、显式 View/Stage、RenderPlan 和拥有型命令实现。A6 移除了旧 Renderer/Feature 链及内嵌历史 oracle；历史代码和验收过程保留在 Git 935423e 及根目录 A0–A5、MULTITHREADING_* 记录中。

先生成项目并完整构建相应配置，再从仓库根目录运行：

~~~bat
node Tests\Skinning\run-validation.mjs Debug --command-list
node Tests\Skinning\run-validation.mjs Debug --bindings
node Tests\Skinning\run-validation.mjs Debug --auxiliary
node Tests\Skinning\run-validation.mjs Debug --postprocess
node Tests\Skinning\run-validation.mjs Debug --pipeline
node Tests\Skinning\run-validation.mjs Debug --framegraph
node Tests\Skinning\run-validation.mjs Debug --framegraph-editor --framegraph-baseline
node Tests\Skinning\run-validation.mjs Debug --parallel-recording
node Tests\Skinning\run-validation.mjs Debug --editor --command-list --editor-ui --product-loop
node Tests\Skinning\run-validation.mjs Debug --editor-stress
node Tests\Skinning\run-validation.mjs Debug --editor-camera
node Tests\Skinning\run-validation.mjs Debug --game-thread
~~~

Release 使用相同命令，将 Debug 替换为 Release。可用 --vs 指定 VS2022 安装目录。运行器在 Build/JobsValidation/Skinning 下产生独立编译参数、编译日志、运行日志、捕获数据和 result.json；失败重跑先移除旧成功标记。窗口隐藏，编辑器测试使用隔离的项目和配置目录。
运行前将 Resources/Engine 和 Resources/Editor 中有差异的文件同步到当前配置的
Build/Data，防止单独构建项目时遗漏资源发布、误用旧 shader。

| 入口 | 主要行为检查 |
| --- | --- |
| --command-list | 64 状态组合、256 个真实 GL 绘制分支；材质/资源代际；目标 resize/销毁、cube face；命令取消/失败/历史；真实蒙皮、动态阴影/反射/曝光/Bloom/FXAA |
| --frontend | 静态场景冷启动；源销毁、无 GL 准备；完整 UBO 绑定检查与污染恢复 |
| --stages / --bindings | 明确的阶段排序/override/fallback 期望；蒙皮/灯光/绑定；范围 Prepare/Record 的双 Worker 交错、逆序、失败/取消/部分提交与汇合 |
| --auxiliary / --postprocess | 主视图、阴影及全部 cubemap mip 的串行/双 Worker/单 Worker 精确对照；渐进捕获、曝光历史、失败恢复、源及目标生命周期 |
| --pipeline | inspection_pipeline.h 的双视图配置；选择 Opaque/Transparent/SceneUI；声明多个附件；冻结配置、源销毁、无效资源/视图/Stage/尺寸诊断 |
| --framegraph | 子资源/版本/Load/Store/反馈契约，真实池复用、外部 UI pin、GPU 退役及失败回滚；四视口共享、禁用复用逐字节对照；16 效果组合 × 四种尺寸、暖池零分配；诊断数据不持有 GPU 存储，TrimIdle 保留活动 lease/pin 和预算；80 组 R32F/RGBA32F 对照，全部 mip 红通道/渐进曝光历史/最终 HDR 浮点逐位一致，实际 GL 存储减少 75% |
| --framegraph-editor --framegraph-baseline | 实际 Main/Render owner 多视口图的诊断面板：默认关闭、Pass/资源版本图、每条读写边的版本和节点、v0/Load、真实版本节点点击、非版本总览、筛选和隐藏选项详情、冻结/恢复/resize、线程归属、缓存预算/闲置清理、关闭释放；同时执行真实产品和 27 组完整/单面板原生/拥有型 UI 精确对照，检查 F0 两份冻结图像 |
| --parallel-recording | 延迟编码依赖图和共享材质压力；串行、单 Worker、多 Worker，确定命令顺序与资源退役 |
| --editor --command-list --editor-ui --product-loop | Scene/Game/Asset 三视口、Grid/Outline/Gizmo/调试对象、真实 Picking、对象删除/场景替换、完整 UI 和产品循环 |
| --editor-camera | 含 OverlayUI 时，未选中对象/选中 Canvas 的 Scene 相机旋转会改变网格和辅助图形像素，屏幕 UI 保持固定；注入右键事件验证控制器不依赖对象选择（不覆盖操作系统鼠标焦点） |
| --editor-stress / --game-thread | 实际产品 Main/Render owner 对照；在途帧、resize/最小化、场景/模式切换、关闭和呈现失败；Prepare 与 Record 在不同 Worker 上实际重叠 |

命令/资源后端的独立入口继续有效：--geometry、--material、--snapshots、--target-state、--ui-commands、--render-frame、--render-thread、--log-dispatch、--resource-lifetime、--texture-registry、--imgui-lifetime、--gpu-profiling、--resource-executor。它们分别验证底层接口，不是第二套场景准备路径。原蒙皮 --parallel、--asan 和 --mutex-deque 入口仍使用统一渲染前端。

--effects-oracle、--frontend-immediate、--draw-inputs、--draw-packets、--frame-inputs 已退役；相应的有效行为检查由上表新入口承担，旧 Feature 白名单/RTTI 注册等已删除接口的拒绝测试不再保留。

## 图像对照和线程观测

A5 的同机精确基线由 a5-image-baseline.json 固定大小和 SHA-256。运行器比较本次输出文件，并同时要求串行/Worker 的所有像素与逻辑绘制数据一致；不从旧 Git 编译第二套实现。CPU/GL 环境变化造成差异时应定位原因，不能重写基线或放宽像素容差以通过测试。该清单是历史证据，不代替源所有权、无 GL Worker、失败汇合与 GPU 退役检查。

FrameGraph F0 在改造前已记录旧 A5 Editor 场景图像不符，冻结数据见
FRAMEGRAPH_F0_BASELINE.zh-CN.md。Editor 产品/线程/压力/诊断面板入口可显式加
`--framegraph-baseline`，要求 editor-command-frames.bin 和 editor-game.rgba 与开发前数据精确
相等，并在 result.json 保留已存在的 A5 差异。其他 A5 文件仍照常比较；历史清单
没有覆盖。新清单 framegraph-image-baseline.json 仅含已冻结的大小/SHA256。

完整/单面板 UI 在明确的 RGBA8 目标上逐字节对照，并验证原生重放自身稳定。
本机默认窗口的 4 倍 MSAA 读回曾在相同原生 DrawData 重放中出现单像素差异；
未增加容差。实际产品窗口对照仍由 EditorFrame/EditorThread/GameThread 覆盖。
原生与拥有型 UI 的切换仅更换字体纹理 owner，测试检查不重复创建设备程序。

`--editor-stress` 和 `--game-thread` 同时比较实际产品图的串行、单 Worker、双
Worker，以及 Main 即时/提交和 Render owner 提交的精确图像；RenderThread
与即时模式的无效组合仍拒绝。独立视口调用用于图像对照，生产入口始终统一组合。

`--framegraph-memory` 查询实际纹理级别及 Windows 进程内存。
`OV_FRAMEGRAPH_MEMORY_PRODUCT=1` 使用一个产品图/默认预算共享池，四阶段各
6 个样本，记录 product-timings.csv、pool.csv、memory.csv 和 textures.csv；
默认模式保留单独管线测量，以便与 F0/F4 对照。估算纹理字节不包含全部 VRAM。
`OV_FRAMEGRAPH_MEMORY_SINGLE=1` 改为单视口链；
`OV_FRAMEGRAPH_MEMORY_RESERVE2=1` 测量产品池两套预留。产品模式额外记录全部
效果、两帧在途、1080p/720p resize 及恢复后的存储峰值；每阶段预热后不得新增分配。
`OV_FRAMEGRAPH_MEMORY_DISTINCT_OUTPUTS=1` 为每条链创建独立持久输出，测量实际
三视口的额外占用；默认共享单输出保留 F0 隔离测试的输入口径。
开关仅接受字符串 `1`，空环境变量不会启用对应模式；pool-mode.txt 记录实际参数。
产品模式共七组各 6 个时间样本，首帧预热，其余帧要求零新增临时目标；
encodeExecuteUs 包含执行及 GPU 退役等待，不是纯 GPU 时间。

测试探针仅插入 Build 下的当前源码副本；不修改生产调度/准备算法。产品探针在 PrepareViews 和 RecordViews 的实际 Worker 入口设置有超时的双任务闩锁，并检查线程身份和 GL context。Prepare/Record 完成顺序不决定 GPU Pass 顺序；后者由固定计划决定。图像一致和重叠计数不表示性能提升。

## 管线配置示例

inspection_pipeline.h 是小型 C++ 配置实例：先在主线程选择阶段并提取两个视图，通过 SceneFrame.AddView 声明附件和实际采样，再调用 PrepareFrame、RecordFrame 和已有 RenderFrame。无需更改任务协调器、场景提取核心或执行后端。ComposeFrame 拒绝需要未提供历史 owner 的配置；阴影/反射/后处理历史使用现有 ExtractFrame 入口。

当前没有流水线编辑器、脚本 SRP SDK 或新渲染效果。模块与 owner 边界见根目录 RENDERING_ARCHITECTURE.md；A6 最终状态以 RENDERING_ARCHITECTURE_A6_VALIDATION.md 为准。

Frame Graph 面板使用与诊断数据边界见根目录 FRAMEGRAPH_DIAGNOSTICS_VALIDATION.zh-CN.md。
`--framegraph` 还覆盖一次性资源预览：版本生产点与 alias 覆盖、mip/cube 面、
深度 texture/renderbuffer、HDR/通道转换、GL 状态恢复、范围校验和图像独立于源
资源的寿命。`--framegraph-editor --framegraph-baseline` 在 Main/Render 模式验证
实际 Preview UI、GPU 完成后发布、冻结 resize、取消/失败、Texture Debugger
跳转及清除/关闭；原 F0 图像和命令帧基线保持不变。
计时验收也包含在上述两个入口：精确时间戳 oracle、GPU timeout/availability
不阻塞、取消/查询与帧失败、查询池暖机/容量/域重建/Worker 删除；编辑器实际
采样开关、热点行选择、测量图冻结、resize、恢复计划顺序、关闭与精确视口像素。
编辑器入口还验证 GPU heat 的两种真实画布、原生节点颜色和资源颜色保持、
同规模新图拒绝旧值、测量图恢复、实际按钮与节点点击、冻结 resize 和取消冻结。
生命周期时间轴检查在 framegraph_lifetime_editor_validation.inl：实际产品逐 Pass
内存曲线/编译峰值、同槽非重叠条、原生条与曲线点击、筛选/峰值、逻辑/外部资源、
缩放/水平滚动、详情双向跳转、冻结 resize/恢复，另测 Export/未使用/空图边界。

自动曝光亮度缓冲的格式、数值对照与多视口内存实测见根目录 FRAMEGRAPH_LUMINANCE_VALIDATION.zh-CN.md。
