#pragma once
#include <OvRendering/Data/FrameInfo.h>
#include <OvRendering/Core/MeshCommandRecorder.h>
#include <OvCore/Rendering/ToolDrawInput.h>
#include <OvRendering/Resources/Model.h>
#include <OvRendering/Utils/Profiling.h>
namespace OvCore::Rendering
{
    // Main-thread tool extraction. Only owned values enter deferred packet encoding.
    class ToolDrawCollector final
    {
    public:
        ToolDrawCollector();
        const OvRendering::Data::FrameInfo& GetFrameInfo() const { return m_info; }
        void Begin(OvRendering::Data::RenderView view, OvRendering::Data::PipelineState state,
            std::optional<baregl::Framebuffer::Snapshot> target);
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
        void SubmitRenderOperation(OvRendering::Data::RenderCommandList::Operation operation) { m_commands->Append(std::move(operation)); }
        OvRendering::Utils::GpuScope ProfileGpuScope(const tracy::SourceLocationData* source) { return {m_commands.get(),source}; }
    private:
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
