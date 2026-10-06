#pragma once
#include <OvRendering/Data/RenderPlan.h>

namespace OvRendering::Data
{
    // Values only: freezing a plan never retains GPU resources, commands,
    // views, materials, or a pool lease.
    struct FrameGraphDiagnostics
    {
        struct CpuTimings
        {
            uint64_t compile=0,realize=0,prepare=0,diagnostics=0,recordViews=0,recordOperations=0,seal=0;
            bool captured=false,encodingDeferred=false;
            uint64_t Total() const {return compile+realize+prepare+diagnostics+recordViews+recordOperations+seal;}
        } cpu;
        struct Resource
        {
            ResourceHandle handle;
            std::string name;
            ResourceOrigin origin;
            PlanResourceKind kind;
            ResourceAspect aspect;
            baregl::types::EInternalFormat format;
            uint32_t width=0,height=0;
            uint16_t mips=1,layers=1,samples=1;
            ResourceLifetime lifetime;
            uint32_t finalVersion=0;
            uint64_t physicalSlot=0;
        };
        struct Pass
        {
            struct AccessVersion
            {
                // Whole-resource content state before/after this pass. Ranges
                // remain in accesses; a version does not allocate a texture.
                std::optional<uint32_t> read,write;
            };
            std::string name;
            uint64_t viewId=0;
            std::vector<ResourceAccess> accesses;
            std::vector<AccessVersion> contentVersions;
        };
        uint64_t scope=0,captureIndex=0;
        uint64_t logicalTransientBytes=0,estimatedPeakBytes=0,physicalTransientBytes=0;
        size_t physicalTransientTargets=0;
        std::vector<Resource> resources;
        std::vector<Pass> passes;
        std::vector<PlanDependency> dependencies;
    };
    FrameGraphDiagnostics CaptureFrameGraphDiagnostics(const RenderPlan&,const CompiledRenderPlan&,
        const Core::RenderResourceMapping&,std::span<const std::shared_ptr<const RenderSceneSnapshot>> views={});
}
