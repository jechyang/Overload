#pragma once
#include <OvRendering/Data/FrameInfo.h>
#include <OvRendering/Core/MeshCommandRecorder.h>
#include <OvRendering/Resources/Mesh.h>
#include <OvRendering/Settings/EBlitFlags.h>
#include <OvRendering/Utils/Profiling.h>
namespace OvRendering::Core
{
    // GPU resources are prepared once by the viewport owner, before steady recording.
    class FullscreenCommandResources final
    {
        friend class FullscreenCommandRecorder;
    public:
        FullscreenCommandResources();
    private:
        std::shared_ptr<const Resources::IMesh> m_quad;
        MeshCommandRecorder m_materialResources;
    };
    // One recording, explicit output and bindings. No scene or renderer services.
    class FullscreenCommandRecorder final
    {
    public:
        FullscreenCommandRecorder(Data::RenderCommandList& commands, FullscreenCommandResources& resources);
        const Data::FrameInfo& GetFrameInfo() const { return m_info; }
        void Blit(Data::PipelineState state, baregl::Framebuffer& source, baregl::Framebuffer& target,
            Data::Material& material, Settings::EBlitFlags flags = Settings::EBlitFlags::DEFAULT);
        void BlitWithRetainedTextures(Data::PipelineState state, baregl::Framebuffer& source,
            baregl::Framebuffer& target, Data::Material& material, Settings::EBlitFlags flags,
            std::span<const std::shared_ptr<baregl::Texture>> textures);
        bool UseRenderHistory(Data::RenderCommandList::History& history) { return m_commands.UseHistory(history); }
        void SubmitRenderOperation(Data::RenderCommandList::Operation operation) { m_commands.Append(std::move(operation)); }
        Utils::GpuScope ProfileGpuScope(const tracy::SourceLocationData* source) { return {&m_commands, source}; }
    private:
        Data::RenderCommandList& m_commands;
        Data::FrameInfo m_info;
        FullscreenCommandResources& m_resources;
        std::optional<Data::MaterialSignatureSet> m_signature;
        Data::Material::PreparedSnapshot m_previous;
    };
}
