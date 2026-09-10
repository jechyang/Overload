#include <OvRendering/Core/ViewPreparer.h>
#include <OvRendering/Core/RenderTaskCoordinator.h>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <stdexcept>
using namespace OvRendering;

Data::RenderView Core::ViewPreparer::BuildView(const Data::RenderViewInput& input)
{
    Data::RenderView view;
    view.input = input;
    if (!input.width || !input.height || !std::isfinite(input.elapsedTime))
        throw std::invalid_argument("Invalid render view dimensions or time");
    const auto target = input.position + input.forward;
    view.view = OvMaths::FMatrix4::CreateView(input.position.x, input.position.y, input.position.z,
        target.x, target.y, target.z, input.up.x, input.up.y, input.up.z);
    const float ratio = input.width / static_cast<float>(input.height);
    switch (input.projectionMode)
    {
    case Settings::EProjectionMode::ORTHOGRAPHIC:
        view.projection = OvMaths::FMatrix4::CreateOrthographic(input.size, ratio, input.nearPlane, input.farPlane);
        break;
    case Settings::EProjectionMode::PERSPECTIVE:
        view.projection = OvMaths::FMatrix4::CreatePerspective(input.fov, ratio, input.nearPlane, input.farPlane);
        break;
    default: throw std::invalid_argument("Unsupported view projection");
    }
    if (input.cullGeometry)
    {
        view.frustum = input.frustumOverride;
        if (!view.frustum)
        {
            view.frustum.emplace();
            view.frustum->CalculateFrustum(view.projection * view.view);
        }
    }
    return view;
}

std::shared_ptr<const Data::PreparedFrameData> Core::ViewPreparer::Prepare(
    std::shared_ptr<const Data::RenderSceneSnapshot> scene, Data::PipelineState baseState, const std::atomic_bool* cancel)
{
    return RenderTaskCoordinator::Prepare(std::move(scene), baseState, nullptr, cancel);
}

std::unique_ptr<Data::PreparedFrameData> Core::ViewPreparer::BuildDrawLists(
    std::shared_ptr<const Data::RenderSceneSnapshot> scene, Data::RenderView view,
    std::vector<MeshRangeResult> ranges, Data::PipelineState baseState, const std::atomic_bool* cancel)
{
    auto result = std::unique_ptr<Data::PreparedFrameData>(new Data::PreparedFrameData);
    result->m_scene = std::move(scene);
    result->m_view = std::move(view);
    result->m_scene->GetPipeline().Validate();
    std::vector<VisibleStaticMesh> visible;
    for (auto& range : ranges)
    {
        result->m_preparationThreads.push_back(range.thread);
        visible.insert(visible.end(), range.visible.begin(), range.visible.end());
    }
    result->m_preparationThread = ranges.empty() ? std::this_thread::get_id() : ranges.front().thread;
    const auto meshes = result->m_scene->GetMeshes();
    for (auto stage : result->m_scene->GetPipeline().stages)
    {
        std::vector<VisibleStaticMesh> list;
        for (const auto& candidate : visible)
            if (meshes[candidate.sourceIndex].stage == stage) list.push_back(candidate);
        SortStage(list, meshes, stage);
        result->m_lists.push_back({result->m_view.input.id, stage, result->m_draws.size(), list.size()});
        for (const auto& candidate : list)
        {
            if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("View preparation cancelled");
            const auto index = candidate.sourceIndex;
            const auto& mesh = meshes[index];
            const auto state = MeshFeatureProcessor::BuildState(baseState, mesh.state);
            result->m_draws.push_back({index, state, OvMaths::FMatrix4::Transpose(mesh.model), mesh.user, {}});
        }
    }
    if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("View preparation cancelled");
    return result;
}

void Core::ViewPreparer::SortStage(std::vector<VisibleStaticMesh>& list, std::span<const Data::MeshInput> meshes, Data::RenderStage stage)
{
    const auto sorting = Data::DescribeStage(stage).sorting;
    // Whole-list sort after deterministic range merge. Never sort each chunk.
    std::stable_sort(list.begin(), list.end(), [&](const auto& a, const auto& b) {
        const auto& x = meshes[a.sourceIndex]; const auto& y = meshes[b.sourceIndex];
        if (x.drawOrder != y.drawOrder) return x.drawOrder < y.drawOrder;
        if (sorting == Data::DrawSorting::MaterialFrontToBack && x.materialGroup != y.materialGroup)
        return x.materialGroup < y.materialGroup;
        return sorting == Data::DrawSorting::BackToFront ? a.distance > b.distance : a.distance < b.distance;
    });
}
