#pragma once
#include <OvRendering/Data/RenderPlan.h>
#include <memory>

namespace OvRendering::Core
{
    // Immutable logical-to-physical mapping. Holding it is the frame's lease;
    // commands retain it until RenderFrame observes GPU retirement.
    class RenderResourceMapping final
    {
        friend class RenderTargetPool;
        struct Storage;
        std::shared_ptr<Storage> m_storage;
        explicit RenderResourceMapping(std::shared_ptr<Storage> storage) : m_storage(std::move(storage)) {}
    public:
        baregl::Texture::Snapshot TextureStorage(Data::ResourceHandle) const;
        std::shared_ptr<baregl::Texture> Texture(Data::ResourceHandle) const;
        baregl::Framebuffer::Snapshot Target(Data::ResourceHandle, uint16_t layer=0) const;
        uint64_t PhysicalID(Data::ResourceHandle) const;
        std::string Dump() const;
    };
    class RenderTargetPool final
    {
        struct State;
        std::shared_ptr<State> m_state;
    public:
        struct Statistics
        {
            uint64_t allocations=0, hits=0, aliases=0, evictions=0, autoEvictions=0;
            uint64_t idleBytes=0, leasedBytes=0, peakLeasedBytes=0;
            size_t idleTargets=0, leasedTargets=0;
            uint64_t idleBudget=0;
            size_t frameReserve=1;
            uint64_t unusedFrameLimit=60;
            // Disjoint physical categories; pins on an active lease stay in active.
            uint64_t activeCurrentBytes=0,activeOldBytes=0,pinnedOnlyBytes=0,idleReserveBytes=0,idleUnusedBytes=0;
            size_t activeCurrentTargets=0,activeOldTargets=0,pinnedOnlyTargets=0,idleReserveTargets=0,idleUnusedTargets=0;
        };
        struct MemoryEntry
        {
            uint64_t slot=0,bytes=0,unusedGraphs=0;
            uint32_t width=0,height=0;
            uint16_t mips=1,layers=1,samples=1;
            Data::PlanResourceKind kind;
            baregl::types::EInternalFormat format;
            bool leased=false,requiredByLatest=false;
            size_t pins=0;
        };
        struct MemoryDiagnostics {Statistics statistics;std::vector<MemoryEntry> entries;};
        explicit RenderTargetPool(uint64_t idleBudget=256ull*1024*1024);
        ~RenderTargetPool();
        RenderTargetPool(const RenderTargetPool&)=delete;
        RenderTargetPool& operator=(const RenderTargetPool&)=delete;
        std::shared_ptr<const RenderResourceMapping> Realize(const Data::RenderPlan&,const Data::CompiledRenderPlan&,bool reuse=true);
        Statistics GetStatistics() const;
        // One locked, values-only view. No GPU query or resource retention.
        MemoryDiagnostics GetMemoryDiagnostics() const;
        void SetIdleBudget(uint64_t);
        // Release only idle entries; preserve the budget and all active leases/pins.
        void TrimIdle();
        // Reserve physical sets for the product's bounded in-flight capacity.
        void SetFrameReserve(size_t);
        // Age in successfully realized graphs. Zero disables automatic expiry.
        // Required spare sets, active leases and external pins never expire.
        void SetUnusedFrameLimit(uint64_t);
        // Owner boundary, after draining frames. Active leases remain pinned.
        // A closed pool cannot be used for another context.
        void Close();
    };
}
