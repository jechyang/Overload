# Overload 多线程渲染改造 — 阶段 0 实施计划（v4，验收完成）

> 2026-09-06 更新：阶段 0 已完成实现、双队列/ASan 回归、真实场景对拍、Tracy 线程/等待验收及最终 Debug/Release 全解决方案增量构建（0 警告、0 错误）。后续实施请从 [MULTITHREADING_NEXT_STEPS.md](MULTITHREADING_NEXT_STEPS.md) 开始。
> 当前证据、修复说明与限制见 [MULTITHREADING_VALIDATION.md](MULTITHREADING_VALIDATION.md)。用户最新约束优先于本文：不使用电脑界面验收，操作 VS2022 前必须询问。

---

## 0. 定位与范围

**总路线**：依赖式 job 系统（work-stealing + counter 依赖）→ 先以 A 形态（Game/Render 双线程）承载帧任务 → 保留向 fiber/完全体 C（job graph）演进的空间 → 最终 RHI 化接 Vulkan/DX12。

**本轮（阶段 0）交付**：
1. `OvTools::Jobs` 依赖式任务系统（work-stealing 线程池 + 依赖 counter + ParallelFor）。
2. `SceneRenderer::ParseScene` / `FilterDrawables` 并行化集成。
3. 冒烟测试（独立编译，不入库）+ 引擎构建 + Tracy 验收。

**本轮不做**（后续轮次）：
- 蒙皮姿态评估并行化（`CSkinnedMeshRenderer::EvaluatePose` 在 update 阶段改组件状态，需单独设计）。
- Render 线程拓扑（A 形态）、命令录制层、fiber 升级。
- GL 路径任何改动——**渲染路径保持单线程**，每次 ParseScene/FilterDrawables 在返回前 join（包含 DrawFrame 中反射 pass 的过滤调用）。

---

## 1. 关键事实（已核实，可直接信赖）

| 事实 | 证据 |
|---|---|
| premake 按通配符收集文件，新文件放进目录即可，改完重跑 `gen_proj.bat` | `Sources/OvTools/premake5.lua:9-15`（`files { "**.h", "**.inl", "**.cpp", ... }`） |
| OvTools includedirs 仅 `Dependencies/tracy` + `include`，**不 links 任何库**；tracy 宏在无 `TRACY_ENABLE` 时为 no-op（`TracyAllocators.h` 先例） | `Sources/OvTools/premake5.lua:17-23` |
| OvCore includedirs 含全部 8 个兄弟 SDK include 目录（可用 OvTools 头） | `Sources/OvCore/premake5.lua:22-43` |
| 模块均为 StaticLib，无 export 宏；`fatalwarnings { "All" }`——**新代码必须零警告** | 各模块 premake5.lua |
| C++20（每模块 `cppdialect "C++20"`）；Tracy 仅 Debug/Release 定义 `TRACY_ENABLE`（Publish 无） | 根 `premake5.lua:19-31` |
| 阶段 0 之前的历史基线：应用层尚无 JobSystem；当前已新增工作线程池，不能再称全引擎零线程设施 | 当前 JobSystem 与 Context 集成 |
| `OVASSERT` 在 `NDEBUG` 下编译为空（Release 不生效） | `Sources/OvDebug/include/OvDebug/Assertion.h:11` |
| ServiceLocator = any-map，`Provide<T>(T&)` / `OVSERVICE(T)` | `Sources/OvCore/include/OvCore/Global/ServiceLocator.h` |
| 服务注册点：Editor `Context.cpp:165-175`、Game `Context.cpp:159-169` | 两处均 `ServiceLocator::Provide<...>` |
| `Actor::GetComponent() const`（并发只读安全）；`Material::GenerateStateMask() const` | `Actor.h:323`、`OvRendering/Data/Material.h:335` |
| `FTransform` 世界矩阵**急切维护**（无脏标记懒计算），`GetWorldMatrix()` 纯 const 读 | `OvMaths/FTransform.cpp:206-209` |
| `FastAccessComponents` 是普通 vector，仅在 update 阶段增量改（push_back/erase），帧内只读 | `OvCore/SceneSystem/Scene.cpp:428-471` |
| 两个应用的时序：场景模拟/组件变更严格先于 `BeginFrame`（Editor: `Editor::Update` → `RenderViews`；Game: `Game::Update` → `RenderCurrentScene`） | `OvEditor/Core/Editor.cpp:226-412`、`OvGame/Core/Game.cpp:57-144` |
| ParseScene、FilterDrawables 为非静态成员；主相机准备在 BeginFrame 内，反射 pass 也会在 DrawFrame 中同步调用 FilterDrawables | `Sources/OvCore/src/OvCore/Rendering/SceneRenderer.cpp:196-238, 262-462` |
| `SkinningDrawableDescriptor` 存裸指针 + `poseVersion`，蒙皮数据在 `scene->Update`（渲染前）已定型 | `SkinningDrawableDescriptor.h:18-24` |
| 编辑器每帧多视图各跑一次 `BeginFrame→DrawFrame→EndFrame`（SceneView/GameView/AssetView 各持独立 renderer） | `OvEditor/Panels/AView.cpp:53-81` |

---

## 2. 交付物与文件清单

```
（新增，全在 OvTools 模块，premake 自动收集）
Sources/OvTools/include/OvTools/Jobs/EAffinity.h            枚举: Any / MainThread
Sources/OvTools/include/OvTools/Jobs/JobHandle.h            句柄 = shared_ptr<Detail::ControlBlock>
Sources/OvTools/include/OvTools/Jobs/JobSystem.h            公开 API
Sources/OvTools/include/OvTools/Jobs/JobSystem.inl          ParallelFor 模板实现
Sources/OvTools/include/OvTools/Jobs/Detail/ControlBlock.h  内部任务节点（放 include 树的 Detail/ 下，
                                                             因 JobHandle 内联状态查询访问节点；shared_ptr 本身允许 incomplete type）
Sources/OvTools/src/OvTools/Jobs/JobSystem.cpp              调度器核心
Sources/OvTools/src/OvTools/Jobs/WorkStealingDeque.h        双实现: MutexDeque（正确性基准）+ ChaseLevDeque（无锁）

（修改）
Sources/OvEditor/src/OvEditor/Core/Context.cpp/.h           创建 + Provide JobSystem
Sources/OvGame/src/OvGame/Core/Context.cpp/.h               同上
Sources/OvCore/src/OvCore/Rendering/SceneRenderer.cpp       ParseScene / FilterDrawables 并行化

（临时，验证后删除，不入库）
Temp 冒烟测试: smoke_test.cpp（独立 cl 编译，见 §8.1）
```

---

## 3. JobSystem API 定稿

```cpp
namespace OvTools::Jobs
{
    enum class EAffinity : uint8_t { Any, MainThread };

    class JobHandle
    {
    public:
        JobHandle() = default;                 // 默认构造 = 无效 ≙ 视为已完成
        bool IsValid() const;                  // block != nullptr
        bool IsCompleted() const;              // 无效句柄恒 true
    private:
        friend class JobSystem;
        std::shared_ptr<Detail::ControlBlock> m_block;   // null = invalid
    };

    class JobSystem
    {
    public:
        static constexpr uint32_t AutoDetectWorkerCount = 0;

        // AutoDetect → std::max(1u, hardware_concurrency - 1)（主线程经 Wait 参与）
        explicit JobSystem(uint32_t p_workerCount = AutoDetectWorkerCount);
        ~JobSystem();                          // 停机语义见 §5.3

        // 从任意线程（含 job 内部）可调用。p_dependencies 全部完成后任务才就绪。
        // 默认构造/无效句柄在依赖列表中被跳过。
        JobHandle Submit(
            std::function<void()> p_job,
            std::span<const JobHandle> p_dependencies = {},
            EAffinity p_affinity = EAffinity::Any);

        // 等待期间"边等边做"：调用线程参与执行就绪任务（防死锁，主线程天然参与计算）。
        // 主线程可执行 Any + MainThread 任务；worker 仅执行 Any 任务。
        void Wait(JobHandle p_handle);
        void WaitAll(std::span<const JobHandle> p_handles);

        // 执行至多一个 MainThread 亲和任务，有任务返回 true。为 Render 线程阶段预留；
        // 阶段 0 无生产者，仅冒烟测试覆盖。
        bool PumpMainThread();

        // F: void(uint64_t begin, uint64_t end)，按连续区间切分为 ~4×worker 个块，每块一个 job。
        // 返回的句柄代表全部块完成（内部依赖聚合，见 JobSystem.inl）。
        template<typename F>
        JobHandle ParallelFor(
            uint64_t p_count, uint64_t p_grainSize, F&& p_range,
            std::span<const JobHandle> p_dependencies = {});

        // 任务抛异常时的通知回调（默认空）。异常永远不会中断依赖图（§4.2）。
        void SetErrorHandler(std::function<void(std::exception_ptr)> p_handler);
    };
}
```

---

## 4. 正确性协议（必须按此实现，这是 review 修正的核心）

### 4.1 依赖挂接协议（防丢失唤醒 / 静默死锁）

每个 `ControlBlock` 含一把小自旋锁（`std::atomic_flag`），保护 **状态翻转 + 后继表** 的原子性。

```
状态机: WaitingDeps → Ready → Running → Completed
                                     ↘ Failed（异常，见 §4.2）

Submit(B, deps):
  B.block = new ControlBlock{ status=WaitingDeps }
  先校验有效 deps 全部属于当前 JobSystem，否则抛 invalid_argument
  pending = 统计 deps 中有效句柄数 + 1（提交哨兵）
  B.block->pending.store(pending)              // 必须先于任何挂接
  for each 有效 dep:
      lock(dep->spinlock)
          if dep->status 为终态:
              B->pending.fetch_sub(1)（哨兵尚在，不会提前就绪）
          else:
              dep->successors.push_back(B.block)   // dep 未完成 → 挂后继
      unlock
  全部挂接成功后 if (B->pending.fetch_sub(1) == 1) TryMarkReady(B)
  若挂接抛异常：将 B 标记 Retired、清理 callable/self 后重抛；此前挂接的边不能启动 B

完成 A:
  执行用户函数（try/catch，见 §4.2）
  完成错误回调并释放 callable 捕获，再发布终态
  lock(A->spinlock)
      A->status = Completed（或 Failed）
      snapshot = std::move(A->successors)
  unlock
  通知 Wait/WaitAll 的进度条件变量
  for each s in snapshot:
      if (s->pending.fetch_sub(1) == 1) TryMarkReady(s)

TryMarkReady(X):
  CAS(X->status, WaitingDeps → Ready)          // CAS 只会成功一次 → 恰好入队一次
  成功则按 affinity 入队（Any → 全局队列；MainThread → 主线程队列）
```

**为什么无丢失唤醒**：挂接和完成检查在同一把 dep 锁下互斥——Submit 要么看到 Completed 自行递减，要么挂进后继表，而完成侧搬移后继表也持同一把锁，二者必有一个生效。
**为什么 pending 先初始化**：避免完成侧递减发生在 pending 初始化之前导致下溢/误判零穿越。
**重复依赖**：同一 dep 出现两次 → pending 计 2、后继表出现两次、递减两次，自洽。
**零穿越检测统一用 `fetch_sub(1) == 1`**（prev==1 即归零）。

### 4.2 异常安全（job 抛异常不得卡死依赖图）

```
执行用户函数:
  try { job(); } catch (...) {
      block->exception = std::current_exception();   // 存 exception_ptr
      try { if (errorHandler) errorHandler(block->exception); } catch (...) {}
      // 回调返回及捕获释放后，Finish 才发布 Failed；不使用终止进程的断言
  }
  // 无论成败，§4.1 的完成记账（搬移后继 + 递减）照常执行
```

- `IsCompleted()` 对 Completed、Failed、Retired 均返回 true（等待者不悬挂）。
- 失败任务的**后继照常执行**（fire-and-forget 语义），文档写明。
- OvTools 无日志依赖（不依赖 OvDebug），错误只经 `SetErrorHandler` 回调暴露，默认空操作。

### 4.3 双端队列：双实现策略

`WorkStealingDeque.h` 提供同一接口的两份实现：

1. **MutexDeque**：互斥锁 + `std::deque`。作为**正确性基准**，阶段 0 首先用它跑通全部测试。
2. **ChaseLevDeque**：Chase-Lev 无锁双端队列。owner 底部 push/pop（LIFO，缓存热），窃取者顶部偷（FIFO）。
   - **ABA**：窃取索引必须带 tag（64 位 = 高位计数器 + 低位索引），防索引回绕误配。
   - **内存序**：Push release 发布 bottom；Pop 回撤 bottom 后 seq_cst fence，再读取 top，最后元素通过 top 的 seq_cst CAS 仲裁；Steal 在 acquire top/bottom 读取之间使用 seq_cst fence，先读 slot，再单次 seq_cst CAS top。原“双 CAS”描述有误，按 Le 等人 PPoPP 2013 Figure 1 校正。
   - 固定容量 4096，满时溢出到全局队列；slot 为原子指针，64 位完整计数器参与 CAS，槽位循环复用不重置计数器。
   - 实现完成后用 MutexDeque 对拍同一套冒烟测试（编译期开关切换，如 `OVTOOLS_JOBS_USE_LOCKFREE_DEQUE` 宏或模板参数）。

**实施顺序：先 MutexDeque 跑通 → 再写 Chase-Lev → 压力测试对拍**。学习目标优先保证正确性。

### 4.4 伪共享

- 每 worker 的 deque 头部、`ControlBlock::pending` 所在结构按 `alignas(std::hardware_destructive_interference_size)`（MSVC 支持，回退 64）对齐/填充。

### 4.5 调度结构

- 每 worker 一个双端队列 + 一个全局队列（mutex+deque+condvar，阶段 0 可接受，粗粒度 job 下争用低）。
- **提交路由**：worker 内部提交 → 本 worker deque（LIFO 缓存热）；非 worker 线程提交 → 全局队列。
- **worker 循环**：pop 本地 deque → 取全局队列 → 窃取兄弟 deque（随机起点轮询）→ 睡 CV（搜索队列前观察进度 epoch，发布任务时更新 epoch 并唤醒，覆盖本地 deque）。
- **Wait 即参与**：Wait 循环在短暂 yield 后通过进度 CV 等待新任务或完成通知，观察 epoch 在检查状态/队列之前。禁止用 sleep_for(200µs) 轮询：本机实测可能引入约 15ms 停顿；TryExecuteOne 按调用者身份取任务（主线程可取 Any + MainThread；worker 仅 Any）。
- **嵌套 Submit**：Submit 全程不持用户可见锁（自旋锁只保护控制块内部），job 内可安全 Submit。

---

## 5. 已钉死的设计决策（v2 新增，实施时不得再摇摆）

| # | 决策 | 内容 |
|---|---|---|
| 5.1 | worker 数 | AutoDetect = `std::max(1u, std::thread::hardware_concurrency() - 1)`；主线程经 Wait 参与 |
| 5.2 | 亲和性语义 | `EAffinity::MainThread` 仅由构造线程（= PumpMainThread / 主线程 Wait）执行；worker 不执行、不得 Wait 此类句柄。队列在所有配置下限制 MainThread 任务仅由构造线程获取；Pump 错线程 Debug 断言、Release 返回 false。worker Wait 直接 MainThread 句柄仅 Debug 断言，间接依赖及等待闭环仍是调用方契约 |
| 5.3 | 析构语义 | `~JobSystem`：置停机标志 → 唤醒全部 worker → join。**队列中未执行任务及其后继闭包标记 Retired**（IsCompleted 为 true，不代表执行成功）。闭包遍历持有 shared_ptr，重复边不得产生悬空指针。调用方保证析构期间无并发 Submit/Wait（Context 单线程销毁，满足） |
| 5.4 | 异常策略 | 吞异常 + 存 exception_ptr + `SetErrorHandler` 回调（默认空），不终止进程；回调自身异常也被隔离；绝不中断依赖图 |
| 5.5 | Submit 契约 | 任意线程（含 job 内部）可调用；线程安全 |
| 5.6 | Filter 合并顺序 | 局部 multimap 按 **chunk 下标升序** 合入输出；用单元素 `insert(k, v)` 逐个插入（C++11 起非提示插入放等值区间上界 → 等值元素保持插入序），不用 range-insert |
| 5.7 | Context 成员顺序 | `JobSystem` unique_ptr **早声明**（成员逆序析构 → 晚于面板/Renderer 销毁） |
| 5.8 | 服务获取时机 | SceneRenderer 在每次 ParseScene/FilterDrawables 调用内获取 `OVSERVICE(OvTools::Jobs::JobSystem)`，构造函数不缓存引用；包括 DrawFrame 内反射过滤 |
| 5.9 | 小场景快速路径 | ParseScene 输入 < `max(128, 2 × worker数)` 时走串行；FilterDrawables 为 `max(256, 2 × worker数)`。两个阶段按本机测量分别调参 |
| 5.10 | per-chunk 输出 | vector 预先 reserve；multimap 无 reserve，局部构建后逐元素合并仍有二次节点分配/排序开销，不宣称已消除堆分配 |

---

## 6. 集成设计

### 6.1 ParseScene（`SceneRenderer.cpp:262-335`）

现状：单循环遍历 `scene.GetFastAccessComponents().modelRenderers`，逐 mesh 构造 `Drawable` push 进 `result.drawables`。

并行版：
```
const auto& modelRenderers = scene.GetFastAccessComponents().modelRenderers;
if (modelRenderers.size() < kParallelThreshold) → 原（保留）
else:
    chunkCount = clamp(4 × workerCount, 2, modelRenderers.size())
    chunks: vector<vector<Drawable>>，每个 reserve(估均值)
    每块一个 Submit（闭包捕获 scene 引用 + [begin,end) + 输出块引用）
    WaitAll → 按 chunk 下标序拼接进 result.drawables
```
**确定性**：块覆盖连续区间、块内顺序 = 原循环顺序、拼接按块序 → 输出与串行**逐元素一致**。
闭包捕获栈上/局部状态 → 契约：**WaitAll 必须在作用域退出前完成**（BeginFrame 内联等待，满足）。

### 6.2 FilterDrawables（`SceneRenderer.cpp:337-462`）

现状：单循环逐 drawable 剔除/过滤/分类进三个 multimap（opaques/transparents/ui）。

并行版：与 6.1 同构——按 `p_drawables.drawables` 连续区间切块，每块构建局部 `SceneFilteredDrawablesDescriptor`（三个空 multimap），WaitAll 后按块序合并（§5.6 逐元素 insert）。

**确定性论证**：multimap key = `(order, materialKey, distance)`；等值 key 下 C++11 非提示插入放等值区间上界 → 等值元素保持插入序；块序 == 全局 drawable 序 → 合并结果与串行一致。**不信任记忆，实施时用 §8.3 对拍实测**。

### 6.3 Context 接线（两处同构）

```cpp
// Context.h: 成员区靠前位置
#include <OvTools/Jobs/JobSystem.h>
std::unique_ptr<OvTools::Jobs::JobSystem> jobSystem;

// Context.cpp: 构造函数，先于现有 Provide 区（Editor:165 / Game:159 附近）
jobSystem = std::make_unique<OvTools::Jobs::JobSystem>();
ServiceLocator::Provide<OvTools::Jobs::JobSystem>(*jobSystem);
```
（实施时先读两个 Context.h 确认成员布局，把 jobSystem 放在成员声明区最前部。）

### 6.4 Tracy

- worker 线程入口：`tracy::SetThreadName("JobWorker-N")`；任务执行包 `ZoneScoped`。宏在无 `TRACY_ENABLE` 时为 no-op，可无条件调用（Publish 配置自动失效）。
- Parse/Filter 的并行区外层 `ZoneScopedN("ParseScene (parallel)")` 等，便于与 worker 条带对照。
- 这将是引擎里第一个被命名的非主线程（Tracy 捕获应出现 JobWorker-N）。

---

## 7. 线程安全论证（已逐项核实）

- ParseScene/FilterDrawables 各自在返回前 join，包括 DrawFrame 内反射 pass 的过滤；GL 调用仍由调用渲染接口的主线程独占。
- job 期间触及的数据均为 const 只读：`FastAccessComponents` 向量（update 阶段后才读）、`FTransform::GetWorldMatrix()`（急切维护）、`Actor::GetComponent()`（const）、`Material::GenerateStateMask()`（const）、Model/Mesh/Material 访问器、蒙皮矩阵 + poseVersion（`scene->Update` 已定型）。
- 写冲突为零：各 chunk 写各自的输出结构，合并单线程。
- 此论证限定当前引擎场景 CPU 数据路径；Tracy、驱动等依赖可有内部线程，不能声称全进程无其他线程。

---

## 8. 构建与验证

### 8.1 冒烟测试（临时文件，验证后删除）

tracy 宏无定义时 no-op → **可脱离引擎独立编译**，无需链接 tracy 库：

```
cl /nologo /std:c++20 /EHsc /W4 /I Sources\OvTools\include /I Dependencies\tracy ^
   smoke_test.cpp Sources\OvTools\src\OvTools\Jobs\JobSystem.cpp
```
（JobSystem.cpp 对 Detail 头用同目录相对 include，cl 自动解析。若 cl 不可用，退化为构建整个引擎后经编辑器验证。）

**必测用例**：
1. 基本 Submit + Wait（副作用可见）。
2. 依赖顺序：B 依赖 A，原子序检查（B 执行时 A 已完成）。
3. 菱形 A→(B,C)→D：D 时 A/B/C 均完成。
4. **千级链式依赖**（专压 §4.1 挂接协议）。
5. ParallelFor 求和 1M 元素 == 串行和。
6. 单 worker + 主线程 Wait（Wait 参与路径）。
7. **job 抛异常：依赖图仍全部完成 + errorHandler 被调**（§4.2）。
8. job 内嵌套 Submit。
9. 默认句柄 = 已完成；Wait(无效) 立即返回；依赖列表含无效句柄被跳过。
10. 随机 DAG 1 万节点压力测试全部完成。
11. MutexDeque 与 ChaseLevDeque 跑同一套测试对拍（§4.3）。
12. MainThread 亲和任务经 PumpMainThread 执行。

### 8.2 引擎构建

1. `gen_proj.bat`（→ premake vs2022，重新生成工程收集新文件）。
2. 构建 Debug + Release（`Scripts/Windows/Build.bat`，实施时核实确切脚本名；或 msbuild）。
3. `fatalwarnings All` 下零警告。

### 8.3 运行时验收（确定性 + 性能）

1. **对拍**（临时 Debug-only 代码，验证后删）：在 `BeginFrame` 里同时跑串行与并行 ParseScene/FilterDrawables，断言输出逐元素相等（multimap 等值 key 顺序也须一致，实测 §5.6 论证）。
2. Tracy 捕获：出现 JobWorker-N 线程；Parse/Filter 呈多 worker 扇形；对比主线程序帧时长。
3. 后台隐藏 OpenGL 窗口，使用真实模型/蒙皮/材质做串行与并行离屏 RGBA 对拍，必须检查非背景像素并真正 SetDescriptor 替换串行输入。此验证不替代编辑器实际项目场景/完整交互验收。

---

## 9. 实施顺序（checklist，按序执行）

1. [x] `include/OvTools/Jobs/EAffinity.h` + `Detail/ControlBlock.h` + `JobHandle.h`
2. [x] `src/OvTools/Jobs/WorkStealingDeque.h`：先 MutexDeque，后 ChaseLevDeque（含 tag/内存序/对齐）
3. [x] `JobSystem.h` + `.inl` + `.cpp`：Submit / Wait / WaitAll / PumpMainThread / ParallelFor / 析构（§4 全协议）
4. [x] 冒烟测试独立编译全绿（双 deque 对拍）
5. [x] Context 接线 ×2（§6.3，注意 §5.7 成员顺序）
6. [x] ParseScene 并行 + 阈值快速路径（§6.1）
7. [x] FilterDrawables 并行 + 块序合并（§6.2，§5.6）
8. [x] 最终 Debug/x64 + Release/x64 全解决方案增量构建已完成（2026-09-06），均 0 警告、0 错误；保持 /WX，本次命令关闭无关的全局 vcpkg DLL 收集步骤。日志：Build/JobsValidation/msbuild-{debug,release}-verified.log
9. [x] 对拍、离屏图像及 Tracy 线程/等待采集验收已通过（2026-09-06，见 MULTITHREADING_TRACY_VALIDATION.md）；采集为独立引擎负载，不等同于完整编辑器帧。临时验证文件集中于忽略目录 Build/JobsValidation，未插入生产源码

---

## 10. 代码规范（repo 惯例，必须遵守）

- 文件头：`/** @project: Overload / @author: Overload Tech. / @licence: MIT */` + `#pragma once`。
- 命名空间 `OvTools::Jobs`（内部细节 `OvTools::Jobs::Detail`）；成员前缀 `m_`；参数前缀 `p_`。
- 模板实现放 `.inl`，头文件底部 include；非模板类 .h 声明 / .cpp 定义。
- 公开成员 doxygen `/** ... @param ... */`，**代码注释用英文**（repo 全英文）。
- 零警告（`fatalwarnings All`）；C++20。

## 11. 风险与缓解

| 风险 | 缓解 |
|---|---|
| Chase-Lev 实现错误（偶发数据竞争） | §4.3 双实现 + 压力对拍；先锁版保正确性 |
| 挂接协议丢失唤醒（静默死锁） | §4.1 明确锁协议 + 冒烟用例 4（千级链） |
| multimap 等值 key 顺序不确定 | §5.6 逐元素 insert + §8.3 实测对拍 |
| 小场景任务调度开销倒挂 | §5.9 阈值快速路径 |
| Release 无断言掩盖误用 | §5.2 文档化契约；阶段 0 无 MainThread 生产者 |

## 12. 后续路线（本轮之后，不做）

阶段 1：蒙皮姿态评估 job 化（update 阶段，需组件状态快照设计）→ 命令录制层（DrawEntity 产出 DrawCommand 而非直接 GL）→ Render 线程（A 形态，MainThread 亲和启用、延迟删除队列）→ 并行录制（多 Pass/View 多命令列表）→ RHI 化（baregl 旁边起 Vulkan 后端，`.ovfx` 变体机制换 SPIR-V 后端复用）→ fiber 演进（完全体 C）。

参考：GDC 2015 *Parallelizing the Naughty Dog Engine Using Fibers*；GDC 2019 *DOOM Eternal: A New Benchmark for Graphics Performance*。
