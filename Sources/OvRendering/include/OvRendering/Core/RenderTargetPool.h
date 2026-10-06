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
            uint64_t allocations=0, hits=0, aliases=0, evictions=0;
            uint64_t idleBytes=0, leasedBytes=0, peakLeasedBytes=0;
            size_t idleTargets=0, leasedTargets=0;
            uint64_t idleBudget=0;
            size_t frameReserve=1;
        };
        explicit RenderTargetPool(uint64_t idleBudget=256ull*1024*1024);
        ~RenderTargetPool();
        RenderTargetPool(const RenderTargetPool&)=delete;
        RenderTargetPool& operator=(const RenderTargetPool&)=delete;
        std::shared_ptr<const RenderResourceMapping> Realize(const Data::RenderPlan&,const Data::CompiledRenderPlan&,bool reuse=true);
        Statistics GetStatistics() const;
        void SetIdleBudget(uint64_t);
        // Release only idle entries; preserve the budget and all active leases/pins.
        void TrimIdle();
        // Reserve physical sets for the product's bounded in-flight capacity.
        void SetFrameReserve(size_t);
        // Owner boundary, after draining frames. Active leases remain pinned.
        // A closed pool cannot be used for another context.
        void Close();
    };
}
