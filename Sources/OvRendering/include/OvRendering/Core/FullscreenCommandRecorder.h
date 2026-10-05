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
        struct SamplerBinding {std::string name;Data::ResourceHandle resource;bool singleUse=false;};
        Data::ResourceHandle ImportTarget(baregl::Framebuffer&,Data::ResourceOrigin=Data::ResourceOrigin::CurrentFrame,bool initialized=false);
        Data::ResourceHandle CreateTransient(std::string,const baregl::data::TextureDesc&);
        Data::ResourceHandle CreateTransient(std::string,uint16_t width,uint16_t height,bool mipmaps=false);
        std::pair<uint16_t,uint16_t> GetSize(Data::ResourceHandle) const;
        void Blit(Data::PipelineState,Data::ResourceHandle source,Data::ResourceHandle target,
            Data::Material&,Settings::EBlitFlags=Settings::EBlitFlags::DEFAULT,std::span<const SamplerBinding> bindings={});
        bool UseRenderHistory(Data::RenderCommandList::History& history) { return m_commands.UseHistory(history); }
        void SetPassName(std::string name) { m_passName=std::move(name); }
        void ClearColor(Data::ResourceHandle,const OvMaths::FVector4& color);
        void GenerateMipmaps(Data::ResourceHandle);
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
        Data::FrameInfo m_info;
        FullscreenCommandResources& m_resources;
        std::optional<Data::MaterialSignatureSet> m_signature;
        Data::Material::PreparedSnapshot m_previous;
    };
}
