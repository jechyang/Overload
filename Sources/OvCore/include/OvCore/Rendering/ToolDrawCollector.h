#pragma once
#include <OvRendering/Data/FrameInfo.h>
#include <OvRendering/Core/MeshCommandRecorder.h>
#include <OvCore/Rendering/ToolDrawInput.h>
#include <OvRendering/Resources/Model.h>
#include <OvRendering/Utils/Profiling.h>
#include <OvRendering/Core/FullscreenCommandRecorder.h>
namespace OvCore::Rendering
{
    // Main-thread tool extraction. Only owned values enter deferred packet encoding.
    class ToolDrawCollector final
    {
    public:
        ToolDrawCollector();
        const OvRendering::Data::FrameInfo& GetFrameInfo() const { return m_info; }
        void Begin(OvRendering::Data::RenderView view, OvRendering::Data::PipelineState state,
            std::optional<baregl::Framebuffer::Snapshot> target,OvRendering::Data::RenderPlan* plan=nullptr);
        OvRendering::Data::RenderCommandList Finish();
        void CollectMesh(OvRendering::Data::PipelineState state, const OvCore::Rendering::ToolDrawInput& drawable,
            const OvRendering::Data::Material::PropertyOverrides& overrides = {},
            std::span<const std::shared_ptr<baregl::Texture>> textures = {});
        void CollectModel(OvRendering::Data::PipelineState state, OvRendering::Resources::Model& model,
            OvRendering::Data::Material& material, const OvMaths::FMatrix4& transform);
        void CollectModelWithTextures(OvRendering::Data::PipelineState state, OvRendering::Resources::Model& model,
            OvRendering::Data::Material& material, const OvMaths::FMatrix4& transform,
            std::vector<std::shared_ptr<baregl::Texture>> textures);
        void Clear(bool color, bool depth, bool stencil, const OvMaths::FVector4& value = {});
        void BindTarget(std::optional<baregl::Framebuffer::Snapshot> target) { m_target = std::move(target); }
        void SetLights(std::vector<OvMaths::FMatrix4> lights) { m_lights = std::move(lights); }
        void SetReflection(OvRendering::Data::ReflectionReceiverBindings binding) { m_reflection = std::move(binding); }
        OvRendering::Data::PipelineState CreatePipelineState() const { return m_state; }
        const OvRendering::Data::RenderView& GetView() const { return m_view; }
        OvRendering::Data::RenderCommandList::Completion GetCommandCompletion() const { return m_commands->GetCompletion(); }
        void SetPassName(std::string name) { m_passName=std::move(name); }
        void SubmitRenderOperation(OvRendering::Data::RenderCommandList::Operation);
        class GpuScope {
        public:
            GpuScope(OvRendering::Data::RenderPlan*,OvRendering::Data::RenderCommandList*,const tracy::SourceLocationData*);
        private:
            std::optional<OvRendering::Core::FullscreenCommandRecorder::PlanGpuScope> m_planScope;
            std::optional<OvRendering::Utils::GpuScope> m_commandScope;
        };
        GpuScope ProfileGpuScope(const tracy::SourceLocationData* source) { return {m_plan,m_commands.get(),source}; }
    private:
        OvRendering::Data::RenderPlan* m_plan=nullptr;
        std::string m_passName="Editor.ToolDraw";
        std::vector<OvRendering::Data::ResourceAccess> TargetAccesses(bool color,bool depth,bool stencil,bool clear=false);
        void AppendPass(std::string,OvRendering::Data::RenderCommandList::OperationFactory,std::vector<OvRendering::Data::ResourceAccess>);
        OvRendering::Data::FrameInfo m_info;
        OvRendering::Core::MeshCommandRecorder m_resources;
        std::shared_ptr<baregl::Buffer> m_engineBuffer, m_lightBuffer, m_reflectionBuffer, m_skinBuffer;
        std::unique_ptr<OvRendering::Data::RenderCommandList> m_commands;
        OvRendering::Data::RenderView m_view;
        OvRendering::Data::PipelineState m_state;
        std::optional<baregl::Framebuffer::Snapshot> m_target;
        std::vector<OvMaths::FMatrix4> m_lights;
        OvRendering::Data::ReflectionReceiverBindings m_reflection;
        std::optional<OvRendering::Data::MaterialSignatureSet> m_signature;
        OvRendering::Data::Material::PreparedSnapshot m_previous;
    };
}
