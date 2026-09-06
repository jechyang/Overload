# 阶段 3 第三增量：原生 GPU 资源的回收所有者

日期：2026-09-06。前置提交：8274037（后台日志与主线程通知）。

## 实现范围

baregl 新增 DeletionQueue / ResourceLifetime，覆盖 Texture storage、Buffer、Framebuffer、Renderbuffer、ShaderProgram、ShaderStage、VertexArray 七类原生对象。RenderThread 在初始化 Driver 前绑定回收队列，正常循环回收后台最后释放的对象；停止时在 GPU 帧排空后关闭资源域，再销毁 Driver 并解除 GL 上下文。

这一步将已在固定 owner 创建的原生对象的最后释放移回 owner，尚未将 Editor/Game 产品的资源创建、UI 初始化和 GL 录制调用迁移到 RenderThread。产品仍使用同线程帧循环。

## 生命周期协议

ResourceLifetime 在执行 GL 创建之前分配回收记录；创建成功后记录原生名称。Buffer 等使用 NativeObject 的生命周期成员，Texture 使用独立 storage 成员，因此销毁 Texture 包装对象不提前销毁帧持有的 GPU storage。ShaderProgram/ShaderStage 的 GL 创建移到构造函数体内，在回收记录准备好后执行。

固定 owner 上最后释放时直接删除。其他线程最后释放时，用预先存在的链表节点将记录移到待回收列表并唤醒 owner，不分配新的回收任务，不阻塞等待 owner 执行。正常 Drain 只取出待回收列表，不逐帧扫描全部活资源；GL 删除在锁外执行。回收记录处理完毕后才对 WaitIdle 发布完成。

Render 唤醒信号使用共享 mutex/condition_variable，回收状态发布后再通过同一个等待互斥锁通知，避免空闲线程丢失唤醒。晚到的 CPU 释放只持有这个独立信号和资源域状态，不借用已销毁的 RenderThread 对象。原帧队列的容量与 fence 协议保持不变，帧捕获仍留到 GPU 退役。

没有绑定 DeletionQueue 时保留原来的 GL-owner 直接析构路径。已经按旧路径创建的对象不会因稍后启动 RenderThread 自动改变 owner，产品接入时必须在目标 owner 上创建相应资源。

## 停机与旧句柄

Close 在 owner、GPU 排空之后删除域内剩余原生名称，包括仍被外部 CPU 对象保留的名称，同时发布资源域失效。NativeObject::GetID 和 Texture::Snapshot::GetID 此后返回 0。旧包装/storage 的后续析构不再调用 GL，因此不会在新上下文中误删同号名称。Close 返回被失效的活原生资源数量；重复关闭不再执行删除。

关闭期间局部共享记录保护删除列表，允许 CPU 持有者同时释放；GL 删除不在域锁内执行。正常应用退出仍应先停止生产并释放外部资源，再关闭消费者。域失效后必须丢弃旧渲染输入，不能继续调用旧包装对象的 Bind/Upload 等 GL 方法，也不能重放缓存了原始 GLuint 的命令。

## 验证

NativeObject 实例布局改变，必须完整重建所有使用方，并重新生成工程以加入 ResourceLifetime.cpp。随后运行（按需追加 --vs）：

~~~text
node Tests/Skinning/run-validation.mjs Debug --resource-lifetime
node Tests/Skinning/run-validation.mjs Release --resource-lifetime
node Tests/Skinning/run-validation.mjs Debug --render-thread
node Tests/Skinning/run-validation.mjs Release --render-thread
node Tests/Skinning/run-validation.mjs Debug --editor --command-list --editor-ui --product-loop
node Tests/Skinning/run-validation.mjs Release --editor --command-list --editor-ui --product-loop
node Tests/Skinning/run-validation.mjs Debug --game-frame --product-loop
node Tests/Skinning/run-validation.mjs Release --game-frame --product-loop
~~~

Debug/Release 全部通过：

- 七类真实 GL 对象各 32 轮，生产线程没有当前上下文，在 owner 暂停期间最后释放包装/storage；随后均只在 owner 删除一次，glIs* 查询确认名称回收。
- 空闲 owner 由后台纹理释放唤醒，WaitIdle 等到实际删除完成；没有依靠下一帧 Submit 才回收。
- 主线程销毁源 Texture 包装后，拥有型帧仍可访问其 storage；GPU fence 退役后最终回收。
- 停机时保留外部七类包装与纹理快照，确认原生名称已删除且公开 ID 失效；创建新资源域后再释放旧包装，不影响新资源。
- 原 RenderThread 八组真实 UI 图像精确对拍、两帧 GPU 在途、背压、关闭唤醒、失败/后继失效和任务退出回归通过。
- 实际 Editor/Game 产品图像、交互与退出回归通过；保持 Editor 三视图、18 组 UI、拾取/Gizmo/材质拖放、八帧动态姿态和 Game 整帧对照。
- Debug/Release 完整解决方案构建均零警告、零错误。专项使用隐藏窗口；仅在 Render 线程退出并解除上下文后，测试临时在主线程查询 GL 名称是否已删除。

证据：Build/JobsValidation/msbuild-{debug,release}-resource-lifetime.log；Skinning/<配置>-resource-lifetime/result.json、<配置>-render-thread/result.json 和两个 product-loop 模式的 result.json；current 子目录保存编译和运行日志。未将并发构建/验证期间耗时当作性能基线，没有宣称整体性能收益或形式化无竞争证明。

## 尚未完成的边界与下一步

- ResourceTracking 仍是裸 Texture 指针注册表，事件在包装创建/销毁线程即时触发。需要改为稳定元数据/代际协议并在主线程消费；Texture 创建事件早于 Allocate，不能直接延后调用现有裸指针回调。域关闭后 GetID 为 0，注册表移除也不能再仅依赖析构时查询原生 ID。
- ImGui 后端的 DeviceGeneration 仍直接删除 shader/vertices/indices，并维护 backend/device generation 与 GL loader 生命周期。需要将这段共享 GPU 状态的完整收尾移回 owner，不能只改三处 glDelete 而留下跨线程计数和 loader 关闭。
- RenderFrame 的 GLsync 仍由既有 GPU fence 协议在 owner 回收；Tracy GPU TLS/查询对象的长期退出行为仍需核实。原始 GL 临时资源、资源创建/上传、录制期间查询和窗口交换还须按产品调用边界检查。
- 完成以上边界后接入产品有界帧队列、逐帧输入/结果与失败恢复，再验收真实 Game/Render 交叠、resize/场景/退出压力及 Tracy CPU/GPU/队列/输入延迟。阶段 2/3 整体目标尚未完成。
