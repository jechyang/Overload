# 阶段 2 第六增量：相机/时间值快照与灯光上传、绑定持有

日期：2026-09-06。前置提交：2648d43（可选同次 draw packet）。

## 生产路径

EngineBufferRenderFeature 增加 CameraUploadCommand 和 FrameUploadCommand。PrepareCameraUpload 读取调用方已缓存的 view/projection 矩阵并按原顺序转置，复制 camera position；PrepareFrameUpload 再附加显式 elapsedTime。准备不访问 GL，也不持有 Camera/Transform。ExecuteCameraUpload / ExecuteFrameUpload 只读取载荷、写 feature 的 UBO，不改变后续 draw 选择的 CPU 相机状态。

真实 OnBeginFrame 按原时钟计算一次 elapsedTime，经新接口上传并保存本帧值；SetCamera 仍只上传相机区域，同时更新本帧的相机选择，不重新计算或覆盖时间。反射/阴影 pass 的现有 SetCamera 调用无需改写。录制模式 OnBeforeDraw 捕获当前帧常量，在模型/用户矩阵上传前恢复这些值并绑定 UBO 0。缺少模型 descriptor 时仍保留原模型区域；EndFrame 清除本帧 CPU 值。即时模式保持原上传范围及调用顺序。

| Engine UBO 数据 | 字节偏移 | 字节数 |
|---|---:|---:|
| model | 0 | 64 |
| view | 64 | 64 |
| projection | 128 | 64 |
| camera position | 192 | 12 |
| elapsedTime | 204 | 4 |
| user | 208 | 64 |

CameraUploadCommand 为 140 字节，FrameUploadCommand 为 144 字节，由编译期断言核对大小及时间偏移。上传不会覆盖 model/user。

LightingRenderFeature 增加 PrepareLightingUpload / ExecuteLightingUpload。CPU 准备按原顺序使用 override frustum 或相机 light frustum，保留无限范围光源、空集合和矩阵打包语义，返回独占矩阵 vector。真实 OnBeginFrame 已接入，执行不读取 Light、Transform 或 Frustum；灯光仍在原帧开始点筛选，不随反射相机逐面重新筛选。

灯光 GPU buffer 改为共享持有，新增 LightBindingCommand 捕获 buffer 及 binding point。录制 OnBeforeDraw 仅捕获当前选中的缓冲区，执行时重绑，避免每个 draw 重新分配或复制灯光矩阵。BindOverride 显式选择 owning override，Bind 恢复本 feature 的场景灯光。独立 ExecuteLightBinding 只改变 GPU 绑定，不改变下一次录制使用的选择状态。EndFrame 清除选择缓存。

编辑器 DebugCamerasRenderPass 和 DebugReflectionProbesRenderPass 已将假灯光缓冲区改为共享持有并调用 BindOverride，结束时仍用 Bind 恢复场景灯光。没有 lighting feature 时保持原直接绑定。这样新 packet 不会把调试覆盖错误替换成场景灯光。

## 仍然存在的边界

- 相机/时间数值由包拥有；执行 EngineBuffer 操作的 feature 及 UBO 仍须存活。新 API 未使 renderer 或 feature 可并发调用。
- LightBindingCommand 保证 GPU 对象存活，**不冻结缓冲区内容**。同一场景 buffer 下一帧会重新 Allocate/Upload，不能据此把旧绑定排到下一帧执行。灯光 CPU 上传载荷本身独立，但生产尚未按帧版本化 GPU buffer。
- 显式覆盖必须走 BindOverride；在录制路径外任意直接改绑 SSBO 不会更新 CPU 选择。内置两处调试覆盖已迁移，任意用户 pass 需要遵循该协议。
- LightingDescriptor 仍供 ShadowRenderPass 等消费者借用 Light，FrameDescriptor 仍借用 Camera/Framebuffer。复制上传常量不代表这些上游引用可提前销毁。
- target、viewport、clear、pass 前后操作、反射/阴影/后处理资源生产顺序、UI/debug/picking 通用录制与帧完成仍未闭合。本增量不引入 Render 线程、跨帧队列或 GPU fence。
- 资源创建、GPU 上传/绑定及最终释放保持 GL owner 且早于上下文销毁。CPU 准备时源输入必须稳定；灯光准备分配失败不执行上传，不是整帧或 GL 写入事务回滚协议。
- EngineBufferRenderFeature 和 LightingRenderFeature 布局改变，需完整重建依赖模块。原默认即时模式与可选录制开关不变。

## 验收入口

~~~bat
node Tests\Skinning\run-validation.mjs Debug --frame-inputs --vs "C:/Program Files/Microsoft Visual Studio/2022/Community"
node Tests\Skinning\run-validation.mjs Release --frame-inputs --vs "C:/Program Files/Microsoft Visual Studio/2022/Community"
node Tests\Skinning\run-validation.mjs Debug --editor --frame-inputs --vs "C:/Program Files/Microsoft Visual Studio/2022/Community"
node Tests\Skinning\run-validation.mjs Release --editor --frame-inputs --vs "C:/Program Files/Microsoft Visual Studio/2022/Community"
~~~

先构建对应配置完整解决方案。输出为 Build/JobsValidation/Skinning/<配置>-frame-inputs/current/ 与 <配置>-frame-inputs-editor/current/。当前专项在同一实现中切换即时/录制；帧常量另与按原布局独立构造的 CPU 参考及实际 GPU 读回比较，不声称检出旧 Editor 对拍。旧 --draw-inputs 基准保持 b1128a7。

## 本机结果

- Debug/Release 各 32 份相机/时间载荷在 GL 初始化前准备。源 Camera/外部 Transform 修改并销毁后，实际 UBO 144 字节与独立布局参考精确一致；污染全 UBO 后回放只覆盖指定区域，camera-only 回放保持时间和 model/user 不变。
- 灯光覆盖无剔除、相机 frustum、override frustum、无限范围光源、空集合、顺序及指定矩阵分配失败。源 Light/Transform/Frustum 销毁后上传结果逐字节正确。空集合的 GPU buffer 大小为零，无旧数据残留。
- 覆盖 buffer 的外部持有释放后，绑定命令仍能执行；最后持有释放后 GL buffer 回收。另检查场景灯光 buffer 在源 feature 销毁后回放及最终回收；空 override 拒绝并保留原选择。
- 每配置 8 帧 × 3 种相机，共 24 组即时/录制图像精确一致。覆盖透视、正交、90 度反射相机配置，相机临时对象销毁、灯光覆盖/恢复。录制分支在 draw 前故意覆盖 UBO 相机/时间并改绑无关 SSBO；draw 后 140 字节相机、本帧时间及灯光绑定恢复正确，图像非空且逐字节一致。
- 原 packet 场景的 12 组反射/阴影/蒙皮/singleUse 图像与 draw 状态对照通过；24 帧真实 Mannequin/Standard/SceneRenderer 即时/录制图像精确一致，22 次动画变化、最终 933 个非背景像素。
- 真实 Editor 的 Scene/Game/Asset 三 renderer 开启录制：Edit/Play/Pause/8 次单步/恢复/Stop、24 组同视图顺序图像和 8 轮切场景/resize 通过。代表性项目三个场景的并行评估数为 6144/0/1536，批次 96/0/24，Debug/Release 一致。未操作桌面输入。
- Debug/Release --draw-inputs 旧路径对照、材质资源生命周期和独立几何载荷回归全部通过。精确文件大小为：缓冲 trace 16860、输入图像 98304、材质状态/trace 45003/11186、几何图像 4194304、draw 记录 19456、蒙皮图像 7372800 字节。
- Debug/Release 完整解决方案 0 警告、0 错误；10 项程序/模型/shader 复制核对一致。构建包含既有用户改动，阶段提交仅包含本轮文件。

证据：Build/JobsValidation/msbuild-{debug,release}-frame-inputs-final.log、frame-inputs-{debug,release}.log、frame-inputs-editor-{debug,release}.log、frame-inputs-regression-{debug,release}.log、frame-inputs-build-artifacts.json，以及测试输出的 result.json、frame-upload-trace.bin、frame-packet-frames.bin 和复用的 packet/Mannequin 图像。生成项目和日志被 Git 忽略，可用跟踪的测试程序重新生成。

本轮未新增 ASan/Tracy 或独占性能测量。新录制路径每 draw 增加相机页复制/上传和灯光缓冲重绑；即时路径继续在原位置上传。GPU 读回测试中 NVIDIA 会给出 buffer migration 性能提示，glGetError 检查通过；这些带读回的运行不作为性能基线。

## 下一增量

先将 pass/viewport/target/clear 的准备与执行分离，明确 render target 及其附件所有权、resize 与反射/阴影/后处理生产者顺序；随后处理 GPU buffer/texture 内容版本和帧完成，扩大可回放范围。保持旧路径、多视图和覆盖语义对照，继续同线程验收后独立提交。
