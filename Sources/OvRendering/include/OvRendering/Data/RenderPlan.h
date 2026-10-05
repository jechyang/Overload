#pragma once
#include <OvRendering/Data/PreparedFrameData.h>
#include <baregl/Framebuffer.h>
#include <OvRendering/Data/RenderPlanResource.h>
#include <OvRendering/Data/RenderCommandList.h>
#include <cstdint>
#include <string>
#include <vector>
namespace tracy { struct SourceLocationData; }

namespace OvRendering::Data
{
    enum class ResourceOrigin { CurrentFrame, Imported, History, Transient };
    struct PlanResource
    {
        uint64_t id = 0, sceneGeneration = 0;
        baregl::Texture::Snapshot storage;
        uint16_t width = 0, height = 0;
        uint8_t subresources = 1, available = 0;
        ResourceOrigin origin = ResourceOrigin::CurrentFrame;
        std::string name;
        std::optional<PlanResourceDesc> descriptor;
        std::shared_ptr<const baregl::Renderbuffer> renderbuffer;
        std::vector<ResourceRange> initialized;
        uint32_t generation=1;
    };
    struct ResourceUse { size_t resource = 0; uint8_t subresources = 1; };
    struct RenderPlanPass
    {
        static constexpr size_t NoView=size_t(-1);
        std::string name;
        size_t view = 0;
        uint64_t viewId = 0;
        std::vector<RenderStage> stages;
        std::optional<baregl::Framebuffer::Snapshot> target;
        std::vector<ResourceUse> reads, writes;
        // A view-independent operation owns immutable inputs. Encoding can run
        // after resource preparation without touching a Material, ECS or panel.
        RenderCommandList::OperationFactory operation;
        std::vector<ResourceAccess> accesses;
        std::vector<size_t> after;
        std::vector<const tracy::SourceLocationData*> gpuScopesBegin;
        size_t gpuScopesEnd=0;
    };
    // External commands appended after the plan's last pass (editor overlay, picking,
    // final UI). Declared so Validate can cover the full frame resource contract;
    // the command lists themselves remain owned by the caller.
    struct ExternalComposition
    {
        std::string name;
        std::vector<ResourceUse> reads, writes;
    };
    // Fixed GPU order, independent of the order in which CPU view jobs finish.
    // Imported/history availability is supplied only at the owner planning boundary.
    struct RenderPlan
    {
        RenderPlan();
        std::vector<PlanResource> resources;
        std::vector<RenderPlanPass> passes;
        std::vector<ExternalComposition> trailing;
        void Validate(std::span<const std::shared_ptr<const RenderSceneSnapshot>> views) const;
        ResourceHandle Handle(size_t index) const;
        const PlanResource& Resolve(ResourceHandle) const;
        ResourceHandle CreateTransient(std::string name,const PlanResourceDesc&);
        ResourceHandle Import(std::string name,const baregl::Texture::Snapshot&,ResourceOrigin,
            std::vector<ResourceRange> initialized={});
        ResourceHandle Import(std::string name,std::shared_ptr<const baregl::Renderbuffer>,ResourceOrigin,
            std::vector<ResourceRange> initialized={});
        std::optional<ResourceHandle> Find(const baregl::Texture::Snapshot&) const;
        void GenerateMipmaps(std::string name,const std::shared_ptr<baregl::Texture>&);
        void RecordOperations(RenderCommandList&,std::span<RenderCommandList> viewBlocks={}) const;
        CompiledRenderPlan Compile(std::span<const std::shared_ptr<const RenderSceneSnapshot>> views={}) const;
        uint64_t Scope() const { return m_scope; }
    private:
        uint64_t m_scope;
    };
}
