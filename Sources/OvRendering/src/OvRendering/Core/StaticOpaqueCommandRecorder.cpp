#include <OvRendering/Core/StaticOpaqueCommandRecorder.h>
#include <OvRendering/Context/Driver.h>
#include <array>
#include <cstddef>
#include <stdexcept>

using namespace OvRendering;
namespace
{
    struct EngineConstants
    {
        OvMaths::FMatrix4 model, view, projection;
        OvMaths::FVector3 camera;
        float elapsed;
        OvMaths::FMatrix4 user;
    };
    static_assert(sizeof(EngineConstants) == 272 && offsetof(EngineConstants, user) == 208);
}

Core::StaticOpaqueCommandRecorder::StaticOpaqueCommandRecorder()
{
    m_white = std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D);
    m_black = std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_CUBE);
    const baregl::data::TextureDesc desc{ .width = 1, .height = 1,
        .minFilter = baregl::types::ETextureFilteringMode::NEAREST, .magFilter = baregl::types::ETextureFilteringMode::NEAREST,
        .horizontalWrap = baregl::types::ETextureWrapMode::REPEAT, .verticalWrap = baregl::types::ETextureWrapMode::REPEAT,
        .internalFormat = baregl::types::EInternalFormat::RGBA8, .useMipMaps = false };
    const std::array<uint8_t, 4> white{255, 255, 255, 255};
    const std::array<uint8_t, 24> black{};
    m_white->Allocate(desc); m_white->Upload(white.data(), baregl::types::EFormat::RGBA, baregl::types::EPixelDataType::UNSIGNED_BYTE);
    m_black->Allocate(desc); m_black->Upload(black.data(), baregl::types::EFormat::RGBA, baregl::types::EPixelDataType::UNSIGNED_BYTE);
    m_engine = std::make_shared<baregl::Buffer>();
    m_engine->Allocate(sizeof(EngineConstants), baregl::types::EAccessSpecifier::STREAM_DRAW);
    m_emptyLights = std::make_shared<baregl::Buffer>();
}

Data::RenderCommandList Core::StaticOpaqueCommandRecorder::Record(
    std::shared_ptr<const Data::PreparedFrameData> p_frame,
    std::optional<baregl::Framebuffer::Snapshot> p_target) const
{
    if (!p_frame || p_frame->GetView().stage != Data::RenderStage::Opaque)
        throw std::invalid_argument("Static recorder requires published Opaque data");
    Data::RenderCommandList commands;
    try
    {
        const auto view = p_frame->GetView();
        commands.Append([view, p_target](Context::Driver& driver) {
            if (p_target) p_target->Bind(); else baregl::Framebuffer::BindDefault();
            driver.SetViewport(0, 0, view.input.width, view.input.height);
            driver.Clear(view.input.clearColor, view.input.clearDepth, view.input.clearStencil, view.input.clearValue);
        });
        for (const auto& draw : p_frame->GetDraws())
        {
            const auto& source = p_frame->GetScene().GetMeshes()[draw.sourceIndex];
            auto material = Data::Material::EncodeSnapshot(source.material);
            const EngineConstants constants{draw.modelUpload, OvMaths::FMatrix4::Transpose(view.view),
                OvMaths::FMatrix4::Transpose(view.projection), view.input.position, view.input.elapsedTime, draw.userUpload};
            commands.Append([geometry = source.geometry, material = std::move(material),
                instances = source.instances, state = draw.pipelineState, constants, view, p_target,
                engine = m_engine, lights = m_emptyLights](Context::Driver& driver) {
                if (p_target) p_target->Bind(); else baregl::Framebuffer::BindDefault();
                driver.SetViewport(0, 0, view.input.width, view.input.height);
                engine->Upload(&constants);
                engine->Bind(baregl::types::EBufferType::UNIFORM, 0);
                lights->Bind(baregl::types::EBufferType::SHADER_STORAGE, 0);
                material->Execute();
                driver.Draw(state, *geometry, baregl::types::EPrimitiveMode::TRIANGLES, instances);
            });
        }
        commands.Append([engine = m_engine, lights = m_emptyLights, hasDraws = !p_frame->GetDraws().empty()](Context::Driver&) {
            if (hasDraws) { engine->Unbind(); lights->Unbind(); }
            baregl::Framebuffer::BindDefault();
        });
        commands.Seal();
        return commands;
    }
    catch (...) { commands.FailRecording(); throw; }
}
