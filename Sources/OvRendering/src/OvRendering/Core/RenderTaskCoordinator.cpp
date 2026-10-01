#include <OvRendering/Core/RenderTaskCoordinator.h>
#include <OvRendering/Core/DrawPacketBuilder.h>
#include <algorithm>
#include <exception>
#include <stdexcept>
using namespace OvRendering;
std::shared_ptr<const Data::PreparedFrameData> Core::RenderTaskCoordinator::Prepare(
    std::shared_ptr<const Data::RenderSceneSnapshot> scene, Data::PipelineState baseState,
    OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel)
{
    if (jobs && !jobs->IsMainThread()) throw std::logic_error("Prepare coordinator must own the JobSystem");
    if (!scene) throw std::invalid_argument("Missing scene snapshot");
    if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("View preparation cancelled");
    const auto view = ViewPreparer::BuildView(scene->GetView());
    const auto size = scene->GetMeshes().size();
    const size_t count = jobs ? std::min(std::max(size, size_t{1}), std::max(size_t{2}, size_t(jobs->GetWorkerCount()))) : 1;
    struct Result { MeshRangeResult value; std::exception_ptr error; bool succeeded = false; };
    std::vector<Result> results(count);
    std::vector<OvTools::Jobs::JobHandle> handles;
    handles.reserve(count); // No vector allocation may lose an already submitted handle.
    try
    {
        for (size_t slot = 0; slot < count; ++slot)
        {
            const auto begin = size * slot / count, end = size * (slot + 1) / count;
            auto task = [scene, view, begin, end, cancel, &result = results[slot]] {
                try
                {
                    result.value = MeshFeatureProcessor::PrepareRange(*scene, view, begin, end, cancel);
                    result.succeeded = true;
                }
                catch (...) { result.error = std::current_exception(); }
            };
            if (jobs)
            {
            handles.push_back(jobs->Submit(std::move(task)));
            }
            else task();
        }
    }
    catch (...)
    {
        if (jobs) jobs->WaitAllPassive(handles);
        throw;
    }
    if (jobs) jobs->WaitAllPassive(handles);
    std::vector<MeshRangeResult> ranges;
    ranges.reserve(count);
    for (auto& result : results)
    {
        if (result.error) std::rethrow_exception(result.error);
        if (!result.succeeded) throw std::runtime_error("Prepare task retired without successful output");
        ranges.push_back(std::move(result.value));
    }
    auto frame = ViewPreparer::BuildDrawLists(std::move(scene), view, std::move(ranges), baseState, cancel);
    const auto drawCount = frame->m_draws.size();
    struct BindingResult { BindingRangeResult value; std::exception_ptr error; bool succeeded = false; };
    std::vector<BindingResult> bindings(count);
    handles.clear();
    try
    {
        for (size_t slot = 0; slot < count; ++slot)
        {
            const auto begin = drawCount * slot / count, end = drawCount * (slot + 1) / count;
            auto task = [&frame, begin, end, slot, cancel, &result = bindings[slot]] {
                try { result.value = DrawPacketBuilder::PrepareRange(*frame, begin, end, slot == 0, cancel); result.succeeded = true; }
                catch (...) { result.error = std::current_exception(); }
            };
            if (jobs) handles.push_back(jobs->Submit(std::move(task)));
            else task();
        }
    }
    catch (...) { if (jobs) jobs->WaitAllPassive(handles); throw; }
    if (jobs) jobs->WaitAllPassive(handles);
    for (const auto& result : bindings)
    {
        if (result.error) std::rethrow_exception(result.error);
        if (!result.succeeded) throw std::runtime_error("Binding task retired without successful output");
    }
    frame->m_frameBindings.elapsedTime = view.input.elapsedTime;
    frame->m_viewBindings = std::move(*bindings[0].value.view);
    for (size_t slot = 0; slot < count; ++slot)
        for (size_t i = 0; i < bindings[slot].value.materials.size(); ++i)
            frame->m_draws[drawCount * slot / count + i].material = std::move(bindings[slot].value.materials[i]);
    if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("Binding preparation cancelled");
    return std::shared_ptr<const Data::PreparedFrameData>(std::move(frame));
}

Data::RenderCommandList Core::RenderTaskCoordinator::Record(std::shared_ptr<const Data::PreparedFrameData> frame,
    const MeshCommandRecorder& recorder, std::optional<baregl::Framebuffer::Snapshot> target,
    OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel)
{
    if (!frame) throw std::invalid_argument("Missing published frame");
    if (jobs && !jobs->IsMainThread()) throw std::logic_error("Record coordinator must own the JobSystem");
    if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("Command recording cancelled");
    const auto size = frame->GetDraws().size();
    const size_t count = jobs ? std::min(std::max(size, size_t{1}), std::max(size_t{2}, size_t(jobs->GetWorkerCount()))) : 1;
    struct Result { std::optional<Data::RenderCommandList> commands; std::exception_ptr error; };
    std::vector<Result> results(count);
    std::vector<OvTools::Jobs::JobHandle> handles;
    handles.reserve(count);
    Data::RenderCommandList commands;
    try
    {
        recorder.BeginView(commands, frame->GetView(), target);
        try
        {
            for (size_t slot = 0; slot < count; ++slot)
            {
                const auto begin = size * slot / count, end = size * (slot + 1) / count;
                auto task = [frame, target, &recorder, begin, end, cancel, &result = results[slot]] {
                    try { result.commands.emplace(recorder.RecordRange(frame, target, begin, end, cancel)); }
                    catch (...) { result.error = std::current_exception(); }
                };
                if (jobs) handles.push_back(jobs->Submit(std::move(task)));
                else task();
            }
        }
        catch (...) { if (jobs) jobs->WaitAllPassive(handles); throw; }
        if (jobs) jobs->WaitAllPassive(handles);
        for (const auto& result : results)
        {
            if (result.error) std::rethrow_exception(result.error);
            if (!result.commands) throw std::runtime_error("Record task retired without successful output");
        }
        if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("Command recording cancelled");
        for (auto& result : results) commands.Append(std::move(*result.commands));
        recorder.EndView(commands, size != 0);
        commands.Seal();
        return commands;
    }
    catch (...) { commands.FailRecording(); throw; }
}

namespace
{
    // Chunk count for a linear range of `size` items under the current worker budget.
    size_t RangeChunks(size_t size, OvTools::Jobs::JobSystem* jobs)
    {
        if (!jobs || size == 0) return 1;
        return std::min(std::max(size, size_t{1}), std::max(size_t{2}, size_t(jobs->GetWorkerCount())));
    }

    template<class Task>
    void RunFlat(std::vector<Task>& tasks, OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel)
    {
        std::vector<OvTools::Jobs::JobHandle> handles;
        handles.reserve(tasks.size());
        try
        {
            for (size_t i = 0; i < tasks.size(); ++i)
            {
                if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("Flat tasks cancelled");
                auto task = [&tasks, i] {
                    try { tasks[i].Run(); }
                    catch (...) { tasks[i].error = std::current_exception(); }
                };
                if (jobs) handles.push_back(jobs->Submit(std::move(task))); else task();
            }
        }
        catch (...) { if (jobs) jobs->WaitAllPassive(handles); throw; }
        if (jobs) jobs->WaitAllPassive(handles);
        for (auto& task : tasks)
            if (task.error) std::rethrow_exception(task.error);
        if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("Flat tasks cancelled");
    }
}
std::vector<std::shared_ptr<const Data::PreparedFrameData>> Core::RenderTaskCoordinator::PrepareViews(
    std::span<const std::shared_ptr<const Data::RenderSceneSnapshot>> scenes, Data::PipelineState baseState,
    OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel)
{
    if (jobs && !jobs->IsMainThread()) throw std::logic_error("View coordinator must own the JobSystem");
    const size_t viewCount = scenes.size();
    std::vector<std::shared_ptr<const Data::PreparedFrameData>> frames(viewCount);
    if (!viewCount) return frames;
    std::vector<Data::RenderView> views(viewCount);
    for (size_t i = 0; i < viewCount; ++i)
    {
        if (!scenes[i]) throw std::invalid_argument("Missing scene snapshot");
        views[i] = ViewPreparer::BuildView(scenes[i]->GetView());
    }
    // Flatten mesh ranges across views so a large main view shares the worker pool
    // with shadows/reflections instead of running as one serial view task.
    struct MeshTask
    {
        size_t view = 0, begin = 0, end = 0, slot = 0, total = 0;
        MeshRangeResult value;
        std::exception_ptr error;
        std::span<const std::shared_ptr<const Data::RenderSceneSnapshot>> scenes;
        std::span<const Data::RenderView> views;
        const std::atomic_bool* cancel = nullptr;
        void Run() { value = MeshFeatureProcessor::PrepareRange(*scenes[view], views[view], begin, end, cancel); }
    };
    std::vector<MeshTask> meshTasks;
    for (size_t v = 0; v < viewCount; ++v)
    {
        const auto size = scenes[v]->GetMeshes().size();
        const size_t chunks = RangeChunks(size, jobs);
        for (size_t slot = 0; slot < chunks; ++slot)
            meshTasks.push_back({v, size * slot / chunks, size * (slot + 1) / chunks, meshTasks.size(), 0, {}, {}, scenes, views, cancel});
    }
    for (auto& task : meshTasks) task.total = meshTasks.size();
    RunFlat(meshTasks, jobs, cancel);
    std::vector<std::vector<MeshRangeResult>> ranges(viewCount);
    for (auto& task : meshTasks) ranges[task.view].push_back(std::move(task.value));
    std::vector<std::unique_ptr<Data::PreparedFrameData>> built(viewCount);
    for (size_t v = 0; v < viewCount; ++v)
        built[v] = ViewPreparer::BuildDrawLists(scenes[v], views[v], std::move(ranges[v]), baseState, cancel);
    // Flatten binding ranges the same way; slot 0 of each view owns the view bindings.
    struct BindTask
    {
        size_t view = 0, begin = 0, end = 0;
        bool prepareView = false;
        BindingRangeResult value;
        std::exception_ptr error;
        std::vector<std::unique_ptr<Data::PreparedFrameData>>* built = nullptr;
        const std::atomic_bool* cancel = nullptr;
        void Run() { value = DrawPacketBuilder::PrepareRange(*(*built)[view], begin, end, prepareView, cancel); }
    };
    std::vector<BindTask> bindTasks;
    for (size_t v = 0; v < viewCount; ++v)
    {
        const auto size = built[v]->m_draws.size();
        const size_t chunks = RangeChunks(size, jobs);
        for (size_t slot = 0; slot < chunks; ++slot)
            bindTasks.push_back({v, size * slot / chunks, size * (slot + 1) / chunks, slot == 0, {}, {}, &built, cancel});
    }
    RunFlat(bindTasks, jobs, cancel);
    std::vector<std::vector<BindingRangeResult>> bindings(viewCount);
    for (auto& task : bindTasks) bindings[task.view].push_back(std::move(task.value));
    for (size_t v = 0; v < viewCount; ++v)
    {
        auto& frame = *built[v];
        const auto drawCount = frame.m_draws.size();
        frame.m_frameBindings.elapsedTime = views[v].input.elapsedTime;
        frame.m_viewBindings = std::move(*bindings[v][0].view);
        const auto chunks = bindings[v].size();
        for (size_t slot = 0; slot < chunks; ++slot)
            for (size_t i = 0; i < bindings[v][slot].materials.size(); ++i)
                frame.m_draws[drawCount * slot / chunks + i].material = std::move(bindings[v][slot].materials[i]);
        frames[v] = std::shared_ptr<const Data::PreparedFrameData>(std::move(built[v]));
    }
    return frames;
}
std::vector<Data::RenderCommandList> Core::RenderTaskCoordinator::RecordViews(
    std::span<const std::shared_ptr<const Data::PreparedFrameData>> frames,
    std::span<const std::optional<baregl::Framebuffer::Snapshot>> targets,
    const MeshCommandRecorder& recorder, OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel)
{
    if (frames.size() != targets.size()) throw std::invalid_argument("View/target count mismatch");
    if (jobs && !jobs->IsMainThread()) throw std::logic_error("View coordinator must own the JobSystem");
    const size_t viewCount = frames.size();
    std::vector<Data::RenderCommandList> commands(viewCount);
    if (!viewCount) return commands;
    struct RecordTask
    {
        size_t view = 0, begin = 0, end = 0, slot = 0, total = 0;
        std::optional<Data::RenderCommandList> value;
        std::exception_ptr error;
        std::span<const std::shared_ptr<const Data::PreparedFrameData>> frames;
        std::span<const std::optional<baregl::Framebuffer::Snapshot>> targets;
        const MeshCommandRecorder* recorder = nullptr;
        const std::atomic_bool* cancel = nullptr;
        void Run() { value.emplace(recorder->RecordRange(frames[view], targets[view], begin, end, cancel)); }
    };
    std::vector<RecordTask> tasks;
    for (size_t v = 0; v < viewCount; ++v)
    {
        if (!frames[v]) throw std::invalid_argument("Missing published frame");
        const auto size = frames[v]->GetDraws().size();
        const size_t chunks = RangeChunks(size, jobs);
        for (size_t slot = 0; slot < chunks; ++slot)
            tasks.push_back({v, size * slot / chunks, size * (slot + 1) / chunks, tasks.size(), 0, {}, {}, frames, targets, &recorder, cancel});
    }
    for (auto& task : tasks) task.total = tasks.size();
    RunFlat(tasks, jobs, cancel);
    std::vector<std::vector<Data::RenderCommandList>> blocks(viewCount);
    for (auto& task : tasks) blocks[task.view].push_back(std::move(*task.value));
    for (size_t v = 0; v < viewCount; ++v)
    {
        const auto size = frames[v]->GetDraws().size();
        recorder.BeginView(commands[v], frames[v]->GetView(), targets[v]);
        for (auto& block : blocks[v]) commands[v].Append(std::move(block));
        recorder.EndView(commands[v], size != 0);
        commands[v].Seal();
    }
    return commands;
}
