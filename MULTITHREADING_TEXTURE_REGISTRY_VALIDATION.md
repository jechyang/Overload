# 阶段 3 第四增量：纹理注册表与主线程调试面板

日期：2026-09-06。前置提交：89e10c7（原生 GPU 资源 owner 回收）。

## 实现范围

ResourceTracking 改为受互斥锁保护的内部包装对象身份表，对外返回拥有型元数据与 Texture::Snapshot，不再返回裸 Texture 借用或容器 span。每次注册分配递增代际；查询可指定 native ID 和代际。内部地址仅作为身份键，销毁通知按地址移除，因此资源域关闭后 GetID() 已变为 0 也能清理记录。

纹理构造通知早于 Allocate，初始记录只发布名称、类型、ID、代际与 storage，尺寸为 0、allocated 为 false。Allocate 完成后发布 OnTextureUpdated；Resize 也经此路径更新。读者复制的是锁内一致的尺寸与状态，不读取正在被 owner 修改的 TextureDesc。像素仍可更新，快照不冻结 GPU 内容；同一包装对象的创建/修改仍由其 owner 串行执行。

TextureDebugger 不再订阅创建/删除线程即时触发的 UI 回调。主线程 Update 拉取稳定快照、维护选择和图片持有；仅相同 ID、相同代际可以保留旧选择，重新注册不会悄悄替换选中对象。可见时刷新列表，关闭时仍检查选中纹理是否被删除并释放持有。线程检查先于任何面板开关读取；尺寸未分配或无效时图片大小为零。

## 生命周期与使用约束

- TrackResources 在资源生产者启动前安装一次，重复调用不会重设 handler。不能在生产期间替换或重置 baregl 全局 handler；本次没有把所有 debug handler setter 改成并发接口。
- GetTexture/GetTextures 的 managed storage 可跨线程持有，最后释放走已有 owner 回收。legacy storage 的跨线程查询拒绝，创建线程拿到的 legacy 副本仍必须留在对应 GL owner；GetTextureIDs 只复制 CPU 数值，可以跨线程查询。
- 删除在锁内摘除节点、锁外释放 storage，避免 GL 删除/诊断重入注册表锁。列表复制出错时，已复制的 storage 也在解锁后析构。
- 域关闭后查询过滤已失效 storage；旧元数据的 id 数值是历史身份，实际使用应检查 storage.GetID()。关闭后丢弃旧渲染输入，不能重放原始 GLuint。
- 这是纹理注册表与面板接口变更，原 TextureAddedEvent/TextureRemovedEvent 已移除。所有仓库调用方已迁移；自定义外部调用方需改用快照查询。IEventHandler 新增默认空实现的 OnTextureUpdated，现有源代码实现无需新增 override，但应完整重建虚表使用方。

## 验证入口与证据

先完整构建 Debug/Release，再运行（按需追加 --vs）：

~~~text
node Tests/Skinning/run-validation.mjs Debug --texture-registry
node Tests/Skinning/run-validation.mjs Release --texture-registry
node Tests/Skinning/run-validation.mjs Debug --editor --command-list --editor-ui --product-loop
node Tests/Skinning/run-validation.mjs Release --editor --command-list --editor-ui --product-loop
~~~

另保留两配置 --resource-lifetime、--render-thread、--game-frame --product-loop 回归。

专项各执行 512 次固定 Render owner 尺寸变更和临时纹理创建/销毁，同时主线程无当前 GL 上下文地查询单项、列表与 ID。检查宽高一致、复制元数据不随后续 resize 改变、错误代际拒绝、包装销毁后登记消失但 storage 仍存活，以及跨线程最终释放的实际 GL 回收。验证 legacy 单项/列表跨线程持有被拒绝、纯 ID 查询可用；资源域关闭后的 ID 过滤、关闭后销毁和同一 C++ 地址在新域中的再构造也通过。

实际 Application 测试临时将既有 GL 上下文交给固定测试线程，并在该线程绑定生产 DeletionQueue。主线程不持有 GL 上下文，只刷新实际 TextureDebugger；验证后台创建/分配/resize/移除不会直接改变 UI、后台 Update 被拒绝、关闭面板释放选择。通过显式移除/重新注册注入相同 native ID 的新注册代际，确认旧选择被清除。此项是确定性注册代际注入，不声称强制实现物理 GL 名称复用。

baregl 当前只支持一个 Driver/Context 实例，集成夹具沿用 Application 的唯一 Driver；未绕开单实例断言，也不在主线程脱离上下文期间运行 ImGui GPU 后端。专项另外用真实 RenderThread 验证 managed 注册表；产品仍未接入 RenderThread。

Debug/Release 完整解决方案构建零警告、零错误。最终生产构建日志为 Build/JobsValidation/msbuild-{debug,release}-texture-registry-final.log；此前完整虚表/头文件消费者重建记录在无 -final 的同名前缀日志。专项与产品结果分别在 Skinning/<配置>-texture-registry/result.json、<配置>-command-list-editor-ui-product-loop/result.json、<配置>-game-frame-product-loop/result.json，current 子目录保留编译/运行日志。原资源/队列模式各自的 result.json 也已更新。

实际 Editor 回归保持 18 组 UI、三视图聚合、八帧动态姿态/视图、拾取/Gizmo/材质拖放以及 Application.Run/退出精确对照。Game 图像/产品循环、七类原生资源和固定 Render 线程背压/失败/退出回归均通过。测试使用隐藏窗口；未用同时执行构建或验证期间的耗时推导性能收益，未新增 ASan 或 Tracy 交叠结论。

## 下一步

ImGui 后端 DeviceGeneration 的原始 GPU 对象、共享计数与 loader 收尾仍需回到 owner；还需核实 Tracy GPU TLS 生命周期、生产资源创建/上传及录制期间 GL 查询、窗口平台操作与 SwapBuffers 的归属。之后接入产品多帧队列、对应帧输入结果、失败恢复，验收真实 Game/Render 交叠、resize/场景/退出压力和性能。阶段 2/3 整体目标仍未完成。
