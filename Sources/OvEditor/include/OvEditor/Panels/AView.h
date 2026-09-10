#pragma once
#include <chrono>
#include <OvCore/Rendering/RenderFrontend.h>
#include <OvCore/Rendering/ViewportSurface.h>
#include <OvEditor/Rendering/EditorOverlayCollector.h>
#include <OvEditor/Rendering/PickingCollector.h>
#include <OvRendering/Data/FrameInfo.h>
#include <OvUI/Panels/PanelWindow.h>
#include <OvUI/Widgets/Visual/Image.h>
namespace OvEditor::Panels
{
    class AView : public OvUI::Panels::PanelWindow
    {
    public:
        AView(const std::string& title,bool opened,const OvUI::Settings::PanelWindowSettings& settings);
        virtual void Update(float deltaTime);
        void _Draw_Impl() override;
        virtual void InitFrame();
        void Render();
        virtual OvRendering::Data::RenderCommandList RecordRenderCommands(bool deferEncoding=false);
        void SetCommandRecordingEnabled(bool enabled,OvTools::Jobs::JobSystem* jobs=nullptr) { m_jobs=enabled?jobs:nullptr; }
        OvTools::Jobs::JobSystem* GetCommandRecordingJobs() const { return m_jobs; }
        virtual void CompleteRender() {}
        virtual OvRendering::Entities::Camera* GetCamera()=0;
        virtual OvCore::SceneSystem::Scene* GetScene()=0;
        std::pair<uint16_t,uint16_t> GetSafeSize() const;
        baregl::Framebuffer::Snapshot GetOutputTarget() const { return m_surface.GetTarget(); }
        std::shared_ptr<baregl::Texture> GetOutputTexture() const { return m_surface.GetColor(); }
        const OvRendering::Data::FrameInfo& GetFrameInfo() const { return m_frameInfo; }
    protected:
        virtual OvCore::Rendering::SceneRenderRequest CreateSceneRequest();
        virtual Rendering::EditorOverlayRequest CreateOverlayRequest();
        virtual void OnViewRecorded() {}
        OvUI::Widgets::Visual::Image* m_image;
        OvMaths::FVector3 m_gridColor{0.176f,0.176f,0.176f};
        OvCore::Rendering::ViewportSurface m_surface;
        OvCore::Rendering::RenderFrontend m_frontend;
        OvCore::Rendering::PostProcess::PostProcessPipeline m_postProcess;
        OvRendering::Data::Material m_shadowFallback;
        std::unique_ptr<Rendering::EditorOverlayCollector> m_overlays;
        std::unique_ptr<Rendering::PickingCollector> m_picking;
        Rendering::EditorOverlayRequest m_overlayRequest;
        OvRendering::Data::RenderCommandList::Completion m_completion;
        OvRendering::Data::FrameInfo m_frameInfo;
        OvTools::Jobs::JobSystem* m_jobs=nullptr;
        std::chrono::high_resolution_clock::time_point m_startTime=std::chrono::high_resolution_clock::now();
    };
}
