#pragma once
#include <OvRendering/Data/FrameInfo.h>
#include <OvRendering/Core/MeshCommandRecorder.h>
#include <OvRendering/Resources/Mesh.h>
#include <OvRendering/Settings/EBlitFlags.h>
#include <OvRendering/Utils/Profiling.h>
#include <OvRendering/Data/RenderPlan.h>
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
        FullscreenCommandRecorder(Data::RenderPlan& plan, Data::RenderCommandList& commands, FullscreenCommandResources& resources);
        const Data::FrameInfo& GetFrameInfo() const { return m_info; }
        void Blit(Data::PipelineState state, baregl::Framebuffer& source, baregl::Framebuffer& target,
            Data::Material& material, Settings::EBlitFlags flags = Settings::EBlitFlags::DEFAULT);
        void BlitWithRetainedTextures(Data::PipelineState state, baregl::Framebuffer& source,
            baregl::Framebuffer& target, Data::Material& material, Settings::EBlitFlags flags,
            std::span<const std::shared_ptr<baregl::Texture>> textures);
        bool UseRenderHistory(Data::RenderCommandList::History& history) { return m_commands.UseHistory(history); }
        void SetPassName(std::string name) { m_passName=std::move(name); }
        void RegisterHistoryTexture(const std::shared_ptr<baregl::Texture>& texture,bool initialized);
        void ClearColor(baregl::Framebuffer&,const OvMaths::FVector4& color);
        void GenerateMipmaps(const std::shared_ptr<baregl::Texture>&);
        class PlanGpuScope
        {
        public:
            PlanGpuScope(Data::RenderPlan&,Data::RenderCommandList&,const tracy::SourceLocationData*);
            ~PlanGpuScope();
            PlanGpuScope(const PlanGpuScope&)=delete;
        private:
            Data::RenderPlan& m_plan;Data::RenderCommandList& m_commands;
            const tracy::SourceLocationData* m_source;
            size_t m_first;int m_exceptions;
        };
        PlanGpuScope ProfileGpuScope(const tracy::SourceLocationData* source) { return {m_plan,m_commands,source}; }
    private:
        Data::RenderCommandList& m_commands;
        Data::RenderPlan& m_plan;
        std::string m_passName="Blit";
        Data::ResourceHandle TextureHandle(const std::shared_ptr<baregl::Texture>&,bool initialized=false);
        Data::FrameInfo m_info;
        FullscreenCommandResources& m_resources;
        std::optional<Data::MaterialSignatureSet> m_signature;
        Data::Material::PreparedSnapshot m_previous;
    };
}
