#pragma once
#include <OvUI/Panels/PanelWindow.h>
#include <OvRendering/Data/FrameGraphDiagnostics.h>
#include <OvRendering/Core/RenderTargetPool.h>
#include <OvCore/Rendering/RenderFrontend.h>
#include <array>
#include <thread>

namespace OvEditor::Panels
{
    class FrameGraph final : public OvUI::Panels::PanelWindow
    {
    public:
        struct PoolRequests { std::optional<uint64_t> budget,unusedFrameLimit;bool trimIdle=false; };
        FrameGraph(const std::string&,bool,const OvUI::Settings::PanelWindowSettings&);
        bool WantsCapture() const;
        void Update(std::shared_ptr<const OvRendering::Data::FrameGraphDiagnostics>,const OvRendering::Core::RenderTargetPool::Statistics&);
        PoolRequests TakePoolRequests();
        std::optional<OvRendering::Data::FrameGraphPreviewRequest> TakePreviewRequest();
        void UpdatePreview(std::shared_ptr<const OvCore::Rendering::RenderFrontend::PreviewTicket>);
        bool TakePreviewClearRequest();
        OvTools::Eventing::Event<std::shared_ptr<const OvRendering::Data::FrameGraphPreviewImage>> OpenPreviewEvent;
    private:
        void RequireUIThread() const;
        void DrawContents();
        void DrawPasses();
        void DrawResources();
        void DrawDependencies();
        void DrawResourceFlow();
        void DrawPreview();
        void QueuePreview();
        void SelectPass(size_t);
        void SelectResource(size_t);
        const std::thread::id m_uiThread=std::this_thread::get_id();
        std::shared_ptr<const OvRendering::Data::FrameGraphDiagnostics> m_snapshot;
        OvRendering::Core::RenderTargetPool::Statistics m_pool;
        PoolRequests m_requests;
        bool m_frozen=false,m_transientOnly=false,m_budgetEdited=false;
        int m_budgetMiB=256,m_requestedTab=-1;
        size_t m_selectedPass=0,m_selectedResource=0;
        std::array<char,128> m_filter{};
        bool m_graphFocus=true,m_graphFit=true,m_graphSelectionInitialized=false,m_graphResourceFlow=true,m_flowSelectionIsResource=false;
        bool m_graphVersions=true;
        uint32_t m_selectedVersion=0;
        int m_previewMip=0,m_previewLayer=0,m_previewChannel=0;
        float m_previewExposure=0,m_previewMin=0,m_previewMax=1;
        bool m_previewToneMap=false;
        bool m_clearPreview=false;
        std::optional<OvRendering::Data::FrameGraphPreviewRequest> m_previewRequest;
        std::shared_ptr<const OvCore::Rendering::RenderFrontend::PreviewTicket> m_previewTicket;
        std::shared_ptr<const OvRendering::Data::FrameGraphPreviewImage> m_previewImage;
        uint64_t m_previewSequence=0;
        std::string m_previewStatus;
        float m_graphZoom=1;
        std::array<float,2> m_graphPan{};
        // Screen rectangles contain values only; also serve node hit testing.
        struct GraphNode { bool resource;size_t index;std::array<float,4> rect;uint32_t version=0; };
        struct GraphEdge { size_t pass,resource;bool read,write;uint32_t version=0;bool load=false; };
        std::vector<GraphNode> m_graphNodes;
        std::vector<GraphEdge> m_graphEdges;
    };
}
