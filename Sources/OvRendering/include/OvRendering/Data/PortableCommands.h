#pragma once
#include <OvRendering/Data/BackendCommands.h>
#include <OvRendering/Data/RenderCommandList.h>
#include <OvRendering/Context/Driver.h>
#include <baregl/Framebuffer.h>
namespace OvRendering::Data
{
    // Typed portable operations for product command recording. Execution goes through
    // Driver or owned snapshot helpers; product code must not call raw GL entry points.
    // Remaining baregl types on plan/material/recorder interfaces are the RHI seam:
    // replace them with handle wrappers before adding a non-OpenGL backend.
    inline RenderCommandList::Operation PresentColor(
        baregl::Framebuffer::Snapshot source, uint16_t backBufferWidth, uint16_t backBufferHeight)
    {
        return [source = std::move(source), backBufferWidth, backBufferHeight](Context::Driver&)
        {
            source.BlitToBackBuffer(backBufferWidth, backBufferHeight);
        };
    }
    inline RenderCommandList::Operation ClearTarget(
        baregl::Framebuffer::Snapshot target, bool color, bool depth, bool stencil,
        const OvMaths::FVector4& clearColor = OvMaths::FVector4::Zero)
    {
        return [target = std::move(target), color, depth, stencil, clearColor](Context::Driver& driver)
        {
            target.Bind();
            driver.Clear(color, depth, stencil, clearColor);
        };
    }
    // Executor for typed view setup and mesh draws. GL stays behind Driver/baregl.
    inline void ExecuteBeginView(const BeginViewCommand& command, Context::Driver& driver)
    {
        if (command.target) command.target->Bind(); else baregl::Framebuffer::BindDefault();
        driver.SetViewport(0, 0, command.width, command.height);
        driver.Clear(command.clearColor, command.clearDepth, command.clearStencil, command.clearValue);
    }
    inline void ExecuteMeshDraw(const MeshDrawCommand& command, const MeshDrawBuffers& buffers, Context::Driver& driver)
    {
        if (!command.geometry || !command.material)
            throw std::invalid_argument("Mesh draw requires owned geometry and material snapshot");
        if (command.target) command.target->Bind(); else baregl::Framebuffer::BindDefault();
        driver.SetViewport(0, 0, command.width, command.height);
        buffers.engine->Upload(&command.constants);
        buffers.engine->Bind(baregl::types::EBufferType::UNIFORM, 0);
        buffers.lights->Bind(baregl::types::EBufferType::SHADER_STORAGE, 0);
        buffers.reflection->Upload(&command.reflection);
        buffers.reflection->Bind(baregl::types::EBufferType::UNIFORM, 1);
        if (command.palette.enabled)
        {
            const auto& matrices = command.palette.matrices;
            if (!matrices || matrices->empty()) buffers.identity->Bind(baregl::types::EBufferType::SHADER_STORAGE, 1);
            else
            {
                const auto bytes = matrices->size() * sizeof(OvMaths::FMatrix4);
                if (buffers.skinning->GetSize() < bytes)
                    buffers.skinning->Allocate(bytes, baregl::types::EAccessSpecifier::STREAM_DRAW);
                buffers.skinning->Upload(matrices->data(), baregl::data::BufferMemoryRange{.offset = 0, .size = bytes});
                buffers.skinning->Bind(baregl::types::EBufferType::SHADER_STORAGE, 1);
            }
        }
        command.material->Execute();
        driver.Draw(command.state, *command.geometry, baregl::types::EPrimitiveMode::TRIANGLES, command.instances);
    }
    inline RenderCommandList::Operation MakeBeginView(BeginViewCommand command)
    {
        return [command = std::move(command)](Context::Driver& driver) { ExecuteBeginView(command, driver); };
    }
    inline RenderCommandList::Operation MakeMeshDraw(MeshDrawCommand command, MeshDrawBuffers buffers)
    {
        return [command = std::move(command), buffers = std::move(buffers)](Context::Driver& driver)
        {
            ExecuteMeshDraw(command, buffers, driver);
        };
    }
}
