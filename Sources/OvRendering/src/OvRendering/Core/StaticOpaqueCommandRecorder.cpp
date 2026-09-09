#include <OvRendering/Core/StaticOpaqueCommandRecorder.h>
#include <OvRendering/Context/Driver.h>
#include <baregl/ResourceExecutor.h>
#include <baregl/detail/glad/glad.h>
#include <algorithm>
#include <array>
#include <span>
#include <string_view>
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
    // std140 includes the final 16-byte rounding (the legacy probe payload is 124 bytes).
    struct alignas(16) NoReflectionConstants
    {
        std::array<float, 4> position{};
        std::array<float, 16> rotation{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        std::array<float, 4> boxCenter{}, boxHalfExtents{};
        float brightness = 0;
        int32_t boxProjection = 0, local = 0, padding = 0;
    };
    static_assert(sizeof(NoReflectionConstants) == 128 && offsetof(NoReflectionConstants, brightness) == 112
        && offsetof(NoReflectionConstants, boxProjection) == 116 && offsetof(NoReflectionConstants, local) == 120);
    struct Member { std::string_view name; GLint offset; GLenum type; };
    constexpr Member engineMembers[] = {{"ubo_Model",0,GL_FLOAT_MAT4}, {"ubo_View",64,GL_FLOAT_MAT4},
        {"ubo_Projection",128,GL_FLOAT_MAT4}, {"ubo_ViewPos",192,GL_FLOAT_VEC3}, {"ubo_Time",204,GL_FLOAT},
        {"ubo_UserMatrix",208,GL_FLOAT_MAT4}};
    constexpr Member reflectionMembers[] = {{"position",0,GL_FLOAT_VEC4}, {"rotation",16,GL_FLOAT_MAT4},
        {"boxCenter",80,GL_FLOAT_VEC4}, {"boxHalfExtents",96,GL_FLOAT_VEC4}, {"brightness",112,GL_FLOAT},
        {"boxProjection",116,GL_BOOL}, {"local",120,GL_BOOL}};
    static_assert(sizeof(EngineConstants) == 272 && offsetof(EngineConstants, user) == 208);
}

void Core::StaticOpaqueCommandRecorder::ValidateProgram(const baregl::ShaderProgram& p_program)
{
    baregl::ResourceExecutor::Invoke([&] {
        const auto program = p_program.GetID();
        GLint count = 0;
        glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &count);
        for (GLint block = 0; block < count; ++block)
        {
            std::array<char, 256> name{};
            glGetActiveUniformBlockName(program, block, GLsizei(name.size()), nullptr, name.data());
            const std::string blockName(name.data());
            const bool engine = blockName == "EngineUBO";
            if (!engine && blockName != "ReflectionUBO")
                throw std::invalid_argument("A1 has no binding provider for uniform block: " + std::string(blockName));
            GLint binding = 0, size = 0, members = 0;
            glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_BINDING, &binding);
            glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
            if (binding != (engine ? 0 : 1) || size <= 0 || size > static_cast<GLint>(engine ? sizeof(EngineConstants) : sizeof(NoReflectionConstants)))
                throw std::invalid_argument("A1 uniform block binding/size mismatch: " + std::string(blockName));
            glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &members);
            std::vector<GLint> indices(members);
            glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES, indices.data());
            const std::span<const Member> expected = engine ? std::span<const Member>(engineMembers) : std::span<const Member>(reflectionMembers);
            for (const auto index : indices)
            {
                const GLuint uniform = static_cast<GLuint>(index);
                GLint arraySize = 0, offset = 0, stride = 0, arrayStride = 0, rowMajor = 0;
                GLenum type = 0;
                glGetActiveUniform(program, uniform, GLsizei(name.size()), nullptr, &arraySize, &type, name.data());
                std::string_view memberName(name.data());
                if (const auto dot = memberName.rfind('.'); dot != std::string_view::npos) memberName.remove_prefix(dot + 1);
                glGetActiveUniformsiv(program, 1, &uniform, GL_UNIFORM_OFFSET, &offset);
                glGetActiveUniformsiv(program, 1, &uniform, GL_UNIFORM_MATRIX_STRIDE, &stride);
                glGetActiveUniformsiv(program, 1, &uniform, GL_UNIFORM_ARRAY_STRIDE, &arrayStride);
                glGetActiveUniformsiv(program, 1, &uniform, GL_UNIFORM_IS_ROW_MAJOR, &rowMajor);
                const auto found = std::find_if(expected.begin(), expected.end(), [&](const auto& m) {
                    return m.name == memberName && m.offset == offset && m.type == type;
                });
                if (found == expected.end() || arraySize != 1 || arrayStride != 0 || rowMajor != 0 || stride != (type == GL_FLOAT_MAT4 ? 16 : 0))
                    throw std::invalid_argument("A1 uniform block layout mismatch: " + std::string(blockName));
            }
        }
    });
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
    m_noReflection = std::make_shared<baregl::Buffer>();
    m_noReflection->Allocate(sizeof(NoReflectionConstants), baregl::types::EAccessSpecifier::STREAM_DRAW);
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
                engine = m_engine, lights = m_emptyLights, reflection = m_noReflection](Context::Driver& driver) {
                if (p_target) p_target->Bind(); else baregl::Framebuffer::BindDefault();
                driver.SetViewport(0, 0, view.input.width, view.input.height);
                engine->Upload(&constants);
                engine->Bind(baregl::types::EBufferType::UNIFORM, 0);
                lights->Bind(baregl::types::EBufferType::SHADER_STORAGE, 0);
                const NoReflectionConstants noReflection;
                reflection->Upload(&noReflection);
                reflection->Bind(baregl::types::EBufferType::UNIFORM, 1);
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
