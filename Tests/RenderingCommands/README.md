# 同线程几何命令回归（阶段 2 首增量）

复用蒙皮测试的 Windows/VS2022 编译、链接和隐藏窗口设施：

```bat
node Tests\Skinning\run-validation.mjs Debug --geometry
node Tests\Skinning\run-validation.mjs Release --geometry
```

先构建对应配置的完整解决方案；必要时添加 `--vs "C:/Program Files/Microsoft Visual Studio/2022/Community"`。输出为 `Build/JobsValidation/Skinning/<配置>-geometry/{baseline,current}/`。

两份独立程序仅替换 ABaseRenderer.cpp：baseline 从阶段 1 提交 `4f8da08b854a7d57e3a2b0ce08f3d8a05039d7d9` 读取，current 取工作树。两者使用当前头文件（新增嵌套类型/方法不改变对象布局）、当前蒙皮与 Scene 源码、同配置其他引擎库。旧路径没有新命令调用。原蒙皮测试的 baseline 仍然固定为原始提交，未被此模式替换。

直接链接 ABaseRenderer.cpp 会把其中遗留的静态默认相机构造提前到库初始化之前；两份测试均先编译/链接当前 FVector3、FQuaternion、FMatrix3、FMatrix4 源码，保证其所需数学常量先初始化。没有改写旧渲染器源码或关闭 Debug 检查。测试另将引擎断言输出到 stderr 后终止，避免进入 Windows JIT 调试流程。

- 新路径在创建 GL 上下文之前准备 64 种 stateMask 命令。mesh 的全部虚函数都抛错，确认准备阶段不绑定/读取 GPU 网格；修改并销毁 Drawable/Material 后检查捕获值、原 pass 状态与借用 mesh 身份。缺失 mesh/material、零/负实例数必须拒绝。
- 在真实隐藏 GL 上下文中执行 64 种状态组合 × 4 种 index/instance 分支，交替 TRIANGLES/LINES/POINTS 并修改共享材质 uniform。非索引分支通过 IMesh 复用有效 VAO，避免触发现有 Mesh::Upload 对空 index buffer 的限制。拦截实际 GL 绘制入口、记录顺序/参数/关键管线状态，再转发给真实驱动；检查四分支各出现 64 次、无 GL 错误、画面不是全部空白。
- `draws.bin` 的 256 条记录和 `geometry-frames.bin` 的 256 张 64×64 RGBA 在旧/新程序间逐字节一致。记录不包含不稳定的 GPU 句柄，也不声称捕获了所有 GL 状态。
- 复用真实 Mannequin/Standard/SceneRenderer：`frames.bin` 的 24 帧 320×240 RGBA 完全一致，动画图像变化且绘制不推进姿态版本。

同线程集成检查另运行 `Debug/Release --editor`，验收真实新建项目、共享资源、多视图消费顺序、暂停/逐帧/恢复/切场景。此检查是新路径的集成回归，并非旧编辑器与新编辑器逐帧对拍。

该增量仍同步消费借用 mesh，shader/texture/uniform/palette/viewport/target 由外层原调用链设置。测试没有在源 mesh 销毁后执行命令，也不支持跨帧排队、后台录制或 Render 线程。详细所有权审查与剩余工作见根目录 `MULTITHREADING_RENDER_COMMAND_VALIDATION.md`。
