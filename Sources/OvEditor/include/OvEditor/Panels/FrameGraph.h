#pragma once
#include <OvUI/Panels/PanelWindow.h>
#include <OvRendering/Data/FrameGraphDiagnostics.h>
#include <OvRendering/Core/RenderTargetPool.h>
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
    private:
        void RequireUIThread() const;
        void DrawContents();
        void DrawPasses();
        void DrawResources();
        void DrawDependencies();
        void DrawResourceFlow();
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
        float m_graphZoom=1;
        std::array<float,2> m_graphPan{};
        // Screen rectangles contain values only; also serve node hit testing.
        struct GraphNode { bool resource;size_t index;std::array<float,4> rect; };
        struct GraphEdge { size_t pass,resource;bool read,write; };
        std::vector<GraphNode> m_graphNodes;
        std::vector<GraphEdge> m_graphEdges;
    };
}
