# 渲染架构 A4：辅助视图与显式资源计划

日期：2026-09-10。状态：A4 实现与 Debug/Release 验收完成；本地提交受执行环境阻止，尚未提交。

## 数据与线程归属

SceneFrameExtractor 在主线程稳定读取期规划阴影、反射和主视图。场景访问仍集中在 Extract；辅助 Pass 接收拥有型快照，Worker 不读取组件、源材质、相机或探针 owner。阴影资源创建/resize 使用既有 Light/Framebuffer 和 ResourceExecutor 边界；没有增加 GPU 资源后端。

SceneFrame 是一次多视图准备/录制请求的拥有型容器，不代表已提交的产品 RenderFrame 或 GPU fence。对外只有只读计划、快照和发布数据；PrepareFrame/RecordFrame 消费 unique_ptr，失败不会发布半帧结果。

ShadowCaster 保持 SHADOW 可见性、源材质 caster 规则、专用 SHADOW_PASS 或阴影 fallback、双面深度写入与场景收集顺序。ReflectionCapture 保持 REFLECTION 可见性、排除 UI、原始材质捕获标志、不透明/透明排序和 REFLECTION_PASS 回退。辅助视图沿用主视图灯光裁剪结果的语义（含显式 frustum override），几何裁剪与相机值各自独立。反射接收端使用拥有型探针输入，保留 local 优先、最近距离、球/OBB 影响范围与相同距离后者覆盖规则。

singleUse 消费仍属于 Extract。多视图共用一次主线程材质签名缓存，按 GPU 计划顺序消费；独立纹理/矩阵覆盖不写源 Material。跨反射面不能重新初始化消费缓存。

## CPU 任务与 GPU 顺序

主线程规划资源/历史并依序提取 → 所有 View 独立 Prepare → 全部任务汇合及只读发布 → 所有 View 私有 Record → 全部任务汇合 → 固定 Shadows、ReflectionCapture、MainView 顺序合并 → 既有 RenderCommandList/RenderFrame 执行与退役。

跨视图任务使用相同候选、排序、绑定及录制函数；Worker 内部采用串行局部调用，不向同一线程池嵌套提交。原 A3 单视图范围并行入口继续保留。协调器在提交前分配结果槽及 handle 容量，任何提交/任务/取消失败均先 WaitAllPassive 汇合，再检查输出和返回错误。

## 资源与历史

RenderPlan 是固定有序 Pass 列表，包含 View/Stage、输出目标、读写资源、cubemap 面及生成 mipmap 的步骤，不自动重排。资源记录逻辑身份、场景代际、保留的存储代际、尺寸、子资源和当前帧/显式导入/历史来源。Framebuffer::Snapshot 提供只读附件元数据，校验实际目标及面，避免只比较调用者提供的标签。

校验缺失生产者、错误 View/Stage/场景代际、输出尺寸、当前附件反馈、缺失面以及不完整 cubemap 的 mipmap 生成。显式禁用阴影且无导入时给出中性接收结果；未完成的 ON_DEMAND 反射采用中性 fallback。

渐进反射继续由 CReflectionProbe 持有面游标、ping-pong 和 captureHistory。SceneFrame 在推进游标前登记既有 History；主线程按原协议推进候选发布，命令失败/取消/丢弃使 epoch 失效，后续帧完整重建，失效后继在执行 GPU 前拒绝。CPU 准备完成不解释为 GPU 完成。

保留 A0 记录的行为：阴影生产端遍历全部定向投影光，接收端选择首个；本次没有顺便修复 lightIndex 未递增的问题。

## 验证入口

- node Tests/Skinning/run-validation.mjs Debug --auxiliary --vs <VS>
- Release 同理。
- 回归沿用 --bindings、--stages、--command-list。

A4 对照使用独立进程的旧命令、旧即时、新串行、两 Worker、单 Worker，比较动画主画面、阴影深度及每级已发布 cubemap mip。另覆盖实际 Worker 重叠、正反完成顺序、Prepare/Record 失败、取消、部分提交、历史后继拒绝、部分 GPU 执行失败、重建、源销毁和目标/probe resize。

已完成的定向排查：初始真实模型用例的阴影范围未覆盖模型，深度目标只有清除值；扩展测试增加了非空内容检查，并调整测试定向光的位置/范围，保留逐字节比较。实际内容暴露出 16 个深度采样值的末位差异。逐位矩阵检查定位到 Camera/ReferenceOrValue 拷贝会通过 FTransform 拷贝构造重建世界矩阵和归一化旋转，而 Light 中的阴影相机矩阵缓存已经在拷贝前计算。新路径保存生产端缓存的 view/projection 数值，主/反射相机仍由统一 ViewPreparer 从值计算。旧 Camera/Light 和浮点容差均未修改。

最终 Debug/Release A4 对照均已通过：每配置 5 个独立进程 × 14 帧；每进程主画面 344064 字节、阴影深度 86016 字节、cubemap 各级 mip 458304 字节。覆盖两个动画角色、透明反射、缺失 SHADOW_PASS 的实际 fallback、第二个定向投影光、局部探针和盒投影。所有图像逐字节一致；所有并发/失败/源销毁测试通过。

最终 A4 运行链接本轮完整构建生成的 Core/Rendering 等实际库。仅 RenderTaskCoordinator 编译副本加入测试闩锁和异常注入；提取、准备与录制业务没有替代实现，生产代码没有测试钩子。12 份旧 Renderer/Pass/Feature、Camera/Light 和 Shader 源码与 `2678dcf` 的文本内容一致，证据见 `Build/a4-oracle-source-evidence.json`。

完整 Debug/Release 构建通过；六份 Editor/Game 产品输出及 Builder 模板已同步，SHA-256 一致，见 `Build/a4-product-build-evidence.json`。全部最终回归通过。

| 验证 | Debug | Release | 结果 |
| --- | --- | --- | --- |
| 全量解决方案构建 | 通过 | 通过 | Editor/Game 及库完整构建 |
| A4 `--auxiliary` | 通过 | 通过 | 每配置 5 进程 × 14 帧，主画面/阴影/cube mip 逐字节一致 |
| A3 `--bindings` | 通过 | 通过 | 每配置 10 进程 × 8 帧，旧命令/新串行/Worker 像素及绘制数据一致 |
| A3 `--bindings --frontend-immediate` | 通过 | 通过 | 每配置 10 进程 × 8 帧，与旧即时入口精确一致 |
| A2 `--stages` | 通过 | 通过 | 每配置 10 进程 × 8 帧，阶段排序/绑定污染/并发准备回归 |
| 既有 `--command-list` | 通过 | 通过 | 资源/相机/附件回归，24 帧动态效果对照与失败恢复 |

架构图像矩阵合计 70 个独立进程、620 帧；不计诊断重试及另行运行的命令列表回归帧。后者覆盖旧完整路径的阴影、反射、mipmap、曝光、Bloom、Tonemapping、FXAA 即时/延迟精确对照，并验证取消、部分执行失败、放弃及后继取消后的恢复。A4 尚未声称完成新前端的产品 RenderThread/三视口集成验收，该工作随 A5/A6 接入。

最终证据：

- `Build/a4-final-validation-evidence.json`：封存构建、10 组回归、各 result.json、源码/旧对照/产品哈希。
- `Build/a4-final-sequential-build-evidence.json` 与 `Build/a4-final-{debug,release}-sequential-build.log`：全量构建。
- `Build/a4-final-suite-evidence.json` 与 `Build/a4-final-{debug,release}-*.log`：最终回归命令、时间和退出码。
- `Build/JobsValidation/Skinning/{Debug,Release}-auxiliary-command-list/`：五模式结果、逐帧二进制图像及并发/历史日志。

使用 Hostx64/x64 MSVC 14.42，解决方案构建 `/m:1 /p:CL_MPCount=1 /p:ForceRebuild=true /p:TrackFileAccess=false`；测试独立链接当前配置库。`git diff --check` 通过。

## 尚留迁移边界

旧 SceneRenderer 与其 Feature/Pass 仍服务尚未迁移的产品调用者和 A0 对照。新多视图入口独占本次网格管线，不能与旧入口重复运行。后处理、编辑器覆盖层、Picking 与产品面板接入属于 A5，旧继承链与兼容入口最终在 A6 清零。

## 本地提交状态

基线 HEAD 为 `2678dcf32ecc728a7ec0dc3032c62b0530d6ea1d`。本次精确暂存 27 个 A4 文件时，Git 返回 `Unable to create .git/index.lock: Permission denied`；受控提权入口随后在启动阶段因 Windows CET 错误退出（`-2146233082`），没有执行 Git。因此实现与验证已完成，但本记录不宣称已 commit。

已准备 `Build/a4-commit.mjs` 和 `Build/a4-commit-state.json`。在仓库目录的可写终端运行 `node Build/a4-commit.mjs`；脚本先检查 HEAD、空暂存区以及这 27 个文件的 SHA-256，再精确 add、diff --cached --check、提交 `feat(rendering): complete A4 auxiliary views and resource plans`。它不会包含既有的 `.zcode/`、`CODEX_CHANGE_LIST_TEST.txt`、`Docs/` 或 `Examples/`，也不会 push。提交完成后建议开启新会话继续 A5。
