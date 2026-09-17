#include <OvRendering/Core/MeshFeatureProcessor.h>
#include <cmath>
#include <stdexcept>
using namespace OvRendering;
Core::MeshRangeResult Core::MeshFeatureProcessor::PrepareRange(const Data::RenderSceneSnapshot& scene,
    const Data::RenderView& view, size_t begin, size_t end, const std::atomic_bool* cancel)
{
    MeshRangeResult result;
    result.thread = std::this_thread::get_id();
    const auto meshes = scene.GetMeshes();
    if (begin > end || end > meshes.size()) throw std::invalid_argument("Invalid candidate range");
    for (size_t i = begin; i < end; ++i)
    {
        if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("View preparation cancelled");
        const auto& mesh = meshes[i];
        if (!mesh.geometry || !mesh.material || !mesh.instances)
            throw std::invalid_argument("Incomplete static draw input");
        if (view.frustum && mesh.worldBounds)
        {
            const auto& b = *mesh.worldBounds;
            if (!view.frustum->SphereInFrustum(b.position.x, b.position.y, b.position.z, b.radius)) continue;
        }
        const float distance = OvMaths::FVector3::Distance(mesh.position, view.input.position);
        if (!std::isfinite(distance)) throw std::invalid_argument("Invalid object distance");
        result.visible.push_back({i, distance});
    }
    return result;
}
Data::PipelineState Core::MeshFeatureProcessor::BuildState(Data::PipelineState state, const Data::StateMask& material, Data::RenderStage stage)
{
    if (stage == Data::RenderStage::OverlayUI)
    {
        // Overlay owns its state. Do not inherit editor wireframe, stencil or
        // scissor settings. Rectangular clipping is resolved in the UI snapshot.
        state = Data::PipelineState{};
        state.stencilTest = state.scissorTest = state.depthTest = state.depthWriting = false;
        state.blending = true; state.culling = false; state.colorWriting.mask = 0xF;
        state.blendingSrcFactor = baregl::types::EBlendingFactor::SRC_ALPHA;
        state.blendingDestFactor = baregl::types::EBlendingFactor::ONE_MINUS_SRC_ALPHA;
        state.blendingEquation = baregl::types::EBlendingEquation::FUNC_ADD;
        return state;
    }
    // All three old SceneRenderPass implementations unconditionally set stencil.
    if (stage == Data::RenderStage::Opaque || stage == Data::RenderStage::Transparent || stage == Data::RenderStage::SceneUI)
    {
    state.stencilTest = true;
    state.stencilWriteMask = 0xFF;
    state.stencilFuncRef = 1;
    state.stencilFuncMask = 0xFF;
    state.stencilOpFail = baregl::types::EOperation::REPLACE;
    state.depthOpFail = baregl::types::EOperation::REPLACE;
    state.bothOpFail = baregl::types::EOperation::REPLACE;
    }
    state.depthWriting = material.depthWriting;
    state.colorWriting.mask = material.colorWriting ? 0xF : 0;
    state.blending = material.blendable;
    state.culling = material.frontfaceCulling || material.backfaceCulling;
    state.depthTest = material.depthTest;
    if (state.culling) state.cullFace = material.backfaceCulling ?
        (material.frontfaceCulling ? baregl::types::ECullFace::FRONT_AND_BACK : baregl::types::ECullFace::BACK) :
        baregl::types::ECullFace::FRONT;
    return state;
}
