#include <OvRendering/Core/DrawPacketBuilder.h>
#include <cmath>
#include <stdexcept>
using namespace OvRendering;
Core::BindingRangeResult Core::DrawPacketBuilder::PrepareRange(const Data::PreparedFrameData& frame,
    size_t begin, size_t end, bool prepareView, const std::atomic_bool* cancel)
{
    BindingRangeResult result;
    const auto check = [&] { if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("Binding preparation cancelled"); };
    check();
    if (begin > end || end > frame.GetDraws().size()) throw std::invalid_argument("Invalid binding range");
    if (prepareView)
    {
        const auto& view = frame.GetView();
        Data::ViewBindings binding;
        binding.viewUpload = OvMaths::FMatrix4::Transpose(view.view);
        binding.projectionUpload = OvMaths::FMatrix4::Transpose(view.projection);
        binding.camera = view.input.position;
        auto frustum = view.input.frustumOverride;
        if (view.input.cullLights && !frustum)
        {
            frustum.emplace(); frustum->CalculateFrustum(view.projection * view.view);
        }
        for (const auto& light : frame.GetScene().GetLights())
        {
            check();
            if (!view.input.cullLights || std::isinf(light.range) ||
                frustum->SphereInFrustum(light.position.x, light.position.y, light.position.z, light.range))
                binding.lights.push_back(light.matrix);
        }
        result.view = std::move(binding);
    }
    result.materials.reserve(end - begin);
    for (size_t i = begin; i < end; ++i)
    {
        check();
        const auto& source = frame.GetScene().GetMeshes()[frame.GetDraws()[i].sourceIndex];
        result.materials.push_back(Data::Material::EncodeSnapshot(source.material));
    }
    check();
    return result;
}
Data::EngineConstants Core::DrawPacketBuilder::Engine(const Data::PreparedFrameData& frame, const Data::PreparedMeshDraw& draw)
{
    const auto& view = frame.GetViewBindings();
    return {draw.modelUpload, view.viewUpload, view.projectionUpload, view.camera, frame.GetFrameBindings().elapsedTime, draw.userUpload};
}
