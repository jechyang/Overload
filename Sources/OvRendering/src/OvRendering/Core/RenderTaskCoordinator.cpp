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
    template<class Result, class Function>
    std::vector<Result> RunViews(size_t count, OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel, Function function)
    {
        if (jobs && !jobs->IsMainThread()) throw std::logic_error("View coordinator must own the JobSystem");
        struct Slot { std::optional<Result> value; std::exception_ptr error; };
        std::vector<Slot> slots(count);
        std::vector<OvTools::Jobs::JobHandle> handles; handles.reserve(count);
        try
        {
            for (size_t i=0; i<count; ++i)
            {
                if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("View tasks cancelled");
                auto task = [&,i] {
                    try { slots[i].value.emplace(function(i)); }
                    catch (...) { slots[i].error = std::current_exception(); }
                };
                if (jobs) handles.push_back(jobs->Submit(std::move(task))); else task();
            }
        }
        catch (...) { if (jobs) jobs->WaitAllPassive(handles); throw; }
        if (jobs) jobs->WaitAllPassive(handles);
        for (const auto& slot : slots)
        {
            if (slot.error) std::rethrow_exception(slot.error);
            if (!slot.value) throw std::runtime_error("View task retired without output");
        }
        if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("View tasks cancelled");
        std::vector<Result> result; result.reserve(count);
        for (auto& slot : slots) result.push_back(std::move(*slot.value));
        return result;
    }
}
std::vector<std::shared_ptr<const Data::PreparedFrameData>> Core::RenderTaskCoordinator::PrepareViews(
    std::span<const std::shared_ptr<const Data::RenderSceneSnapshot>> scenes, Data::PipelineState baseState,
    OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel)
{
    return RunViews<std::shared_ptr<const Data::PreparedFrameData>>(scenes.size(), jobs, cancel,
        [&](size_t i) { return Prepare(scenes[i],baseState,nullptr,cancel); });
}
std::vector<Data::RenderCommandList> Core::RenderTaskCoordinator::RecordViews(
    std::span<const std::shared_ptr<const Data::PreparedFrameData>> frames,
    std::span<const std::optional<baregl::Framebuffer::Snapshot>> targets,
    const MeshCommandRecorder& recorder, OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel)
{
    if (frames.size() != targets.size()) throw std::invalid_argument("View/target count mismatch");
    return RunViews<Data::RenderCommandList>(frames.size(),jobs,cancel,
        [&](size_t i) { return Record(frames[i],recorder,targets[i],nullptr,cancel); });
}
