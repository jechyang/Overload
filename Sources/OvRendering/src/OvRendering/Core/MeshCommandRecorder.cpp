#include <OvRendering/Core/MeshCommandRecorder.h>
#include <OvRendering/Context/Driver.h>
#include <OvRendering/Core/PipelineRunner.h>
#include <OvRendering/Core/DrawPacketBuilder.h>
#include <OvRendering/Core/RenderTaskCoordinator.h>
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
    using EngineConstants = Data::EngineConstants;
    using NoReflectionConstants = Data::ReflectionConstants;
    struct Member { std::string_view name; GLint offset; GLenum type; };
    constexpr Member engineMembers[] = {{"ubo_Model",0,GL_FLOAT_MAT4}, {"ubo_View",64,GL_FLOAT_MAT4},
        {"ubo_Projection",128,GL_FLOAT_MAT4}, {"ubo_ViewPos",192,GL_FLOAT_VEC3}, {"ubo_Time",204,GL_FLOAT},
        {"ubo_UserMatrix",208,GL_FLOAT_MAT4}};
    constexpr Member reflectionMembers[] = {{"position",0,GL_FLOAT_VEC4}, {"rotation",16,GL_FLOAT_MAT4},
        {"boxCenter",80,GL_FLOAT_VEC4}, {"boxHalfExtents",96,GL_FLOAT_VEC4}, {"brightness",112,GL_FLOAT},
        {"boxProjection",116,GL_BOOL}, {"local",120,GL_BOOL}};
    static_assert(sizeof(EngineConstants) == 272 && offsetof(EngineConstants, user) == 208);
}

bool Core::MeshCommandRecorder::ProgramCache::Validate(const std::shared_ptr<baregl::ShaderProgram>& program)
{
    if (!program) throw std::invalid_argument("Missing shader program generation");
    if (const auto found=m_entries.find(program.get()); found!=m_entries.end() && !found->second.program.expired())
        return found->second.skinning;
    std::erase_if(m_entries,[](const auto& entry){return entry.second.program.expired();});
    const bool skinning=MeshCommandRecorder::ValidateProgram(*program);
    m_entries.insert_or_assign(program.get(),Entry{program,skinning});
    return skinning;
}

bool Core::MeshCommandRecorder::ValidateProgram(const baregl::ShaderProgram& p_program)
{
    bool skinning = false;
    baregl::ResourceExecutor::Invoke([&] {
        const auto program = p_program.GetID();
        GLint storageBlocks = 0;
        glGetProgramInterfaceiv(program, GL_SHADER_STORAGE_BLOCK, GL_ACTIVE_RESOURCES, &storageBlocks);
        for (GLint block = 0; block < storageBlocks; ++block)
        {
            std::array<char, 256> name{};
            glGetProgramResourceName(program, GL_SHADER_STORAGE_BLOCK, block, GLsizei(name.size()), nullptr, name.data());
            const std::string_view blockName(name.data());
            if (blockName != "LightSSBO" && blockName != "SkinningSSBO")
                throw std::invalid_argument("Scene has no storage binding provider: " + std::string(blockName));
            constexpr GLenum properties[] = {GL_BUFFER_BINDING, GL_NUM_ACTIVE_VARIABLES, GL_REFERENCED_BY_VERTEX_SHADER};
            GLint values[3]{};
            glGetProgramResourceiv(program, GL_SHADER_STORAGE_BLOCK, block, 3, properties, 3, nullptr, values);
            skinning = skinning || (blockName == "SkinningSSBO" && values[2] != 0);
            if (values[0] != (blockName == "LightSSBO" ? 0 : 1) || values[1] != 1)
                throw std::invalid_argument("Scene storage binding mismatch");
            constexpr GLenum active = GL_ACTIVE_VARIABLES;
            GLint variable = 0;
            glGetProgramResourceiv(program, GL_SHADER_STORAGE_BLOCK, block, 1, &active, 1, nullptr, &variable);
            constexpr GLenum layout[] = {GL_TYPE, GL_OFFSET, GL_ARRAY_STRIDE, GL_MATRIX_STRIDE, GL_IS_ROW_MAJOR};
            GLint fields[5]{};
            glGetProgramResourceiv(program, GL_BUFFER_VARIABLE, variable, 5, layout, 5, nullptr, fields);
            if (fields[0] != GL_FLOAT_MAT4 || fields[1] != 0 || fields[2] != 64 || fields[3] != 16 || fields[4] != 0)
                throw std::invalid_argument("Scene storage layout mismatch");
        }
        GLint count = 0;
        glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &count);
        for (GLint block = 0; block < count; ++block)
        {
            std::array<char, 256> name{};
            glGetActiveUniformBlockName(program, block, GLsizei(name.size()), nullptr, name.data());
            const std::string blockName(name.data());
            const bool engine = blockName == "EngineUBO";
            if (!engine && blockName != "ReflectionUBO")
                throw std::invalid_argument("Static scene has no binding provider for uniform block: " + std::string(blockName));
            GLint binding = 0, size = 0, members = 0;
            glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_BINDING, &binding);
            glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
            if (binding != (engine ? 0 : 1) || size <= 0 || size > static_cast<GLint>(engine ? sizeof(EngineConstants) : sizeof(NoReflectionConstants)))
                throw std::invalid_argument("Static scene uniform block binding/size mismatch: " + std::string(blockName));
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
                    throw std::invalid_argument("Static scene uniform block layout mismatch: " + std::string(blockName));
            }
        }
    });
    return skinning;
}

Core::MeshCommandRecorder::MeshCommandRecorder()
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
    m_lights = std::make_shared<baregl::Buffer>();
    m_skinning = std::make_shared<baregl::Buffer>();
    m_identity = std::make_shared<baregl::Buffer>();
    const auto identity = OvMaths::FMatrix4::Transpose(OvMaths::FMatrix4::Identity);
    m_identity->Allocate(sizeof(identity), baregl::types::EAccessSpecifier::STATIC_DRAW);
    m_identity->Upload(&identity);
    m_reflection = std::make_shared<baregl::Buffer>();
    m_reflection->Allocate(sizeof(NoReflectionConstants), baregl::types::EAccessSpecifier::STREAM_DRAW);
}

Data::RenderCommandList Core::MeshCommandRecorder::Record(
    std::shared_ptr<const Data::PreparedFrameData> p_frame,
    std::optional<baregl::Framebuffer::Snapshot> p_target, OvTools::Jobs::JobSystem* p_jobs, const std::atomic_bool* p_cancel) const
{
    return RenderTaskCoordinator::Record(std::move(p_frame), *this, std::move(p_target), p_jobs, p_cancel);
}

Data::RenderCommandList Core::MeshCommandRecorder::RecordRange(
    std::shared_ptr<const Data::PreparedFrameData> p_frame,
    std::optional<baregl::Framebuffer::Snapshot> p_target, size_t begin, size_t end, const std::atomic_bool* cancel) const
{
    if (!p_frame || begin > end || end > p_frame->GetDraws().size())
        throw std::invalid_argument("Invalid mesh recording range");
    Data::RenderCommandList commands;
    const auto check = [&] { if (cancel && cancel->load(std::memory_order_acquire)) throw std::runtime_error("Command recording cancelled"); };
    try
    {
        check();
        const auto view = p_frame->GetView();
        // Every block establishes its own view light content. GPU writes stay on the executor.
        commands.Append([p_frame, lights = m_lights](Context::Driver&) {
            const auto matrices = std::span{p_frame->GetViewBindings().lights};
            if (lights->Allocate(matrices.size_bytes(), baregl::types::EAccessSpecifier::STREAM_DRAW)) lights->Upload(matrices.data());
        });
        for (size_t i = begin; i < end; ++i)
        {
            check();
            const auto& draw = p_frame->GetDraws()[i];
            if (!draw.material) throw std::invalid_argument("Missing published draw bindings");
            const auto& source = p_frame->GetScene().GetMeshes()[draw.sourceIndex];
            const auto constants = DrawPacketBuilder::Engine(*p_frame, draw);
            commands.Append([geometry = source.geometry, material = draw.material,
                instances = source.instances, state = draw.pipelineState, constants, view, p_target,
                palette = source.skinning, reflectionData = source.reflection,
                engine = m_engine, lights = m_lights, reflection = m_reflection,
                skinning = m_skinning, identity = m_identity](Context::Driver& driver) {
                if (p_target) p_target->Bind(); else baregl::Framebuffer::BindDefault();
                driver.SetViewport(0, 0, view.input.width, view.input.height);
                engine->Upload(&constants);
                engine->Bind(baregl::types::EBufferType::UNIFORM, 0);
                lights->Bind(baregl::types::EBufferType::SHADER_STORAGE, 0);
                reflection->Upload(&reflectionData);
                reflection->Bind(baregl::types::EBufferType::UNIFORM, 1);
                if (palette.enabled)
                {
                    if (palette.matrices.empty()) identity->Bind(baregl::types::EBufferType::SHADER_STORAGE, 1);
                    else
                    {
                        const auto bytes = palette.matrices.size() * sizeof(OvMaths::FMatrix4);
                        if (skinning->GetSize() < bytes) skinning->Allocate(bytes, baregl::types::EAccessSpecifier::STREAM_DRAW);
                        skinning->Upload(palette.matrices.data(), baregl::data::BufferMemoryRange{.offset = 0, .size = bytes});
                        skinning->Bind(baregl::types::EBufferType::SHADER_STORAGE, 1);
                    }
                }
                material->Execute();
                driver.Draw(state, *geometry, baregl::types::EPrimitiveMode::TRIANGLES, instances);
            });
        }
        check();
        commands.Seal(); // No deferred factories or nested Worker scheduling.
        return commands;
    }
    catch (...) { commands.FailRecording(); throw; }
}

void Core::MeshCommandRecorder::BeginView(Data::RenderCommandList& commands, const Data::RenderView& view,
    std::optional<baregl::Framebuffer::Snapshot> target) const
{
    commands.Append([view, target](Context::Driver& driver) {
        if (target) target->Bind(); else baregl::Framebuffer::BindDefault();
        driver.SetViewport(0, 0, view.input.width, view.input.height);
        driver.Clear(view.input.clearColor, view.input.clearDepth, view.input.clearStencil, view.input.clearValue);
    });
}
void Core::MeshCommandRecorder::EndView(Data::RenderCommandList& commands, bool hasDraws) const
{
    commands.Append([engine=m_engine, lights=m_lights, hasDraws](Context::Driver&) {
        if (hasDraws) { engine->Unbind(); lights->Unbind(); }
        baregl::Framebuffer::BindDefault();
    });
}
