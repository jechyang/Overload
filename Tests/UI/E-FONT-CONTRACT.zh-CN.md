# E 字体桥接与图集契约（v1）

生产资源类型：`Sources/OvCore/include/OvCore/UI/Fonts/Font.h`。
原生入口：`QueryFont(State&, uint64_t managedSession, uint64_t owner,
uint32_t operation, span<uint8_t> packet, uint64_t& output)`。

仅 100–103 使用可写 binary packet。宿主 `INativeAccess.ExchangeFont(owner, operation,
byte[] packet)` 固定数组；宿主强制 `gameplay.argument=session.Identity`，不得从项目传入会话号。
最大 packet 1 MiB、单批最多 4096 scalar/pair、路径最多 4096 UTF-8 bytes。
所有数字 little-endian。输入 header 64 bytes：

| offset | 类型 | 含义 |
|---|---|---|
| 0,4 | u32,u32 | magic `0x3146544f`, version `1` |
| 8,12 | u32,u32 | requestBytes（含header）, capacity（等于packet.Length） |
| 16,20 | u32,u32 | count, style（0..3） |
| 24 | u64 | receipt（100为0；其他为已返回租约） |
| 32,36,40 | u32 | pathBytes, emSize, spread |
| 44,48 | u32 | pageIndex, materialSlot |
| 52,56 | u32,u64 | 保留，必须0 |

100：owner为Actor Transform handle；body为UTF-8字体路径+count个u32 scalar。
返回output=receipt；原地响应header 48 bytes：magic/version/responseBytes/recordCount
（各u32）、receipt/generation（各u64）、ascender/descender/lineHeight（各f32）、pageCount(u32)。
随后每条80 bytes：scalar/id/missing/page(u32)，generation(u64)，advance/bearingX/bearingY(f32)，
plane left/bottom/right/top、uv left/bottom/right/top（各f32），distanceScale/distanceBias(f32)，reserved(u32)。
返回按输入首次出现顺序去重的scalar。capacity不足在生成前拒绝。

101：owner为原请求的Transform；body为count组left/right glyphId(u32)。
返回output=响应字节数，原地响应16 bytes头(magic/version/bytes/count)后count个f32 kerning。
102：owner为原请求Transform，receipt非0释放租约，输出0。宿主关闭会话时另发送
**owner=0、receipt=0** 的64字节空请求清理该session的全部字体/租约；只有102允许此管理操作。
关闭清理不解引用已销毁Actor；普通释放仍核验receipt/session/owner绑定，允许旧owner epoch过期后撤销。
103：owner为CanvasRenderer handle，receipt/pageIndex/materialSlot指绑定；原生验证renderer与receipt同一Actor。
绑定后renderer独立持有shared_ptr，因此managed布局可以释放receipt；已提取的Frame仍独立持有页。

所有输入必须先完整验证，异常不写packet，output由总分发归零。成功响应整包一次复制。
100有界缓存最多32字体/session、128未释放receipt/session；失败不新增receipt。
每个receipt保存state与owner epoch；101/103拒绝过期或跨session，102允许安全撤销过期receipt。
主线程生成/packing；RenderThread只读页租约。宿主关闭会话必须调用102全量清理，不能仅依赖Text.Dispose。

FontManager沿用AResourceManager。Editor/Game Context需调用
`FontManager::ProvideAssetPaths(projectAssetsPath,engineAssetsPath)`；字体manager由桥接session持有，
在102全量清理销毁。Font路径允许`:Fonts/...`引擎前缀和项目相对路径，拒绝绝对路径、`..`和嵌入NUL。

B接口：`CCanvasRenderer::SetFontPage(size_t, shared_ptr<const UI::Fonts::AtlasPage>)`。
页为R8线性、无sRGB/无mip、bottom-left；GPU cache key=(generation,index,revision)，
不得覆写仍由在途帧持有的GPU纹理。SDF参数写入顶点uv1.xy（scale,bias）。
fragment：`d=sample.r*scale+bias; alpha=smoothstep(-0.5*fwidth(d),0.5*fwidth(d),d)`。
多页range必须保持painter顺序；同页不连续片段不得为了合批改变绘制顺序。

当前数据契约不代表U6 GPU验收已通过。独立命令：`node Tests/UI/run-font-validation.mjs Debug`。
