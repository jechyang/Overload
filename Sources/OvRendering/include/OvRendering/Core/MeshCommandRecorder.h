#pragma once
#include <atomic>
#include <unordered_map>
#include <baregl/Buffer.h>
#include <OvTools/Jobs/JobSystem.h>
#include <baregl/Framebuffer.h>
#include <OvRendering/Data/PreparedFrameData.h>
#include <OvRendering/Data/RenderCommandList.h>

namespace OvRendering::Core
{
    // Mesh backend bridge. Resource creation uses the existing GL owner/ResourceExecutor.
    // Record itself is CPU-only; buffers are written only by command execution.
    class MeshCommandRecorder final
    {
    public:
        MeshCommandRecorder();
        // Owned by one frontend/main-thread boundary. Weak generations cannot
        // keep deleted assets alive or accept a recycled GL name as a cache hit.
        class ProgramCache final {
        public:
            bool Validate(const std::shared_ptr<baregl::ShaderProgram>& program);
        private:
            struct Entry { std::weak_ptr<baregl::ShaderProgram> program; bool skinning; };
            std::unordered_map<const baregl::ShaderProgram*,Entry> m_entries;
        };
        // Extract-time resource validation, dispatched to the GL owner.
        // Validates Engine/Reflection UBO and Light/Skinning SSBO providers; returns palette demand.
        static bool ValidateProgram(const baregl::ShaderProgram& p_program);
        const std::shared_ptr<baregl::Texture>& GetFallback2D() const { return m_white; }
        const std::shared_ptr<baregl::Texture>& GetFallbackCube() const { return m_black; }
        Data::RenderCommandList Record(std::shared_ptr<const Data::PreparedFrameData> p_frame,
            std::optional<baregl::Framebuffer::Snapshot> p_target,
            OvTools::Jobs::JobSystem* p_jobs = nullptr, const std::atomic_bool* p_cancel = nullptr) const;
        Data::RenderCommandList RecordRange(std::shared_ptr<const Data::PreparedFrameData> p_frame,
            std::optional<baregl::Framebuffer::Snapshot> p_target, size_t begin, size_t end, const std::atomic_bool* cancel) const;
        void BeginView(Data::RenderCommandList& commands, const Data::RenderView& view,
            std::optional<baregl::Framebuffer::Snapshot> target) const;
        void EndView(Data::RenderCommandList& commands, bool hasDraws) const;
    private:
        std::shared_ptr<baregl::Texture> m_white, m_black;
        std::shared_ptr<baregl::Buffer> m_engine, m_lights, m_reflection, m_skinning, m_identity;
    };
}
