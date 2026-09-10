# A6 清理、总体并发与管线扩展验收

日期：2026-09-10。基线：935423e7cca76d18895ed3bc86ecd29a55374080。状态：A6 实现与验收完成。

## 交付与清理

- 已按用户“执行”的明确授权删除最终清单的 54 项：51 个旧 Renderer/Feature/Pass/Descriptor 源文件及 3 个旧链专属测试。旧继承链、RTTI 注册钩子、借用型场景 Descriptor、A1 StaticOpaqueRequest/Extract 适配入口均退出当前源码和测试。EVisibilityFlags 仍被新材质/提取器使用，保留。
- ToolDrawInput 显式携带模型/用户矩阵与拥有型 palette，ToolDrawCollector 的 CollectMesh/CollectModel 在 Main 提取并冻结材质/几何；延迟编码不读取 ECS、面板或源材质。SkinningUtils 复制工具蒙皮输入；Light 的阴影准备只接收相机位置。
- 统一 RenderFrontend 的 ExtractScene/ExtractFrame 服务产品及测试；新增 ComposeFrame 接收拥有型视图和固定计划，拒绝绕过提取 owner 的历史与后处理计划。RenderPlan 诊断带有资源编号或 Pass/View 上下文。
- inspection_pipeline.h 组合双视图与 Opaque/Transparent/SceneUI，声明多个输出附件；无需修改提取核心、任务协调器或后端。配置示例及其行为验证位于 Tests/RenderingCommands。
- 当前测试不再提取/编译历史 Renderer 源码；产品测试只链接当前 vcxproj 列出的对象文件，排除旧构建目录中的孤立对象。旧 Feature 注册专属测试随接口删除；排序、覆盖/回退、几何/绑定、目标生命周期、工具、Picking、后处理及失败恢复检查迁移到新入口。

清理证据：Build/a6-removal-manifest.json、Build/a6-removal-backup、Build/a6-cleanup-audit.json。54 份备份的大小与 SHA-256 全部复核；源码、测试和生成项目中旧入口引用为零，Debug/Release 的 OvCore/OvRendering 静态库均无旧 Renderer/Feature/Pass 成员。完整原实现继续由 Git 935423e 保存。

## 构建与实际回归

环境：Windows、VS2022/MSVC 14.42.34433、x64、OpenGL 4.5、NVIDIA RTX 3070 Ti/616.64。重新运行 Premake vs2022 后，Debug/Release 完整解决方案构建均退出 0，未发现编译/链接警告或错误。两份编辑器程序和四份游戏构建模板的 SHA-256 与对应 Bin 输出一致；刷新了 Debug Builder 中先前复制的旧 Release 游戏程序。

| 独立运行入口 | Debug | Release |
| --- | --- | --- |
| --command-list | 通过 | 通过 |
| --frontend | 通过 | 通过 |
| --bindings | 通过 | 通过 |
| --auxiliary | 通过 | 通过 |
| --postprocess | 通过 | 通过 |
| --pipeline | 通过 | 通过 |
| --parallel | 通过 | 通过 |
| --parallel-recording | 通过 | 通过 |
| --editor --command-list --editor-ui --product-loop | 通过 | 通过 |
| --editor-stress | 通过 | 通过 |
| --game-thread | 通过 | 通过 |
| --resource-executor | 通过 | 通过 |
| --render-thread | 通过 | 通过 |
| --render-frame | 通过 | 通过 |

每条命令使用 node Tests/Skinning/run-validation.mjs <配置> <入口> --vs <VS 安装目录>。完整可复制入口见 Tests/RenderingCommands/README.md。主矩阵证据为 Build/a6-tests-results.json；Debug 并行录制的最终结果以 Build/a6-retry-results.json 与对应 retry 日志为准。每项详细编译、运行、捕获和 result.json 均位于 Build/JobsValidation/Skinning。构建和复制证据为 Build/a6-build-results.json、Build/a6-build-*.log、Build/a6-build-copies.json。

验收中特别检查：

- 状态组合、真实 GL 绘制、源/材质/目标 resize 和销毁、资源代际、取消/失败/退役；24 帧真实蒙皮及动态阴影/反射/mipmap/曝光/Bloom/tonemapping/FXAA 的精确图像与历史恢复。
- 前端 8 个冷启动进程的串行/Worker、clean/poison、无探针/完整 UBO 读回对照；阶段排序、override/fallback 有独立明确的期望序列。绑定和辅助视图测试覆盖双 Worker 交错、正/逆序完成、失败、取消、部分提交、全部已提交任务汇合及单 Worker 恢复。
- 14 帧主视图、阴影和全部 cubemap mip 在串行、双 Worker、单 Worker 下按字节一致；后处理历史、渐进捕获及失败恢复遵循相同所有权契约。
- 自定义管线的 4 种配置 × 3 种调度，在两个目标上精确对照；开关实际改变图像；源/目标销毁和配置冻结得到验证。7 种无效 View、生产者、Stage、尺寸、资源和历史/postprocess owner 用法必须返回明确诊断。
- 12 帧 × 3 种调度 × 4 Pass 的 27,648 次共享材质绘制精确一致，另有 1,024 份源销毁后的材质输入与资源代际/最终释放检查。
- 实际 Editor 的三视口、工具、Picking、完整 UI 与 Application 循环；压力入口包含 96 帧、95 次可见面板访问、12 次场景/窗口/视图变化及六轮最小化/恢复。实际 Game 包含 32 对 Main/Render 图像、两帧容量、重启、退出与呈现失败。
- 产品测试在当前 RenderTaskCoordinator 的源码副本上插入有超时的入口闩锁；要求 Prepare 与 Record 各自有两个不同 Worker 同时进入，且 Worker 没有 GL context。单视图跳过闩锁，但最终缺少任一阶段的双 Worker 证据仍失败。生产源码与性能 benchmark 不插入该闩锁。

产品并发证据见 Build/a6-product-concurrency.json：Debug/Release 的 Editor 各观察到 3982 次 Prepare、4030 次 Record Worker 入口，Game 各为 396/396；四次运行均通过两个阶段的不同 Worker 重叠和无 GL context 断言。

A5 的 134 份原始数据大小及 SHA-256 固定在 Tests/RenderingCommands/a5-image-baseline.json；本机原始字节另存 Build/a6-a5-baseline。本次当前模式累计核对 86 份对应文件，全部一致。不重编译旧链，不更新基线，不放宽像素容差。已退役模式的数据仍保留在历史清单中。

## 问题处理与结论边界

首轮测试未能自动发现 VS 安装，尚未编译即失败；改为显式 --vs 后执行真实回归。新增单 Worker 录制对照曾因变量位于错误函数作用域而编译失败，修复后重跑通过。失败记录保留；不能把旧 run.log 或语法检查当成本次通过证据。

模块职责、输入输出与状态 owner 已逐项映射于 RENDERING_ARCHITECTURE.md。剩余串行步骤是稳定 Main 提取、single-use 消费顺序、依赖结果合并、动态历史 owner 及固定 GL owner；Prepare/Record 不借用可变 ECS 或材质，不由旧 Feature 共享状态维系顺序。CPU 任务终态、成功发布、命令执行及 GPU/历史完成分别判断。

render_commands 的直接执行/帧提交及 render_thread 的 Main/Render owner 选项消费同一份显式命令。parallel_recording 与 render_thread 默认仍为 false。本次未做新的性能结论，也未新增效果、流水线编辑器、脚本 SRP、RHI/Vulkan 或 fiber/job graph。后续独立路线见 MULTITHREADING_NEXT_STEPS.md。
