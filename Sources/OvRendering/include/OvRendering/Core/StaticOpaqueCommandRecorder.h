#pragma once
#include <baregl/Buffer.h>
#include <baregl/Framebuffer.h>
#include <OvRendering/Data/PreparedFrameData.h>
#include <OvRendering/Data/RenderCommandList.h>

namespace OvRendering::Core
{
    // A1 backend bridge. Resource creation uses the existing GL owner/ResourceExecutor.
    // Record itself is CPU-only; buffers are written only by command execution.
    class StaticOpaqueCommandRecorder final
    {
    public:
        StaticOpaqueCommandRecorder();
        // Extract-time resource validation, dispatched to the GL owner.
        // A1 owns only the standard engine and no-reflection uniform blocks.
        static void ValidateProgram(const baregl::ShaderProgram& p_program);
        const std::shared_ptr<baregl::Texture>& GetFallback2D() const { return m_white; }
        const std::shared_ptr<baregl::Texture>& GetFallbackCube() const { return m_black; }
        Data::RenderCommandList Record(std::shared_ptr<const Data::PreparedFrameData> p_frame,
            std::optional<baregl::Framebuffer::Snapshot> p_target) const;
    private:
        std::shared_ptr<baregl::Texture> m_white, m_black;
        std::shared_ptr<baregl::Buffer> m_engine, m_emptyLights, m_noReflection;
    };
}
