#include <OvRendering/Data/RenderPlan.h>
#include <OvRendering/Data/FrameGraphDiagnostics.h>
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <atomic>
#include <map>
#include <set>
#include <limits>
#include <OvRendering/Utils/Profiling.h>
#include <OvRendering/Core/RenderTargetPool.h>
using namespace OvRendering::Data;
namespace { CompiledRenderPlan CompileAccesses(const RenderPlan&,std::span<const std::shared_ptr<const RenderSceneSnapshot>>); }
namespace
{
    std::vector<ResourceAccess> EffectiveAccesses(const RenderPlan& plan,const RenderPlanPass& pass,
        const std::vector<PlanResourceDesc>& descriptors,std::span<const std::shared_ptr<const RenderSceneSnapshot>> views)
    {
        auto accesses=pass.accesses;
        const auto legacy=[&](const ResourceUse& use,bool write) {
            const auto& desc=descriptors[use.resource];
            for(unsigned layer=0;layer<desc.layers;++layer)if(use.subresources&(1u<<layer)) {
                ResourceAccess access{plan.Handle(use.resource),{desc.aspect,0,1,static_cast<uint16_t>(layer),1},write?ResourceAccessType::Attachment:ResourceAccessType::Sample};
                if(write) {
                    if(pass.view>=views.size()||!views[pass.view])throw std::invalid_argument("RenderPlan access inspection requires its view");
                    const auto& view=views[pass.view]->GetView();
                    const bool clear=desc.aspect==ResourceAspect::Depth?view.clearDepth:desc.aspect==ResourceAspect::DepthStencil?(view.clearDepth&&view.clearStencil):view.clearColor;
                    access.load=clear?AttachmentLoad::Clear:AttachmentLoad::Load;access.fullCoverage=clear;
                }
                accesses.push_back(access);
            }
        };
        for(const auto& use:pass.reads)legacy(use,false);
        for(const auto& use:pass.writes)legacy(use,true);
        return accesses;
    }
}
RenderPlan::RenderPlan() { static std::atomic_uint64_t next{1}; m_scope=next++; }
void RenderPlan::Validate(std::span<const std::shared_ptr<const RenderSceneSnapshot>> views) const { (void)Compile(views); }
CompiledRenderPlan RenderPlan::Compile(std::span<const std::shared_ptr<const RenderSceneSnapshot>> views) const
{
    std::string scope="RenderPlan";
    const auto require = [&](bool value, std::string_view message) { if (!value) throw std::invalid_argument(scope+": "+std::string(message)); };
    std::vector<uint8_t> available;
    std::unordered_set<uint64_t> identities;
    std::unordered_set<uint32_t> storages;
    for (const auto& resource : resources)
    {
        scope="RenderPlan resource "+std::to_string(resource.id);
        require(resource.id && identities.insert(resource.id).second, "Duplicate or missing resource identity");
        if (resource.origin==ResourceOrigin::Transient || resource.renderbuffer || (resource.descriptor&&resource.descriptor->kind==PlanResourceKind::Backbuffer))
        {
            require(resource.descriptor.has_value(), "Logical resource requires a descriptor");
            resource.descriptor->Validate();
            require(resource.origin!=ResourceOrigin::Transient || (!resource.storage.GetID()&&!resource.renderbuffer&&!resource.available&&resource.initialized.empty()),"Transient content has no producer");
        }
        else require(resource.storage.GetID() && storages.insert(resource.storage.GetID()).second, "Duplicate or missing storage generation");
        require(resource.width && resource.height && (resource.subresources == 1 || resource.subresources == 63), "Invalid resource extent/subresources");
        require(!(resource.available & ~resource.subresources), "Invalid imported/history subresources");
        require(resource.origin != ResourceOrigin::CurrentFrame || !resource.available, "Current-frame content has no producer");
        available.push_back(resource.available);
    }
    scope="RenderPlan views";
    require(viewScopes.empty()||viewScopes.size()==views.size(),"Wrong view scope count");
    std::set<std::pair<uint64_t,uint64_t>> viewIds;
    for(size_t i=0;i<views.size();++i)
        require(views[i]&&viewIds.emplace(viewScopes.empty()?Scope():viewScopes[i],views[i]->GetView().id).second,"Missing or duplicate view identity");
    const auto checkResource = [&](const ResourceUse& use) -> const PlanResource& {
        require(use.resource < resources.size(), "Unknown pass resource");
        const auto& resource = resources[use.resource];
        require(use.subresources && !(use.subresources & ~resource.subresources), "Invalid resource face");
        return resource;
    };
    std::vector<bool> used(views.size());
    size_t gpuScopeDepth=0;
    for (const auto& pass : passes)
    {
        scope="RenderPlan pass "+pass.name+" (view "+std::to_string(pass.viewId)+")";
        require(std::all_of(pass.gpuScopesBegin.begin(),pass.gpuScopesBegin.end(),[](const auto* source){return source!=nullptr;}),"GPU scope needs static source metadata");
        gpuScopeDepth+=pass.gpuScopesBegin.size();require(pass.gpuScopesEnd<=gpuScopeDepth,"Unmatched GPU scope");gpuScopeDepth-=pass.gpuScopesEnd;
        if (pass.view==RenderPlanPass::NoView)
        {
            require(!pass.name.empty()&&(bool(pass.operation)||bool(pass.prepareOperation)||bool(pass.commandBlock)),"Independent pass needs an owned operation");
            require(pass.stages.empty()&&pass.reads.empty()&&pass.writes.empty(),"Independent pass uses view-specific fields");
            for(const auto& access:pass.accesses)
                if((access.type==ResourceAccessType::Attachment||access.type==ResourceAccessType::CopyDestination||access.type==ResourceAccessType::MipmapDestination)&&
                    (access.fullCoverage||access.load==AttachmentLoad::Clear)&&access.store==AttachmentStore::Store&&access.range.firstMip==0)
                {
                    (void)Resolve(access.resource);
                    for(unsigned layer=access.range.firstLayer;layer<unsigned(access.range.firstLayer)+access.range.layerCount;++layer)if(layer<6)available[access.resource.index]|=uint8_t(1u<<layer);
                }
            continue;
        }
        require(pass.view < views.size(), "Pass references missing view");
        const auto& view = *views[pass.view];
        require(pass.viewId == view.GetView().id && pass.stages == view.GetPipeline().stages, "Wrong View/Stage reference");
        require(!used[pass.view], "Duplicate view submission"); used[pass.view] = true;
        std::vector<baregl::Framebuffer::Snapshot::TextureAttachmentInfo> attachments;
        if (pass.target)
        {
            require(pass.target->GetSize() == std::pair{view.GetView().width, view.GetView().height}, "Pass output dimensions differ from view");
            attachments = pass.target->GetTextureAttachments();
        }
        const auto checkUse = [&](const ResourceUse& use) -> const PlanResource& {
            const auto& resource = checkResource(use);
            require(resource.sceneGeneration==view.GetView().sceneGeneration||std::find(resource.sceneGenerations.begin(),resource.sceneGenerations.end(),view.GetView().sceneGeneration)!=resource.sceneGenerations.end(),"Resource belongs to another scene generation");
            return resource;
        };
        for (const auto& use : pass.reads)
        {
            const auto& resource = checkUse(use);
            require((available[use.resource] & use.subresources) == use.subresources, "Missing producer or invalid history content");
            for (const auto& attachment : attachments)
                require(attachment.storage.GetID() != resource.storage.GetID(), "Illegal framebuffer texture feedback");
        }
        for (const auto& use : pass.writes)
        {
            const auto& resource = checkUse(use);
            require(resource.width == view.GetView().width && resource.height == view.GetView().height, "Written resource dimensions differ from view");
            uint8_t attached = 0;
            for (const auto& attachment : attachments)
                if (attachment.writable && attachment.storage.GetID() == resource.storage.GetID())
                {
                    require(!attachment.layer || *attachment.layer < 6, "Invalid cubemap attachment layer");
                    attached |= attachment.layer ? uint8_t(1u << *attachment.layer) : resource.subresources;
                }
            require((attached & use.subresources) == use.subresources, "Wrong output attachment or cubemap face");
            available[use.resource] |= use.subresources;
        }
    }
    require(std::all_of(used.begin(), used.end(), [](bool value) { return value; }), "Prepared view has no pass");
    require(gpuScopeDepth==0,"Unfinished GPU scope");
    return CompileAccesses(*this,views);
}

ResourceHandle RenderPlan::Handle(size_t index) const
{
    if(index>=resources.size()||index>std::numeric_limits<uint32_t>::max()) throw std::invalid_argument("RenderPlan: unknown resource handle");
    return {m_scope,static_cast<uint32_t>(index),resources[index].generation};
}
const PlanResource& RenderPlan::Resolve(ResourceHandle handle) const
{
    if(handle.scope!=m_scope||handle.index>=resources.size()||handle.generation!=resources[handle.index].generation)
        throw std::invalid_argument("RenderPlan: foreign or stale resource handle");
    return resources[handle.index];
}
ResourceHandle RenderPlan::CreateTransient(std::string name,const PlanResourceDesc& descriptor)
{
    descriptor.Validate();if(descriptor.kind==PlanResourceKind::Backbuffer)throw std::invalid_argument("RenderPlan: backbuffer cannot be transient"); PlanResource resource;
    resource.id=resources.size()+1; resource.name=std::move(name); resource.descriptor=descriptor;
    resource.width=static_cast<uint16_t>(descriptor.texture.width);resource.height=static_cast<uint16_t>(descriptor.texture.height);
    resource.subresources=descriptor.layers==6?63:1; resource.origin=ResourceOrigin::Transient;
    resources.push_back(std::move(resource));return Handle(resources.size()-1);
}
ResourceHandle RenderPlan::Import(std::string name,const baregl::Texture::Snapshot& storage,ResourceOrigin origin,std::vector<ResourceRange> initialized)
{
    if(!storage.GetID()||origin==ResourceOrigin::Transient) throw std::invalid_argument("RenderPlan: invalid import");
    for(size_t i=0;i<resources.size();++i)if(resources[i].storage==storage)
    {
        if(resources[i].origin!=origin||resources[i].initialized!=initialized)
            throw std::invalid_argument("RenderPlan: repeated import cannot change content validity or history ownership");
        return Handle(i);
    }
    auto descriptor=PlanResourceDesc::From(storage);descriptor.Validate();PlanResource resource;
    resource.id=resources.size()+1;resource.storage=storage;resource.name=std::move(name);resource.descriptor=descriptor;
    resource.width=static_cast<uint16_t>(descriptor.texture.width);resource.height=static_cast<uint16_t>(descriptor.texture.height);
    resource.subresources=descriptor.layers==6?63:1;resource.origin=origin;resource.initialized=std::move(initialized);
    resources.push_back(std::move(resource));return Handle(resources.size()-1);
}
ResourceHandle RenderPlan::Import(std::string name,std::shared_ptr<const baregl::Renderbuffer> storage,ResourceOrigin origin,std::vector<ResourceRange> initialized)
{
    if(!storage||!storage->GetID()||origin==ResourceOrigin::Transient)throw std::invalid_argument("RenderPlan: invalid renderbuffer import");
    for(size_t i=0;i<resources.size();++i)if(resources[i].renderbuffer==storage) {
        if(resources[i].origin!=origin||resources[i].initialized!=initialized)throw std::invalid_argument("RenderPlan: repeated renderbuffer import changed content validity");
        return Handle(i);
    }
    PlanResourceDesc desc;desc.kind=PlanResourceKind::Renderbuffer;desc.sampleable=false;desc.texture.width=storage->GetWidth();desc.texture.height=storage->GetHeight();
    desc.texture.useMipMaps=false;desc.texture.internalFormat=storage->GetFormat();
    desc.aspect=storage->GetFormat()==baregl::types::EInternalFormat::DEPTH_STENCIL?ResourceAspect::DepthStencil:ResourceAspect::Depth;desc.Validate();
    PlanResource resource;resource.id=resources.size()+1;resource.name=std::move(name);resource.renderbuffer=std::move(storage);resource.descriptor=desc;
    resource.width=static_cast<uint16_t>(desc.texture.width);resource.height=static_cast<uint16_t>(desc.texture.height);resource.origin=origin;resource.initialized=std::move(initialized);
    resources.push_back(std::move(resource));return Handle(resources.size()-1);
}
std::optional<ResourceHandle> RenderPlan::Find(const baregl::Texture::Snapshot& storage) const
{
    for(size_t i=0;i<resources.size();++i)if(resources[i].storage==storage)return Handle(i);
    return {};
}
void RenderPlan::AddCommands(std::string name,RenderCommandList&& commands,std::vector<ResourceAccess> accesses)
{
    RenderPlanPass pass;pass.name=std::move(name);pass.view=RenderPlanPass::NoView;
    pass.commandBlock=std::make_shared<RenderCommandList>(std::move(commands));pass.accesses=std::move(accesses);passes.push_back(std::move(pass));
}
ResourceHandle RenderPlan::ImportBackbuffer(uint16_t width,uint16_t height,bool initialized)
{
    for(size_t i=0;i<resources.size();++i)if(resources[i].descriptor&&resources[i].descriptor->kind==PlanResourceKind::Backbuffer) {
        if(resources[i].width!=width||resources[i].height!=height)throw std::invalid_argument("RenderPlan: backbuffer extent changed during composition");
        return Handle(i);
    }
    PlanResourceDesc desc;desc.kind=PlanResourceKind::Backbuffer;desc.sampleable=false;desc.copyable=true;
    desc.texture.width=width;desc.texture.height=height;desc.texture.internalFormat=baregl::types::EInternalFormat::RGBA8;desc.texture.useMipMaps=false;
    desc.Validate();PlanResource resource;resource.id=resources.size()+1;resource.name="Window.Backbuffer";resource.descriptor=desc;
    resource.width=width;resource.height=height;resource.origin=initialized?ResourceOrigin::Imported:ResourceOrigin::CurrentFrame;
    if(initialized)resource.initialized.push_back({});resources.push_back(std::move(resource));return Handle(resources.size()-1);
}
void RenderPlan::Append(RenderPlan&& source,size_t viewOffset,size_t viewCount)
{
    if(&source==this||source.Scope()==Scope()||remaps.contains(source.Scope()))throw std::invalid_argument("RenderPlan: duplicate product fragment scope");
    if(viewScopes.empty()&&viewOffset)viewScopes.resize(viewOffset,Scope());
    if(viewScopes.size()!=viewOffset)throw std::invalid_argument("RenderPlan: fragment view offset mismatch");
    for(size_t i=0;i<viewCount;++i)viewScopes.push_back(source.viewScopes.empty()?source.Scope():source.viewScopes.at(i));
    std::vector<RemappedResource> mapped;
    for(auto& resource:source.resources) {
        const auto found=std::find_if(resources.begin(),resources.end(),[&](const auto& other) {
            return (resource.storage.GetID()&&resource.storage==other.storage)||(resource.renderbuffer&&resource.renderbuffer==other.renderbuffer)||
                (resource.descriptor&&other.descriptor&&resource.descriptor->kind==PlanResourceKind::Backbuffer&&other.descriptor->kind==PlanResourceKind::Backbuffer);
        });
        size_t index;
        if(found==resources.end()) {index=resources.size();resource.id=index+1;resources.push_back(std::move(resource));}
        else {
            index=static_cast<size_t>(found-resources.begin());
            if(resource.descriptor&&found->descriptor&&!resource.descriptor->Compatible(*found->descriptor))throw std::invalid_argument("RenderPlan: conflicting imported descriptors");
            if(!found->descriptor)found->descriptor=resource.descriptor;
            if(!found->textureOwner)found->textureOwner=std::move(resource.textureOwner);
            if(!found->importedTarget)found->importedTarget=std::move(resource.importedTarget);
            if(resource.sceneGeneration!=found->sceneGeneration)found->sceneGenerations.push_back(resource.sceneGeneration);
            found->sceneGenerations.insert(found->sceneGenerations.end(),resource.sceneGenerations.begin(),resource.sceneGenerations.end());
            // Later fragments cannot promote initial content. Earlier producers,
            // mip generation and Load requirements are checked in product order.
        }
        mapped.push_back({source.resources[mapped.size()].generation,Handle(index)});
    }
    const auto translate=[&](ResourceHandle old) {
        if(old.scope!=source.Scope()||old.index>=mapped.size()||old.generation!=mapped[old.index].generation)throw std::invalid_argument("RenderPlan: foreign fragment handle");
        return mapped[old.index].target;
    };
    for(auto& [scope,aliases]:source.remaps) {
        if(scope==Scope()||remaps.contains(scope))throw std::invalid_argument("RenderPlan: duplicate nested fragment scope");
        for(auto& alias:aliases)alias.target=translate(alias.target);
        remaps.emplace(scope,std::move(aliases));
    }
    std::vector<uint32_t> versionOffsets(resources.size());
    const bool needsVersions=std::any_of(source.passes.begin(),source.passes.end(),[](const auto& pass){return std::any_of(pass.accesses.begin(),pass.accesses.end(),[](const auto& access){return access.expectedVersion.has_value();});});
    if(needsVersions)for(const auto& pass:passes) {
        std::unordered_set<size_t> written;
        for(const auto& use:pass.writes)written.insert(use.resource);
        for(const auto& access:pass.accesses)if(access.type==ResourceAccessType::Attachment||access.type==ResourceAccessType::CopyDestination||access.type==ResourceAccessType::MipmapDestination)written.insert(access.resource.index);
        for(auto resource:written)++versionOffsets[resource];
    }
    const auto passOffset=passes.size();
    for(auto& pass:source.passes) {
        if(pass.view!=RenderPlanPass::NoView)pass.view+=viewOffset;
        for(auto& use:pass.reads)use.resource=mapped.at(use.resource).target.index;
        for(auto& use:pass.writes)use.resource=mapped.at(use.resource).target.index;
        for(auto& access:pass.accesses) {
            access.resource=translate(access.resource);
            if(access.expectedVersion) {
                const auto offset=versionOffsets[access.resource.index];
                if(*access.expectedVersion>UINT32_MAX-offset)throw std::invalid_argument("RenderPlan: product content version overflow");
                *access.expectedVersion+=offset;
            }
        }
        for(auto& previous:pass.after)previous+=passOffset;
        passes.push_back(std::move(pass));
    }
    remaps.emplace(source.Scope(),std::move(mapped));
}
void RenderPlan::GenerateMipmaps(std::string name,const std::shared_ptr<baregl::Texture>& texture)
{
    if(!texture)throw std::invalid_argument("RenderPlan: missing mipmap texture");
    const auto found=Find(texture->RetainStorage());if(!found)throw std::invalid_argument("RenderPlan: mipmap resource was not declared");
    const auto desc=PlanResourceDesc::From(texture->RetainStorage());
    RenderPlanPass pass;pass.view=RenderPlanPass::NoView;pass.name=std::move(name);
    pass.accesses.push_back({*found,{desc.aspect,0,1,0,desc.layers},ResourceAccessType::MipmapSource});
    if(desc.mips>1)pass.accesses.push_back({*found,{desc.aspect,1,static_cast<uint16_t>(desc.mips-1),0,desc.layers},ResourceAccessType::MipmapDestination,AttachmentLoad::DontCare,AttachmentStore::Store,true});
    pass.operation=[texture] {return [texture](auto&) {texture->GenerateMipmaps();};};passes.push_back(std::move(pass));
}
void RenderPlan::RecordOperations(RenderCommandList& commands,std::span<RenderCommandList> blocks,
    std::shared_ptr<const Core::RenderResourceMapping> mapping,std::optional<RenderPlanInspection> inspection) const
{
    if(inspection&&(inspection->beforePass>passes.size()||!inspection->operation))throw std::invalid_argument("RenderPlan: invalid inspection position");
    if(mapping)commands.KeepUntilRetired(mapping);
    std::vector<std::pair<size_t,const tracy::SourceLocationData*>> scopes;
    for(size_t index=0;index<=passes.size();++index)
    {
        if(inspection&&inspection->beforePass==index)commands.AppendDeferred(inspection->operation);
        if(index==passes.size())break;
        const auto& pass=passes[index];
        for(const auto* source:pass.gpuScopesBegin)scopes.emplace_back(commands.GetPendingCount(),source);
        if(pass.view==RenderPlanPass::NoView) {
            if(pass.commandBlock)commands.AppendRecording(std::move(*pass.commandBlock));
            else if(pass.prepareOperation){if(!mapping)throw std::logic_error("RenderPlan: missing physical resource mapping");commands.AppendDeferred(pass.prepareOperation(*mapping));}
            else commands.AppendDeferred(pass.operation);
        }
        else {if(pass.view>=blocks.size())throw std::invalid_argument("RenderPlan: missing recorded view block");commands.Append(std::move(blocks[pass.view]));}
        for(size_t i=0;i<pass.gpuScopesEnd;++i) {
            if(scopes.empty())throw std::invalid_argument("RenderPlan: unmatched GPU scope");
            const auto [first,source]=scopes.back();scopes.pop_back();
            commands.WrapOperations(first,[source](auto& driver,const auto& operations) {
#ifdef TRACY_ENABLE
                tracy::GpuCtxScope scope(source,true);
#endif
                for(const auto& operation:operations)operation(driver);
            });
        }
    }
    if(!scopes.empty())throw std::invalid_argument("RenderPlan: unfinished GPU scope");
}
namespace
{
    bool IsWrite(ResourceAccessType type)
    { return type==ResourceAccessType::Attachment||type==ResourceAccessType::CopyDestination||type==ResourceAccessType::MipmapDestination; }
    bool IsRead(const ResourceAccess& access)
    { return !IsWrite(access.type)||(access.type==ResourceAccessType::Attachment&&access.load==AttachmentLoad::Load); }
    void AdvanceContentVersions(std::vector<uint32_t>& versions,const std::vector<bool>& written)
    { for(size_t i=0;i<written.size();++i)if(written[i])++versions[i]; }
    struct Cell { bool initialized=false; size_t writer=size_t(-1);std::vector<size_t> readers; };
    struct AccessFlags { bool read=false,write=false,initialize=false,discard=false; };
    CompiledRenderPlan CompileAccesses(const RenderPlan& plan,std::span<const std::shared_ptr<const RenderSceneSnapshot>> views)
    {
        CompiledRenderPlan result;result.scope=plan.Scope();result.lifetimes.resize(plan.resources.size());result.finalVersions.resize(plan.resources.size());
        for(const auto& pass:plan.passes)result.passNames.push_back(pass.name+" (view "+std::to_string(pass.viewId)+")");
        std::vector<PlanResourceDesc> descriptors;std::vector<std::vector<Cell>> content;
        for(size_t i=0;i<plan.resources.size();++i)
        {
            const auto& resource=plan.resources[i]; auto descriptor=resource.descriptor?*resource.descriptor:PlanResourceDesc::From(resource.storage);
            result.resourceNames.push_back(resource.name.empty()?"storage "+std::to_string(resource.id):resource.name);
            // Legacy imports include assets with driver-selected formats. They
            // are not pool candidates; strict descriptor checks apply to new APIs.
            if(resource.descriptor) descriptor.Validate();
            descriptors.push_back(descriptor);content.emplace_back(size_t(descriptor.mips)*descriptor.layers*3);
            if(resource.descriptor)result.lifetimes[i].estimatedBytes=descriptor.EstimatedBytes();
        }
        const auto cells=[&](ResourceHandle handle,const ResourceRange& range) {
            (void)plan.Resolve(handle);const auto& desc=descriptors[handle.index];
            const auto aspect=uint8_t(range.aspect);
            if(!aspect||(aspect&~uint8_t(desc.aspect))||!range.mipCount||!range.layerCount||
                uint32_t(range.firstMip)+range.mipCount>desc.mips||uint32_t(range.firstLayer)+range.layerCount>desc.layers)
                throw std::invalid_argument("RenderPlan resource "+plan.resources[handle.index].name+": invalid aspect/mip/layer range");
            std::vector<size_t> selected;
            for(unsigned bit=0;bit<3;++bit)if(aspect&(1u<<bit))
                for(unsigned mip=range.firstMip;mip<uint32_t(range.firstMip)+range.mipCount;++mip)
                    for(unsigned layer=range.firstLayer;layer<uint32_t(range.firstLayer)+range.layerCount;++layer)
                        selected.push_back((bit*desc.mips+mip)*desc.layers+layer);
            return selected;
        };
        for(size_t i=0;i<plan.resources.size();++i)
        {
            const auto& resource=plan.resources[i];const auto& desc=descriptors[i];
            if(resource.origin==ResourceOrigin::CurrentFrame&&!resource.initialized.empty())throw std::invalid_argument("RenderPlan: current-frame resource initialized without a producer");
            for(const auto& range:resource.initialized)for(auto cell:cells(plan.Handle(i),range))content[i][cell].initialized=true;
            for(unsigned layer=0;layer<desc.layers;++layer)if(resource.available&(1u<<layer))
                for(auto cell:cells(plan.Handle(i),{desc.aspect,0,1,static_cast<uint16_t>(layer),1}))content[i][cell].initialized=true;
        }
        const auto edge=[&](size_t from,size_t to,size_t resource,DependencyKind kind) {
            if(from!=size_t(-1)&&from!=to && std::none_of(result.dependencies.begin(),result.dependencies.end(),[&](const auto& e){return e.before==from&&e.after==to&&e.resource==resource&&e.kind==kind;}))result.dependencies.push_back({from,to,resource,kind});
        };
        for(size_t passIndex=0;passIndex<plan.passes.size();++passIndex)
        {
            const auto& pass=plan.passes[passIndex];const auto scope="RenderPlan pass "+pass.name+": ";
            for(auto previous:pass.after) { if(previous>=passIndex)throw std::invalid_argument(scope+"explicit dependency conflicts with fixed order/cycle");edge(previous,passIndex,size_t(-1),DependencyKind::Explicit); }
            auto accesses=EffectiveAccesses(plan,pass,descriptors,views);
            std::map<std::pair<size_t,size_t>,AccessFlags> flags;
            std::optional<std::pair<uint32_t,uint32_t>> copyExtent;
            for(const auto& access:accesses)
            {
                const auto& resource=plan.Resolve(access.resource);const size_t index=access.resource.index;const auto& desc=descriptors[index];
                if(access.expectedVersion&&*access.expectedVersion!=result.finalVersions[index])throw std::invalid_argument(scope+"Stale content version: "+resource.name);
                const bool write=IsWrite(access.type);
                if(access.type==ResourceAccessType::Sample&&!desc.sampleable)throw std::invalid_argument(scope+"resource cannot be sampled");
                if(access.type==ResourceAccessType::Attachment&&!desc.attachment)throw std::invalid_argument(scope+"resource cannot be attached");
                if((access.type==ResourceAccessType::CopySource||access.type==ResourceAccessType::CopyDestination)&&!desc.copyable)throw std::invalid_argument(scope+"resource cannot be copied");
                if(access.type==ResourceAccessType::CopySource||access.type==ResourceAccessType::CopyDestination) {
                    const auto extent=std::pair{std::max(1u,desc.texture.width>>access.range.firstMip),std::max(1u,desc.texture.height>>access.range.firstMip)};
                    if(copyExtent&&*copyExtent!=extent)throw std::invalid_argument(scope+"copy dimensions differ");copyExtent=extent;
                    for(const auto& other:accesses)if(other.resource==access.resource&&IsWrite(other.type)&&access.type==ResourceAccessType::CopySource)throw std::invalid_argument(scope+"copy source aliases destination");
                }
                if(access.type==ResourceAccessType::MipmapSource||access.type==ResourceAccessType::MipmapDestination)
                    if(desc.kind==PlanResourceKind::Renderbuffer||access.range.layerCount!=desc.layers||access.range.firstLayer!=0||
                        (access.type==ResourceAccessType::MipmapSource?(access.range.firstMip!=0||access.range.mipCount!=1):(access.range.firstMip!=1||access.range.mipCount!=desc.mips-1)))
                        throw std::invalid_argument(scope+"mipmap operation must cover actual GL levels and all cube faces");
                if(access.type==ResourceAccessType::Sample)
                    for(const auto& other:accesses)if(other.resource==access.resource&&(other.type==ResourceAccessType::Attachment||other.type==ResourceAccessType::CopyDestination))throw std::invalid_argument(scope+"Illegal framebuffer texture feedback");
                if(access.type==ResourceAccessType::Attachment&&pass.target&&resource.origin!=ResourceOrigin::Transient)
                {
                    const auto [w,h]=pass.target->GetSize();
                    if(std::max(1u,desc.texture.width>>access.range.firstMip)!=w||std::max(1u,desc.texture.height>>access.range.firstMip)!=h)throw std::invalid_argument(scope+"attachment dimensions differ");
                    if(desc.kind==PlanResourceKind::Renderbuffer) {
                        const auto attached=pass.target->GetRenderbufferAttachments();
                        if(std::none_of(attached.begin(),attached.end(),[&](const auto& a){return a.storage==resource.renderbuffer;}))throw std::invalid_argument(scope+"Wrong renderbuffer attachment");
                    } else {
                        const auto attached=pass.target->GetTextureAttachments();
                        if(std::none_of(attached.begin(),attached.end(),[&](const auto& a){return a.writable&&a.storage==resource.storage&&(!a.layer||(access.range.firstLayer==*a.layer&&access.range.layerCount==1));}))throw std::invalid_argument(scope+"Wrong texture attachment/face");
                    }
                }
                auto& lifetime=result.lifetimes[index];lifetime.first=std::min(lifetime.first,passIndex);lifetime.last=std::max(lifetime.last,passIndex);
                if(access.type==ResourceAccessType::Export){lifetime.exported=true;lifetime.last=plan.passes.size();}
                for(auto cell:cells(access.resource,access.range)) {
                    auto& value=flags[{index,cell}];
                    if(value.write||(write&&value.read))throw std::invalid_argument(scope+"Overlapping accesses; declare attachment Load for blending");
                    value.read|=IsRead(access);
                    value.write|=write;value.discard|=write&&access.store==AttachmentStore::Discard;
                    value.initialize|=write&&(access.fullCoverage||(access.type==ResourceAccessType::Attachment&&access.load==AttachmentLoad::Clear));
                }
            }
            std::vector<bool> written(plan.resources.size());
            for(const auto& [key,value]:flags)
            {
                auto& cell=content[key.first][key.second];
                if(value.read) { if(!cell.initialized)throw std::invalid_argument(scope+"Missing producer/uninitialized Load or discarded content: "+plan.resources[key.first].name);edge(cell.writer,passIndex,key.first,DependencyKind::RAW);cell.readers.push_back(passIndex); }
                if(value.write) {
                    written[key.first]=true;
                    edge(cell.writer,passIndex,key.first,DependencyKind::WAW);
                    for(auto reader:cell.readers)edge(reader,passIndex,key.first,DependencyKind::WAR);
                    cell.readers.clear();cell.writer=passIndex;cell.initialized=!value.discard&&(cell.initialized||value.initialize);
                }
            }
            AdvanceContentVersions(result.finalVersions,written);
        }
        for(size_t pass=0;pass<plan.passes.size();++pass) {
            uint64_t live=0;for(size_t i=0;i<result.lifetimes.size();++i)if(plan.resources[i].origin==ResourceOrigin::Transient&&result.lifetimes[i].first<=pass&&result.lifetimes[i].last>=pass)live+=result.lifetimes[i].estimatedBytes;
            result.estimatedPeakBytes=std::max(result.estimatedPeakBytes,live);
        }
        return result;
    }
}

FrameGraphDiagnostics OvRendering::Data::CaptureFrameGraphDiagnostics(const RenderPlan& plan,const CompiledRenderPlan& compiled,
    const OvRendering::Core::RenderResourceMapping& mapping,std::span<const std::shared_ptr<const RenderSceneSnapshot>> views)
{
    if(compiled.scope!=plan.Scope()||compiled.lifetimes.size()!=plan.resources.size()||compiled.finalVersions.size()!=plan.resources.size())
        throw std::invalid_argument("FrameGraph diagnostics: foreign or incomplete compiled plan");
    FrameGraphDiagnostics result;result.scope=plan.Scope();result.estimatedPeakBytes=compiled.estimatedPeakBytes;result.dependencies=compiled.dependencies;
    std::vector<PlanResourceDesc> descriptors;descriptors.reserve(plan.resources.size());
    std::unordered_set<uint64_t> physical;
    for(size_t i=0;i<plan.resources.size();++i) {
        const auto& resource=plan.resources[i];const auto desc=resource.descriptor?*resource.descriptor:PlanResourceDesc::From(resource.storage);
        descriptors.push_back(desc);
        FrameGraphDiagnostics::Resource record;
        record.handle=plan.Handle(i);record.name=resource.name.empty()?"Resource "+std::to_string(i):resource.name;
        record.origin=resource.origin;record.kind=desc.kind;record.aspect=desc.aspect;record.format=desc.texture.internalFormat;
        record.width=desc.texture.width;record.height=desc.texture.height;record.mips=desc.mips;record.layers=desc.layers;record.samples=desc.samples;
        record.lifetime=compiled.lifetimes[i];record.finalVersion=compiled.finalVersions[i];record.physicalSlot=mapping.PhysicalID(record.handle);
        if(resource.origin==ResourceOrigin::Transient&&record.lifetime.first!=size_t(-1)) {
            result.logicalTransientBytes+=record.lifetime.estimatedBytes;
            if(record.physicalSlot&&physical.insert(record.physicalSlot).second)result.physicalTransientBytes+=record.lifetime.estimatedBytes;
        }
        result.resources.push_back(std::move(record));
    }
    result.physicalTransientTargets=physical.size();
    std::vector<uint32_t> versions(plan.resources.size());
    for(const auto& pass:plan.passes) {
        FrameGraphDiagnostics::Pass record;
        record.name=pass.name;record.viewId=pass.viewId;record.accesses=EffectiveAccesses(plan,pass,descriptors,views);
        record.contentVersions.reserve(record.accesses.size());
        std::vector<bool> written(plan.resources.size());
        for(const auto& access:record.accesses) {
            (void)plan.Resolve(access.resource);
            const auto r=access.resource.index;FrameGraphDiagnostics::Pass::AccessVersion version;
            if(IsRead(access))version.read=versions[r];
            if(IsWrite(access.type)){version.write=versions[r]+1;written[r]=true;}
            record.contentVersions.push_back(version);
        }
        // Several disjoint mip/layer writes in one pass share one new state.
        AdvanceContentVersions(versions,written);result.passes.push_back(std::move(record));
    }
    if(versions!=compiled.finalVersions)throw std::invalid_argument("FrameGraph diagnostics: content versions differ from compiled plan");
    // Imported attachments often have no explicit logical name. Identify them
    // by their first use without retaining the originating texture/view owner.
    for(size_t i=0;i<plan.resources.size();++i)if(plan.resources[i].name.empty()) {
        for(const auto& pass:result.passes)if(std::any_of(pass.accesses.begin(),pass.accesses.end(),[&](const auto& access){return access.resource.index==i;})) {
            const auto& resource=result.resources[i];
            const auto role=resource.kind==PlanResourceKind::Backbuffer?"Backbuffer":resource.kind==PlanResourceKind::TextureCube?"Cube":
                resource.aspect==ResourceAspect::Depth?"Depth":resource.aspect==ResourceAspect::DepthStencil?"DepthStencil":"Color";
            result.resources[i].name=pass.name+"."+role+" [R"+std::to_string(i)+"]";break;
        }
    }
    return result;
}
