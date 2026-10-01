#pragma once
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
}
