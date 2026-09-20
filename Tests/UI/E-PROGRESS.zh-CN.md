# E / U6 字体与文字进度

2026-09-21，工作区基线 `870c494538629e8c06e825297a8387cf293f284e`。
**E / U6a 与 U6b 已完成并通过验收**，包含生产 GPU、共享回归和最终示例 host generation。
兼容总表及共享桥接集成提交由 A 统一收口，不代表其他 UGUI 阶段已全部完成。

已落盘：

- `UI/Fonts/Font`：FreeType face/file/variation生命周期，固定em outline距离场，
  非零/奇偶填充，二次/三次轮廓自适应细分，单通道R8；无按字号alpha缓存。
- 批量scalar去重、glyph alias去重、metrics/kerning，style/em/spread缓存key；
  字体代际包含face index和variation身份。
- 有界多页append-only packing；事务失败回滚，页采用copy-on-write不可变revision，
  旧Batch和AtlasPage可在Font卸载/重载/销毁后继续独立读取。
- `FontManager`复用AResourceManager；FontBridge 100–103复用宿主会话身份及安全句柄。
- `UI.Text.Fonts/Layout/Native/Text`：托管FontData、Unicode标量/UTF16位置、稳定fallback、
  combining mark基点、kerning、CRLF/tab/CJK/word-wrap、九种对齐、overflow、best-fit、
  b/i/size/color富文本、按有序page ranges生成几何，Text继承MaskableGraphic。
- `FontUpdateTracker`使用A的UISessionRegistry，D已接UI.Rebuild；布局绑定页后立即释放
  receipt，renderer/Frame独立持有页。A已接宿主binary分发、会话关闭清理和字体路径根。
- B已接SetFontPage、不可变Frame页、R8上传和正式UI shader的SDF分支。

接口文档：[E-FONT-CONTRACT.zh-CN.md](E-FONT-CONTRACT.zh-CN.md)。

独立验证：

```
node Tests/UI/run-font-validation.mjs Debug
node Tests/UI/run-font-validation.mjs Debug --integration
```

最终专属证据：`Build/UIValidation/Debug-font-NWv2Oz/result.json`，全部退出0。

- 70项原生检查：内部/轮廓/外部距离、批量去重与glyph alias、metrics/kerning、
  Unicode非法值、CJK/代理对/combining cmap、style/em/spread分档、事务回滚与页容量、
  owner线程拒绝、热替换、资源卸载、在途页代际与最终释放。自生成TrueType fixture
  的每个矩形texel均与解析SDF比较，最大允许误差0.04 SDF像素。
- 86项C#排版检查：UTF16源位置、无效代理项替换、fallback、组合字符、换行/空白/
  CRLF/tab、0.5/2倍行距、九对齐、best-fit、overflow、富文本、批量kerning与异常释放。
- 正式ScriptCore/ManagedHost重新编译；真实CLR Text子类编辑首帧、Play、禁用、
  Inspector字段/XML往返、字体文件热换、脚本热重载、ALC回收、Text移除/切场景。
  默认Text不覆盖font，所有继承字段经正式Inspector ABI读取成功，内置Roboto真实渲染通过。
- RTX 3070 Ti/OpenGL 4.5：Overlay/Camera/World Space、16/40/64字号、2倍DPI，
  半覆盖边缘与解析轮廓误差不超过1像素；字号变化复用固定em图集页。
- 真实多页与混合字体fallback，每一个page/generation都有CPU距离采样对应的GPU像素断言；
  rich bold/颜色/孔洞、使用真实kerning选出最大best-fit字号38、0.5行距重叠行的像素。
- RectMask2D、stencil的可见/不可见像素；录制后移除Text、销毁场景/字体manager/
  项目session，旧帧仍正确执行。默认与RenderThread RGBA逐字节相同。
- FontBridge包容量/保留字/非法Unicode、跨session/跨owner、过期epoch、wrong-thread、
  重复release及session全清理有生产原生边界测试。

共享回归：

- B标准Debug第4轮成功：`Build/UGUI-B/debug-build-4.log`。
- E独立通用UI/managed/S3普通与RenderThread全通过：`Build/UIValidation/Debug-EBnxGK/result.json`。
- A完整U0/foundations/字段/组件引用/重载/demo/S3全通过：`Build/UIValidation/Debug-VSMChf/result.json`。
- 两份OvEditor大小43,059,712 bytes、mtime与SHA-256完全相同；
  `Build/UIValidation/E-editor-artifacts.json`，SHA-256
  `35d4d5a8bc2c2da8729688c5259c5fcb841b5bb854c42cd833414dc64b745fc9`。
- editor-camera：`Build/JobsValidation/Skinning/Debug-editor-camera-editor/result.json`，
  8组选择/投影/提交模式断言通过。OS cursor warp在无焦点环境明确SKIP，
  实际导航点击的ImGui replay在`Build/EditorUX/Scripting/results.json`普通/RenderThread通过。
- 最终示例generation由A统一重建：
  `Examples/ManagedUI/.overload/csharp/8abffb69ea68a0ee19bf2576/Debug/generations/0c94c9a2-b47a-442d-b68f-30d86ce401af`。
  receipt与三程序集SHA在`Build/UIValidation/u1-final-example-generation.json`；包含最终默认字体/行距修复。

集成依赖：A公共工具`f6ac031`、D布局`223ae46`/`fee3e11`、B图形/遮罩页契约`d8acb1f`。
A的Font Exchange/session-close/Context路径接入由A在协调提交队列最后统一提交。E只提交自己的20个文件，
不暂存GameplayBridge、Instances、Fields、Frame或CUI等共享文件。

明确的当前适配：

- UTF16无效代理项替换为U+FFFD；combining mark零advance附着基点，不提供复杂文字shaping/bidi。
- fallback持久字段目前用换行分隔string（沿用已验证string字段ABI），不自造通用列表tag。
- 空Font AssetRef表示自动选择内置Roboto，字段自身不保存被通用AssetRef ABI拒绝的引擎路径；
  `:Fonts/Roboto-Regular.ttf`仅在Text运行时设置和专属字体桥接中解析。
- 一个Text遵循正式CanvasRenderer的65535顶点/64有序材质range上限，超限报错，不重排painter顺序。
- FontUpdateTracker当前按UI.Update抽取调用；多视图可能一场景帧检查多次mtime，但不会重复生成或上传glyph。

所有构建/evidence留H盘独立`Build/UIValidation/Debug-font-*`，未改AView主视图回归保护。
