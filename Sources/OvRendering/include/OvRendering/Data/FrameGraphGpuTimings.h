#pragma once
#include <OvRendering/Data/FrameGraphDiagnostics.h>
#include <OvRendering/Data/RenderFrame.h>
#include <thread>

namespace OvRendering::Data
{
    // Values only. A measured graph is kept alongside its Pass intervals so an
    // older GPU sample can never be paired with a newer prepared plan.
    struct FrameGraphGpuTimings
    {
        std::shared_ptr<const FrameGraphDiagnostics> graph;
        uint64_t frameID=0,totalNanoseconds=0;
        std::vector<uint64_t> passNanoseconds;
    };
    class FrameGraphGpuTimingCapture final
    {
        friend class FrameGraphGpuTimer;
        struct State;
        std::shared_ptr<State> m_state;
        explicit FrameGraphGpuTimingCapture(std::shared_ptr<State>);
        void Collect(bool) noexcept;
    public:
        ~FrameGraphGpuTimingCapture();
        void Begin(size_t pass); // GL consumer only; no bindings or active query target.
        void End(size_t pass);
        bool IsRetired() const;
        std::shared_ptr<const FrameGraphGpuTimings> GetResult() const;
    };
    class FrameGraphGpuTimer final
    {
        struct State;
        std::shared_ptr<State> m_state;
        const std::thread::id m_producer=std::this_thread::get_id();
        void RequireProducer() const;
    public:
        static constexpr size_t MaxPending=3,MaxPasses=512;
        struct Statistics { uint64_t samples=0,skipped=0;size_t pending=0,queries=0; };
        FrameGraphGpuTimer();
        // Returns null when capacity is full or the plan exceeds the fixed cap.
        // No GL calls, owner waits, or source texture/lease retention here.
        std::shared_ptr<FrameGraphGpuTimingCapture> Capture(std::shared_ptr<const FrameGraphDiagnostics>,RenderFrame&);
        Statistics GetStatistics() const;
    };
}
