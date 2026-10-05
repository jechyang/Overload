#pragma once
#include <baregl/Texture.h>
#include <baregl/Renderbuffer.h>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include <string>

namespace OvRendering::Data
{
    enum class PlanResourceKind { Texture2D, TextureCube, Renderbuffer, Backbuffer };
    enum class ResourceAspect : uint8_t { Color=1, Depth=2, Stencil=4, DepthStencil=6 };
    enum class ResourceAccessType { Sample, Attachment, CopySource, CopyDestination, MipmapSource, MipmapDestination, Readback, Export, Present };
    enum class AttachmentLoad { Load, Clear, DontCare };
    enum class AttachmentStore { Store, Discard };
    struct ResourceHandle
    {
        uint64_t scope=0; uint32_t index=0,generation=0;
        bool operator==(const ResourceHandle&) const=default;
    };
    struct ResourceRange
    {
        ResourceAspect aspect=ResourceAspect::Color;
        uint16_t firstMip=0,mipCount=1,firstLayer=0,layerCount=1;
        bool operator==(const ResourceRange&) const=default;
    };
    struct PlanResourceDesc
    {
        PlanResourceKind kind=PlanResourceKind::Texture2D;
        baregl::data::TextureDesc texture;
        ResourceAspect aspect=ResourceAspect::Color;
        uint16_t mips=1,layers=1,samples=1;
        // Texture and renderbuffer attachment kinds/format/sampler state must
        // match exactly for object reuse. Only the supported GL usages are valid.
        bool sampleable=true,attachment=true,copyable=true;
        bool Compatible(const PlanResourceDesc&) const;
        uint64_t EstimatedBytes() const;
        void Validate() const;
        static PlanResourceDesc From(const baregl::Texture::Snapshot&);
    };
    struct ResourceAccess
    {
        ResourceHandle resource;
        ResourceRange range;
        ResourceAccessType type=ResourceAccessType::Sample;
        AttachmentLoad load=AttachmentLoad::Load;
        AttachmentStore store=AttachmentStore::Store;
        // Only a known full overwrite/clear can initialize previously undefined
        // storage. A partial viewport, scissor, depth test or discard cannot.
        bool fullCoverage=false;
        std::array<float,4> clearColor{};
        float clearDepth=1; uint32_t clearStencil=0;
        // Optional assertion of the resource content version before this pass.
        std::optional<uint32_t> expectedVersion;
    };
    enum class DependencyKind { RAW, WAR, WAW, Explicit };
    struct PlanDependency { size_t before=0,after=0,resource=0; DependencyKind kind=DependencyKind::RAW; };
    struct ResourceLifetime { size_t first=size_t(-1),last=0; uint64_t estimatedBytes=0; bool exported=false; };
    struct CompiledRenderPlan
    {
        std::vector<PlanDependency> dependencies;
        std::vector<ResourceLifetime> lifetimes;
        std::vector<uint32_t> finalVersions;
        uint64_t scope=0;
        std::vector<std::string> resourceNames,passNames;
        uint64_t estimatedPeakBytes=0;
        std::string Dump() const;
    };
}
