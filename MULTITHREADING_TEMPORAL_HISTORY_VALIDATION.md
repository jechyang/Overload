# 阶段 2 第九增量：动态历史的失效与重建

日期：2026-09-06。前置提交：4dd8ee7（拥有资源的帧命令序列）。

## 实现范围

RenderCommandList 新增生产者持有的 History 游标。每个使用相同历史的列表记录上一列表的完成标记，以及共享的失效标记；同一列表多次使用只登记一次。UseHistory 必须在推进 CPU 历史之前调用。跨帧只保留小型原子状态，不持有场景、组件或 effect，也没有消费端回调生产对象。

取消、录制失败、执行异常，以及未消费列表的析构都会使本列表涉及的历史失效。消费前先检查所有历史与前序：失效或前序未完成时，在执行任何操作之前拒绝整个列表；失败沿多种历史之间的依赖继续传播。列表不能乱序消费，也不在消费者内部等待前序。移动转移完成标记，移动后的源析构不会取消目标。

生产者下一次登记失效历史时建立新的历史代次，并得到“需要重新初始化”的返回值。旧代次中已经准备的列表保持失效，不能在重建后重新写入资源。完成状态只表示命令调用成功，仍不是 GPU fence。列表自身、History 游标和 GPU 资源仍遵守单一生产者/消费者与外部交接规则；这里的原子状态不是完整渲染队列。

ABaseRenderer::UseRenderHistory 统一录制和即时路径。即时模式遇到健康但未完成的前序会拒绝抢先使用；遇到失效历史会要求重建。原即时路径从未登记异步历史时没有新增堆分配。

反射在选择捕获面前登记历史。恢复时重置两个缓冲迭代器、有效标记和捕获面索引，并强制完整六面重建；这也恢复已经在准备期消耗的 ON_DEMAND 请求。部分执行过的 GPU 纹理无法用 CPU 索引回滚，因此恢复会覆盖全部面并重新生成 mipmap。

自动曝光在选择 ping-pong 缓冲前登记历史。恢复时重置缓冲索引、上一时刻与首帧标记；首帧跳过渐变，不采样尚未生成的曝光历史。后续健康帧继续原渐变规则。

## 验收入口

先完整构建 Debug/Release，再运行 Tests/Skinning/run-validation.mjs 的 --command-list；可用 --vs 指定 VS2022。该模式新增：

- 顺序依赖、同列表重复登记、乱序拒绝、即时模式拒绝超车；取消、录制失败、执行异常和析构放弃；依赖链跨历史传播；移动和源 History 销毁后的消费。
- 每帧注入丢弃的真实 SceneRenderer 动态帧，轮换取消、执行完动态 pass 后抛错、未消费析构、取消后拒绝已录制后继。随后录制恢复帧，24 帧与即时基准严格逐字节比较。
- 恢复分支开启渐变曝光，并使用 ON_DEMAND + ONE_FACE 反射；它必须在失效后重新走曝光首帧及完整六面捕获，才能与基准结果一致。
- 复用原资源销毁后消费、录制 GPU 副作用拦截、目标/帧输入/packet 和正常动态图像回归。

## 验收结果

- Debug/Release 完整解决方案构建均退出 0，0 警告、0 错误；程序、模型、shader 共 10 项复制核对全部一致。
- 两配置 --command-list 全部通过。24 帧恢复场景与即时基准严格逐字节一致，另保留原 24 帧即时/延迟与动态即时/重复/packet/延迟一致性；恢复场景最终 76,795 个非背景像素、23 次动画图像变化。
- 两配置 --draw-inputs 的旧基准/当前材质状态、GL 缓冲调用与几何/蒙皮图像全部精确一致。
- 两配置 --editor --draw-packets 的真实 Edit/Play/Pause/8 次单步/恢复/Stop、24 组视图顺序图像、8 次延迟切场景/resize 和项目批次统计通过。该项仍是同次 draw packet 回归，不是 Editor 整帧延迟消费。
- git diff --check 通过。本轮未增加 ASan、Tracy 采集或独占性能测量，不据测试运行时间声称性能提升。

证据位于 Build/JobsValidation/：msbuild-{debug,release}-history.log、history-{debug,release}.log、history-regression-{debug,release}.log、history-editor-{debug,release}.log、history-build-artifacts.json。图像输出为 Skinning/<配置>-command-list/current/dynamic-recovered/frames.bin，与同目录 dynamic-immediate/frames.bin 比较。

## 边界与后续

这是动态渲染历史的失效重建协议，不是场景更新或 Material singleUse 的通用事务回滚。一次失败会保守失效相关历史中的其他待消费帧；这些帧必须丢弃并重新准备，不得重试原列表。任意自定义动态 pass 仍需要登记其历史并实现重新初始化。

GPU 创建/resize/最终销毁归属、Editor debug/picking/readback、ImGui 稳定 draw data 与正式帧完成仍待闭合；本增量没有独立 Render 线程、背压、GPU fence 或真实 Game/Render 交叠。阶段 2、3 尚未全部完成。
