#include <OvRendering/Core/ViewPreparer.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <tuple>
#include <stdexcept>

using namespace OvRendering;

std::shared_ptr<const Data::PreparedFrameData> Core::ViewPreparer::Prepare(
    std::shared_ptr<const Data::RenderSceneSnapshot> p_scene,
    Data::PipelineState p_baseState, const std::atomic_bool* p_cancel)
{
    const auto checkCancel = [&] {
        if (p_cancel && p_cancel->load(std::memory_order_acquire))
            throw std::runtime_error("View preparation cancelled");
    };
    checkCancel();
    if (!p_scene) throw std::invalid_argument("Missing scene snapshot");
    auto result = std::unique_ptr<Data::PreparedFrameData>(new Data::PreparedFrameData);
    result->m_scene = std::move(p_scene);
    result->m_preparationThread = std::this_thread::get_id();
    auto& view = result->m_view;
    view.input = result->m_scene->GetView();
    const auto& input = view.input;
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
    // Preserve SceneRenderPass's actual unconditional stencil behavior (A0).
    p_baseState.stencilTest = true;
    p_baseState.stencilWriteMask = 0xFF;
    p_baseState.stencilFuncRef = 1;
    p_baseState.stencilFuncMask = 0xFF;
    p_baseState.stencilOpFail = baregl::types::EOperation::REPLACE;
    p_baseState.depthOpFail = baregl::types::EOperation::REPLACE;
    p_baseState.bothOpFail = baregl::types::EOperation::REPLACE;
    const auto meshes = result->m_scene->GetMeshes();
    std::vector<std::pair<size_t, float>> visible;
    visible.reserve(meshes.size());
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        checkCancel();
        const auto& mesh = meshes[i];
        if (!mesh.geometry || !mesh.material || !mesh.instances)
            throw std::invalid_argument("Incomplete static draw input");
        if (view.frustum && mesh.worldBounds)
        {
            const auto& b = *mesh.worldBounds;
            if (!view.frustum->SphereInFrustum(b.position.x, b.position.y, b.position.z, b.radius)) continue;
        }
        const float distance = OvMaths::FVector3::Distance(mesh.position, input.position);
        if (!std::isfinite(distance)) throw std::invalid_argument("Invalid object distance");
        visible.emplace_back(i, distance);
    }
    std::stable_sort(visible.begin(), visible.end(), [&](const auto& a, const auto& b) {
        const auto& x = meshes[a.first]; const auto& y = meshes[b.first];
        return std::tie(x.drawOrder, x.materialGroup, a.second) < std::tie(y.drawOrder, y.materialGroup, b.second);
    });
    for (const auto& [index, distance] : visible)
    {
        checkCancel();
        const auto& mesh = meshes[index];
        auto state = p_baseState;
        state.depthWriting = mesh.state.depthWriting;
        state.colorWriting.mask = mesh.state.colorWriting ? 0xF : 0;
        state.blending = mesh.state.blendable;
        state.culling = mesh.state.frontfaceCulling || mesh.state.backfaceCulling;
        state.depthTest = mesh.state.depthTest;
        if (state.culling) state.cullFace = mesh.state.backfaceCulling ?
            (mesh.state.frontfaceCulling ? baregl::types::ECullFace::FRONT_AND_BACK : baregl::types::ECullFace::BACK) :
            baregl::types::ECullFace::FRONT;
        result->m_draws.push_back({index, state, OvMaths::FMatrix4::Transpose(mesh.model), mesh.user});
    }
    checkCancel();
    return std::shared_ptr<const Data::PreparedFrameData>(std::move(result));
}
