# C# 接入 S6 实施与验收记录

日期：2026-09-16。起始 HEAD：fcfcecd（S5）。本阶段独立提交，不 push。
状态：S6 实现与固定源码完整验收通过。该文件随 S6 独立提交；实际提交号可用 git log -1 -- CS_SCRIPTING_S6_VALIDATION.md 查询，并在后续 S7 验收记录中列出。

## 基线与保护

- 核对 S5/S4/S3/兼容入口/S2 提交链，与交接一致。保存既有 587 个无关文件 SHA-256。
- H 盘起始约 0.27 GiB；新证据位于本会话获准写入的 C 盘目录，未删除旧证据。
- 指定 verify_s5.mjs 基线 exit 0：Build/Tests/Scripting/s5-dImUjq/verification.json，77 条检查，包含 S1–S4、Lua、真实 Editor/Game 与默认/RenderThread。
- 标准 dotnet build CET 失败与显式完整 MSBuild 兼容成功分开记录。SDK/runtime/net10.0 保持 10.0.401/10.0.12/net10.0。
- PowerShell 仍因 CET 失败，使用 CMD/Node。Node 沙箱访问证据路径被拒后改在沙箱外运行。
- 原生首次链接 OvGame 遇到 LNK1116/error 112（H 盘无法扩展 ilk）；本轮构建使用 LinkIncremental=false 后成功，未关闭 FileTracker。

## 实现范围

- 项目程序集与项目依赖复制到独立 shadow 代，collectible ALC 解析项目依赖；ScriptCore/ManagedHost（包含生成 NativeApi）固定为常驻共享身份，检查精确程序集身份。
- 保持 Host ABI 1/R6 不变；新增独立常驻重载与回收诊断入口，启用既有生命周期值 15。
- Editor C# 面板显式开启运行态重载；未开启时保留 S5 的 Stop 后应用语义。
- 预检全部附件、候选构造与支持字段；按 ID/别名匹配且不猜测类型转换。删除字段丢弃，新字段保留默认值。
- Main 安全点停止旧分派，OnReloadRelease 清理旧实例；TrackSubscription 登记的 IDisposable 在 finally 中释放。旧会话/实例和 UI 回调失效，缓存清空。
- 新实例恢复生命周期标记并调用 OnReloadRestore，不重复 Awake/Start；恢复异常只隔离故障实例，保留已发生的 native 副作用。
- 新构造默认值、编辑覆盖值与迁移运行值分别保存。无任意对象图、静态状态、任务或委托迁移。
- WeakReference 诊断实际观察 collectible 上下文；强制 GC 仅在显式验收/诊断调用。64 个待回收代后拒绝继续加载并提示外部事件/静态根/线程任务与重启要求。

## 验收入口

node Tests/Scripting/Integration/verify_s6.mjs --python C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe

固定源码入口包含 S1–S5 回归、默认/RenderThread 各 20 次构建/重载及 Play/Stop，并额外运行未登记事件订阅保留根负例。最终收据：Build/Tests/Scripting/s6-aaF4Qf/verification.json，stage=S6、success=true，UTC 2026-09-16T07:06:41.906Z。外层检查 83 条，固定源文件 195 个。

## 开发与失败证据（不替代最终完整验收）

- s6-dev-2：默认/RenderThread 各 20 轮通过，两模式最后均 collected=25、alive=0；未登记 AppDomain.ProcessExit 事件订阅负例报告仍保留。该轮之后补充了 Inspector 默认值重绑定和更多断言，故不作为最终固定源码收据。
- s6-tY0Dbk：S1–S5 完整回归通过，S6 夹具编译在新增旧句柄测试处因缺少 CTransform 生成反射 traits 头文件（C2027/C3861）失败；不计整轮通过。已补齐生成头文件。
- s6-dev-3：两模式各 2 轮针对性验证及保留事件根负例通过，覆盖新增的旧实例/会话句柄、静态状态不迁移、默认值/运行值/编辑覆盖分离及过期 UI 回调。短测仅用于开发，最终入口固定每模式 20 轮。

## 最终结果

- 默认/RenderThread 均完成 20 次真实完整 MSBuild 构建、运行态重载与 Play/Stop；各自最终 collected=25、alive=0。
- 新旧依赖实际执行结果不同，字段 ID 改名、精确 UInt64/UTF8、静态状态不迁移、未知对象字段排除、构造默认值与编辑/运行值分离、旧 UI 回调、旧会话及旧实例跨会话拒绝全部通过。
- 不兼容字段与编译失败保留旧代；提交后的 OnReloadRestore 异常使新实例 Faulted，已写入的 Transform.Z=99 保留，Peer 继续调度，后续正常重载恢复。
- 正常事件订阅通过 TrackSubscription 解绑；故意不登记的 ProcessExit 事件根负例观察到 alive>0，报告保留线索，没有冒称回收成功。
- S1–S5、Lua、真实 Editor/Game、默认/RenderThread 回归全部通过；native-no-change-build exit 0，obj 哈希/mtime 不变。
- Host ABI/R6 生成物/SDK 锁哈希与基线一致；final-audit.json 复核源码无漂移，既有 587 个无关文件全部未变。
- 标准 dotnet build：CET 失败，未通过；显式完整 MSBuild 兼容流程：通过。这一环境限制未通过更新 Windows、关闭 CET、改 SDK 或全局 PATH、降级框架规避。
- 没有将 S6 声称为任意对象图/静态/任务/委托迁移或用户永久线程的强制卸载保证；S7 尚未实施。
