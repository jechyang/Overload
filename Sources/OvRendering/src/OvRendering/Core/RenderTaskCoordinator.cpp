#include <OvRendering/Core/RenderTaskCoordinator.h>
#include <algorithm>
#include <exception>
#include <stdexcept>
using namespace OvRendering;
std::shared_ptr<const Data::PreparedFrameData> Core::RenderTaskCoordinator::Prepare(
    std::shared_ptr<const Data::RenderSceneSnapshot> scene, Data::PipelineState baseState,
    OvTools::Jobs::JobSystem* jobs, const std::atomic_bool* cancel)
{
    if (!jobs) return ViewPreparer::Prepare(std::move(scene), baseState, cancel);
    if (!jobs->IsMainThread()) throw std::logic_error("Prepare coordinator must own the JobSystem");
    if (!scene) throw std::invalid_argument("Missing scene snapshot");
    if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("View preparation cancelled");
    const auto view = ViewPreparer::BuildView(scene->GetView());
    const auto size = scene->GetMeshes().size();
    const size_t count = std::min(std::max(size, size_t{1}), std::max(size_t{2}, size_t(jobs->GetWorkerCount())));
    struct Result { MeshRangeResult value; std::exception_ptr error; bool succeeded = false; };
    std::vector<Result> results(count);
    std::vector<OvTools::Jobs::JobHandle> handles;
    handles.reserve(count); // No vector allocation may lose an already submitted handle.
    try
    {
        for (size_t slot = 0; slot < count; ++slot)
        {
            const auto begin = size * slot / count, end = size * (slot + 1) / count;
            handles.push_back(jobs->Submit([scene, view, begin, end, cancel, &result = results[slot]] {
                try
                {
                    result.value = MeshFeatureProcessor::PrepareRange(*scene, view, begin, end, cancel);
                    result.succeeded = true;
                }
                catch (...) { result.error = std::current_exception(); }
            }));
        }
    }
    catch (...)
    {
        jobs->WaitAllPassive(handles);
        throw;
    }
    jobs->WaitAllPassive(handles);
    std::vector<MeshRangeResult> ranges;
    ranges.reserve(count);
    for (auto& result : results)
    {
        if (result.error) std::rethrow_exception(result.error);
        if (!result.succeeded) throw std::runtime_error("Prepare task retired without successful output");
        ranges.push_back(std::move(result.value));
    }
    return ViewPreparer::Publish(std::move(scene), view, std::move(ranges), baseState, cancel);
}
