#pragma once
#include <OvCore/Rendering/ToolDrawCollector.h>
#include <OvCore/SceneSystem/Scene.h>
#include <OvEditor/Core/GizmoBehaviour.h>
namespace OvEditor::Rendering
{
    struct EditorOverlayRequest
    {
        bool grid = false, debugObjects = false, picking = false;
        bool lightIcons = true, cameraIcons = true, reflectionIcons = true;
        bool colliders = true, cameraFrustums = true;
        bool geometryBounds = false, lightBounds = false;
        uint64_t selected = 0, highlighted = 0;
        std::vector<uint64_t> selection;
        std::optional<Core::GizmoBehaviour::Frame> gizmoFrame;
        OvCore::SceneSystem::Scene::LifetimeToken scene;
        OvMaths::FVector3 gridColor;
        OvEditor::Core::EGizmoOperation operation = OvEditor::Core::EGizmoOperation::TRANSLATE;
        std::optional<OvEditor::Core::GizmoBehaviour::EDirection> highlightedDirection;
        std::pair<uint16_t,uint16_t> gameViewSize{16,9};
    };
    class EditorOverlayCollector final
    {
    public:
        EditorOverlayCollector();
        ~EditorOverlayCollector();
        const OvRendering::Data::FrameInfo& GetFrameInfo() const;
        // Source references are used only here, on the main-thread scene boundary.
        OvRendering::Data::RenderCommandList Extract(OvCore::SceneSystem::Scene& scene,
            const EditorOverlayRequest& request, const OvRendering::Data::RenderView& view,
            OvRendering::Data::PipelineState state, baregl::Framebuffer::Snapshot target);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
