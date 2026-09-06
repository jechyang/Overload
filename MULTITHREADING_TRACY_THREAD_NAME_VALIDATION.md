# 阶段 3 第十一增量：Tracy Unicode 线程名稳定性

日期：2026-09-07。前置提交：5c64b21。

## 问题与修复

独占连接 Tracy 的产品长基准在 Game 同线程命令模式约第 156 帧停止推进。第一次采集只有一个完整 case，完整性分析明确失败。再次复现后对本任务进程采样线程栈，发现 Tracy Symbol Worker 在 `SysTraceGetExternalName → CopyString/memmove` 发生异常，随后停留在 CrashFilter。该处理器暂停了其他线程，因此 Main 停在驱动调用的栈不表示渲染队列死锁。

Windows 外部线程名是 UTF-16。原实现未初始化 GetThreadDescription 的输出指针、未检查 HRESULT，并把 wcstombs 的返回值直接作为 CopyString 长度。在 C locale 下遇到不能表示的中文等字符时，返回 size_t(-1)，造成越界复制。GetThreadDescription 返回的 LocalAlloc 描述符也没有释放。

TracySysTrace.cpp 现在初始化输出指针并检查 HRESULT，使用与 C locale 无关的 WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS)。只有成功转换且非空时才复制准确的 UTF-8 字节数；描述符在复制后 LocalFree。缓冲区保持 256 字节，最多接受 255 字节 UTF-8 名称；空、超长或无效转换沿用模块名/未知名称回退，不把错误或不完整 UTF-8 当作长度。

没有关闭系统追踪、移除 GPU zone、放宽图像/时序断言或绕过发生问题的产品路径。

## 验证

Debug/Release `--gpu-profiling` 均通过：

- 每配置 224 次真实外部线程名查询，覆盖 ASCII、中文、补充平面 Unicode、255 字节边界、1024 字符超长名称、空名称和不存在的线程。测试在 C locale 下先确认旧 wcstombs 确实返回 size_t(-1)，再精确比较新路径的 UTF-8 字节；完成后恢复线程名和 locale。
- 原 8 次同线程与 8 次固定 Render owner 的 Driver 重建、两类初始化失败、每次恰好 65,536 个匹配查询删除、TLS 清空与重复创建/销毁规则仍通过。
- 完整 Debug/Release 解决方案构建均 0 警告、0 错误，日志为 `Build/JobsValidation/msbuild-{debug,release}-tracy-thread-names.log`。

修复后重新链接并运行真实 Release Game，未连接的九组基准正常退出；连接后的九个 case、共 3,456 帧也完整结束，harness/capture 均退出 0。官方 Tracy CSV 导出可解析，三组同线程命令的 Render 均在 Main，三组双线程的 Render 均在不同线程，每组有 383/384 次实际 Update 与 Render 区间交叠。正常同线程对照没有这种交叠。长基准夹具及通用分析脚本随下一性能验收增量提交。

## 证据与复现

~~~text
node Tests/Skinning/run-validation.mjs Debug --gpu-profiling
node Tests/Skinning/run-validation.mjs Release --gpu-profiling
~~~

按需追加 --vs。专项输出在 `Build/JobsValidation/Skinning/<配置>-gpu-profiling`。失败原始采集保存在 `tracy-game-benchmark-release-first-hang.*`，符号化栈在 `game-benchmark-tracy-stacks.log`；修复后的完整采集、退出状态和分析为 `tracy-game-benchmark-release.tracy`、`-result.json`、`-events-analysis.json`。这些均位于被忽略的 Build/JobsValidation。

本修复只影响 Windows Tracy 外部线程名解析和对应内存释放，不改变渲染命令、队列容量或未连接采集时的渲染热路径。接下来完成编辑器长采集、CPU/GPU/队列/输入延迟最终汇总与阶段 2/3 审计。
