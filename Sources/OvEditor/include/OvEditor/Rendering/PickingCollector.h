#pragma once
#include <OvEditor/Rendering/PickingReadback.h>
#include <OvEditor/Rendering/EditorOverlayCollector.h>
#include <OvCore/Rendering/RenderFrontend.h>
namespace OvEditor::Rendering
{
    class PickingCollector final
    {
    public:
        PickingCollector();
        const OvRendering::Data::FrameInfo& GetFrameInfo() const { return m_info; }
        void Build(OvCore::Rendering::SceneFrame&,OvCore::Rendering::SceneRenderRequest,const EditorOverlayRequest&,OvRendering::Data::PipelineState);
        OvRendering::Data::RenderCommandList Extract(OvCore::Rendering::SceneRenderRequest request,
            const EditorOverlayRequest& overlay, OvRendering::Data::PipelineState state, OvTools::Jobs::JobSystem* jobs);
        std::shared_ptr<const PickingReadback> GetReadback() const { return m_readback; }
    private:
		void DrawPickableCameras(OvRendering::Data::PipelineState p_pso, OvCore::SceneSystem::Scene& p_scene);
		void DrawPickableReflectionProbes(OvRendering::Data::PipelineState p_pso, OvCore::SceneSystem::Scene& p_scene);
		void DrawPickableLights(OvRendering::Data::PipelineState p_pso, OvCore::SceneSystem::Scene& p_scene);
		void DrawPickableGizmo(
			OvRendering::Data::PipelineState p_pso,
			const OvMaths::FVector3& p_position,
			const OvMaths::FQuaternion& p_rotation,
			OvEditor::Core::EGizmoOperation p_operation
		);

        OvRendering::Data::FrameInfo m_info;
        OvCore::Rendering::RenderFrontend m_frontend;
        OvCore::Rendering::ToolDrawCollector m_collector;
        baregl::Framebuffer m_actorPickingFramebuffer;
        OvRendering::Data::Material m_actorPickingFallbackMaterial, m_reflectionProbeMaterial, m_lightMaterial, m_gizmoPickingMaterial;
        std::shared_ptr<PickingReadback> m_readback;
    };
}
