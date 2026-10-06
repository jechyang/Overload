#pragma once
#include <OvRendering/Data/PreparedFrameData.h>
#include <baregl/Framebuffer.h>
#include <OvRendering/Data/RenderPlanResource.h>
#include <OvRendering/Data/RenderCommandList.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
namespace tracy { struct SourceLocationData; }
namespace OvRendering::Core { class RenderResourceMapping; }

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
        std::shared_ptr<baregl::Texture> textureOwner;
        std::optional<baregl::Framebuffer::Snapshot> importedTarget;
        std::vector<uint64_t> sceneGenerations;
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
        // Main resolves logical resources once, before immutable encoding jobs.
        std::function<RenderCommandList::OperationFactory(const Core::RenderResourceMapping&)> prepareOperation;
        std::vector<ResourceAccess> accesses;
        std::vector<size_t> after;
        std::vector<const tracy::SourceLocationData*> gpuScopesBegin;
        size_t gpuScopesEnd=0;
        std::shared_ptr<RenderCommandList> commandBlock;
    };
    // An optional diagnostic read inserted inside an existing resource lifetime.
    // The caller validates content/ranges at this exact fixed-order position.
    struct RenderPlanInspection { size_t beforePass=0;RenderCommandList::OperationFactory operation; };
    // Fixed GPU order, independent of the order in which CPU view jobs finish.
    // Imported/history availability is supplied only at the owner planning boundary.
    struct RenderPlan
    {
        RenderPlan();
        struct RemappedResource { uint32_t generation;ResourceHandle target; };
        std::unordered_map<uint64_t,std::vector<RemappedResource>> remaps;
        std::vector<uint64_t> viewScopes;
        void Append(RenderPlan&&,size_t viewOffset,size_t viewCount);
        void AddCommands(std::string,RenderCommandList&&,std::vector<ResourceAccess> accesses={});
        ResourceHandle ImportBackbuffer(uint16_t width,uint16_t height,bool initialized);
        std::vector<PlanResource> resources;
        std::vector<RenderPlanPass> passes;
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
        void RecordOperations(RenderCommandList&,std::span<RenderCommandList> viewBlocks={},
            std::shared_ptr<const Core::RenderResourceMapping> mapping={},std::optional<RenderPlanInspection> inspection={}) const;
        CompiledRenderPlan Compile(std::span<const std::shared_ptr<const RenderSceneSnapshot>> views={}) const;
        uint64_t Scope() const { return m_scope; }
    private:
        uint64_t m_scope;
    };
}
