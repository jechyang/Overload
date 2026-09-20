# B 图形、材质槽、遮罩与命中契约

本契约使用生产类型，不是临时接口。消费者入口：

- C：Sources/OvManaged/Overload.ScriptCore/UI.Image.cs 已从 UI.Graphic.cs 提取；Image/RawImage 现继承 MaskableGraphic，几何仍保持原有四边形行为。
- E：CanvasRenderer.SetMesh / materialCount / SetMaterial / SetTexture / SetIndexRange；原生 CCanvasRenderer::SetFontPage。
- F：UI.GraphicHitSource 实现 IUIGraphicHitSource；GraphicRegistry 按 session 和 Canvas 查询。
- D：ClipperRegistry.Cull(anchor) 在 layout/Notify 后、PreRender 前；GraphicRebuildTracker.Update 由 A 的每场景 LateUpdate 调用真实 scaled/unscaled delta。

## 顶点与边界

UI.Vertex.cs 的 UIVertex 包含 Vector3 position、Color32 color、Vector2 uv0/uv1/uv2/uv3、Vector3 normal、Vector4 tangent。simpleVert 的 normal=(0,0,-1)、tangent=(1,0,0,-1)、color=white。

VertexHelper 最大 65535 顶点、393210 索引；索引数必须是 3 的倍数。所有浮点通道必须有限；负索引/越界/不完整三角形/超限在修改 helper 前拒绝。UIMesh/FillMesh 使用深复制，GetVertices/GetIndices 不暴露内部数组。modifier 按 Actor.GetComponents 返回的组件顺序在 C# 执行，禁用组件不参与。

op45 UIMeshEncoding v2：little-endian；16 字节头依次为 uint32 magic=0x3249554f、version=2、vertexCount、indexCount；每顶点 22 个 float 共 88 字节，顺序为 position3、uv0(2)、color4、normal3、tangent4、uv1(2)、uv2(2)、uv3(2)；最后为 uint32 indices。Color32 在编码时归一化为 float。原生继续接受旧 8 字节头+36 字节顶点，但新宿主只发 v2。

GPU 使用 B 自有 UI::Geometry(IMesh)，不改通用 Geometry::Vertex：location 0 position3、1 uv0(2)、2 color4、3 normal3、4 tangent4、5 uv1(2)、6 uv2(2)、7 uv3(2)。旧自定义 UI shader 如借用 normal/tangent 存颜色，需改用 location2 vec4。

## 材质槽与资源所有权

最多 64 个有序槽，每槽有 materialPath、texturePath、可选不可变 fontPage，以及 firstIndex/indexCount。默认槽覆盖整个 mesh；显式范围通过 SetIndexRange 设置，必须三角形对齐。不同槽可引用相同材质/纹理，但每个槽只表示一个连续索引范围。先提交 mesh，再设置范围。Graphic 默认一个槽；多页 Text 重写 UpdateGeometry/UpdateMaterial，并调用 RecalculateClipping 与 MaskUtilities.ApplyStencil。

Frame::Mesh 深复制全部顶点/索引和槽，固定普通纹理 shared_ptr 与 Font AtlasPage const lease。RenderResources::Extract 必须在主线程/资源所有者准备阶段完成材质解析和 GPU 资源固定；只把它返回的 RenderSceneSnapshot/PreparedFrameData/commands 交给 Worker/RenderThread。Frame 的 materialPath 是主线程准备输入，不能让 Worker 查询 MaterialManager。命令持有 Geometry、shader/material 快照、纹理代际直到执行完成。

fontPage 类型在 UI/Fonts/Font.h，由 E 唯一拥有。GPU key=(generation,index,revision)，R8 线性无 mip；缓存持有 GPU texture，CPU page lease 失效时清理。旧 revision 不原地修改。UV1.xy=(distanceScale,distanceBias)，默认 UI shader 的 _FontSDF 分支根据 fwidth 距离生成覆盖率。

## 裁剪、模板和坐标

CanvasRenderer.EnableRectClipping 接收 Graphic 局部 Rect；Frame 提取转换到当前 Canvas 坐标。矩形裁剪使用 UI fragment 的精确 discard，适用于 Overlay/Camera/World Space，避免把旋转后的平面误当屏幕 scissor。

UIStencilState 是 C# 决定的 8-bit 状态：enabled/replace/colorWrite/reference/readMask/writeMask。Mask 第 d 层写 bit d，后代比较全部祖先位；最多 8 层，超深度明确拒绝。Frame 仅复制 renderer 状态并在子树结束后生成 pop 绘制，不在 C++ 计算遮罩层级。各 draw 发布独立 PipelineState；低层 uiPipelineState 覆盖只允许 OverlayUI/SceneUI，其他 pass 拒绝。

Graphic.Raycast 接收本 Graphic 局部坐标；父级 filter 使用各自局部坐标。GraphicHitSource/op53 输入遵循 F 的 Game viewport framebuffer bottom-left pixels，不再次 Y 翻转。CCanvasRenderer 持有最近一次提取的 const Frame；命中读取该快照的画家顺序、投影、clip 和 stencil 网格。没有已提取帧时不命中；因此首个输入 dispatch 前要有场景 UI 首帧提取。

ScreenSpaceCamera 采用当前引擎 +Z 摄像机约定，screen-right 对应 camera local -X；已用非中心像素验证，不能只测画面中心。

## 生命周期与跨包桥接

注册使用 A 的 UISessionRegistry，Graphic/Clipper 在 disable/destroy/reload/closing 清理，不创建静态项目强引用。NativeUIComponent.ValidateReference 只读验证组件 epoch。

B 占用 UI op40–59；45 mesh、46槽资源、47写renderer、48只读renderer、49/50 group、51/52 rect mask、53只读命中。op50(argument=0, text=release) 和 op52(argument=0) 是关闭session期间唯一允许的遮罩清理写；普通写仍拒绝。A 持有 GameplayBridge/Instances 的总分发。

验证命令：node Tests/UI/run-graphics-validation.mjs --runtime。--managed-only 和 --native-only 仅用于定位问题，不能替代 --runtime。
