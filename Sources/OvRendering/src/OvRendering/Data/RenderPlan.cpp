#include <OvRendering/Data/RenderPlan.h>
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <atomic>
#include <map>
#include <limits>
using namespace OvRendering::Data;
namespace { CompiledRenderPlan CompileAccesses(const RenderPlan&,std::span<const std::shared_ptr<const RenderSceneSnapshot>>); }
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
        if (resource.origin==ResourceOrigin::Transient || resource.renderbuffer)
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
    std::unordered_set<uint64_t> viewIds;
    for (const auto& view : views)
        require(view && viewIds.insert(view->GetView().id).second, "Missing or duplicate view identity");
    const auto checkResource = [&](const ResourceUse& use) -> const PlanResource& {
        require(use.resource < resources.size(), "Unknown pass resource");
        const auto& resource = resources[use.resource];
        require(use.subresources && !(use.subresources & ~resource.subresources), "Invalid resource face");
        return resource;
    };
    std::vector<bool> used(views.size());
    for (const auto& pass : passes)
    {
        scope="RenderPlan pass "+pass.name+" (view "+std::to_string(pass.viewId)+")";
        if (pass.view==RenderPlanPass::NoView)
        {
            require(!pass.name.empty()&&bool(pass.operation),"Independent pass needs an owned operation");
            require(pass.stages.empty()&&pass.reads.empty()&&pass.writes.empty()&&!pass.postProcessAfter&&!pass.generateMipmaps,"Independent pass uses view-specific fields");
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
            require(resource.sceneGeneration == view.GetView().sceneGeneration, "Resource belongs to another scene generation");
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
        if (pass.postProcessAfter)
        {
            require(pass.target.has_value() && std::find(pass.stages.begin(),pass.stages.end(),RenderStage::SceneUI)==pass.stages.end(), "Post-process output must precede SceneUI");
            require(!pass.postProcessReads.empty() && !pass.postProcessWrites.empty(), "Post-process requires declared input/output resources");
            // Sequential blit through private ping-pong: a resource may be read then written.
            for (const auto& use : pass.postProcessReads)
            {
                const auto& resource = checkUse(use);
                require((available[use.resource] & use.subresources) == use.subresources, "Post-process input has no producer");
            }
            for (const auto& use : pass.postProcessWrites)
            {
                const auto& resource = checkUse(use);
                require(resource.width == view.GetView().width && resource.height == view.GetView().height, "Post-process output dimensions differ from view");
                available[use.resource] |= use.subresources;
            }
        }
        else
            require(pass.postProcessReads.empty() && pass.postProcessWrites.empty(), "Post-process resources without postProcessAfter");
        if (pass.generateMipmaps)
        {
            const auto id = pass.generateMipmaps->RetainStorage().GetID();
            const auto found = std::find_if(resources.begin(), resources.end(), [&](const auto& r) { return r.storage.GetID() == id; });
            require(found != resources.end() && found->subresources == 63 && available[found - resources.begin()] == 63, "Mipmaps require all six valid faces");
        }
    }
    require(std::all_of(used.begin(), used.end(), [](bool value) { return value; }), "Prepared view has no pass");
    for (const auto& step : trailing)
    {
        scope="RenderPlan trailing "+step.name;
        require(!step.name.empty(), "Trailing composition needs a name");
        for (const auto& use : step.reads)
        {
            checkResource(use);
            require((available[use.resource] & use.subresources) == use.subresources, "Trailing composition input has no producer");
        }
        for (const auto& use : step.writes)
        {
            checkResource(use);
            available[use.resource] |= use.subresources;
        }
    }
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
    descriptor.Validate(); PlanResource resource;
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
namespace
{
    bool IsWrite(ResourceAccessType type)
    { return type==ResourceAccessType::Attachment||type==ResourceAccessType::CopyDestination||type==ResourceAccessType::MipmapDestination; }
    struct Cell { bool initialized=false; size_t writer=size_t(-1);std::vector<size_t> readers; };
    struct AccessFlags { bool read=false,write=false,initialize=false,discard=false; };
    CompiledRenderPlan CompileAccesses(const RenderPlan& plan,std::span<const std::shared_ptr<const RenderSceneSnapshot>> views)
    {
        CompiledRenderPlan result;result.scope=plan.Scope();result.lifetimes.resize(plan.resources.size());
        for(const auto& pass:plan.passes)result.passNames.push_back(pass.name+" (view "+std::to_string(pass.viewId)+")");
        for(const auto& trailing:plan.trailing)result.passNames.push_back(trailing.name+" (external)");
        std::vector<PlanResourceDesc> descriptors;std::vector<std::vector<Cell>> content;
        for(size_t i=0;i<plan.resources.size();++i)
        {
            const auto& resource=plan.resources[i]; auto descriptor=resource.descriptor.value_or(PlanResourceDesc::From(resource.storage));
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
            auto accesses=pass.accesses;
            const auto legacy=[&](const ResourceUse& use,bool write) {
                const auto& desc=descriptors[use.resource];
                for(unsigned layer=0;layer<desc.layers;++layer)if(use.subresources&(1u<<layer)) {
                    ResourceAccess access{plan.Handle(use.resource),{desc.aspect,0,1,static_cast<uint16_t>(layer),1},write?ResourceAccessType::Attachment:ResourceAccessType::Sample};
                    if(write) {
                        const auto& view=views[pass.view]->GetView();
                        const bool clear=desc.aspect==ResourceAspect::Depth?view.clearDepth:desc.aspect==ResourceAspect::DepthStencil?(view.clearDepth&&view.clearStencil):view.clearColor;
                        access.load=clear?AttachmentLoad::Clear:AttachmentLoad::Load;access.fullCoverage=clear;
                    }
                    accesses.push_back(access);
                }
            };
            for(const auto& use:pass.reads)legacy(use,false);
            for(const auto& use:pass.writes)legacy(use,true);
            std::map<std::pair<size_t,size_t>,AccessFlags> flags;
            std::optional<std::pair<uint32_t,uint32_t>> copyExtent;
            for(const auto& access:accesses)
            {
                const auto& resource=plan.Resolve(access.resource);const size_t index=access.resource.index;const auto& desc=descriptors[index];
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
                    if(desc.kind==PlanResourceKind::Renderbuffer||desc.mips<2||access.range.layerCount!=desc.layers||access.range.firstLayer!=0||
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
                auto& lifetime=result.lifetimes[index];lifetime.first=std::min(lifetime.first,passIndex);lifetime.last=passIndex;
                for(auto cell:cells(access.resource,access.range)) {
                    auto& value=flags[{index,cell}];value.read|=!write||(access.type==ResourceAccessType::Attachment&&access.load==AttachmentLoad::Load);
                    value.write|=write;value.discard|=write&&access.store==AttachmentStore::Discard;
                    value.initialize|=write&&(access.fullCoverage||(access.type==ResourceAccessType::Attachment&&access.load==AttachmentLoad::Clear));
                }
            }
            for(const auto& [key,value]:flags)
            {
                auto& cell=content[key.first][key.second];
                if(value.read) { if(!cell.initialized)throw std::invalid_argument(scope+"Missing producer/uninitialized Load or discarded content: "+plan.resources[key.first].name);edge(cell.writer,passIndex,key.first,DependencyKind::RAW);cell.readers.push_back(passIndex); }
                if(value.write) {
                    edge(cell.writer,passIndex,key.first,DependencyKind::WAW);
                    for(auto reader:cell.readers)edge(reader,passIndex,key.first,DependencyKind::WAR);
                    cell.readers.clear();cell.writer=passIndex;cell.initialized=!value.discard&&(cell.initialized||value.initialize);
                }
            }
        }
        for(size_t step=0;step<plan.trailing.size();++step) {
            const size_t node=plan.passes.size()+step;const auto& trailing=plan.trailing[step];
            const auto touch=[&](const ResourceUse& use,bool write) {
                auto& lifetime=result.lifetimes[use.resource];lifetime.first=std::min(lifetime.first,node);lifetime.last=node;
                const auto& desc=descriptors[use.resource];
                for(unsigned layer=0;layer<desc.layers;++layer)if(use.subresources&(1u<<layer))
                    for(auto index:cells(plan.Handle(use.resource),{desc.aspect,0,1,static_cast<uint16_t>(layer),1})) {
                        auto& cell=content[use.resource][index];
                        if(!cell.initialized)throw std::invalid_argument("RenderPlan trailing "+trailing.name+": uninitialized content/Load");
                        edge(cell.writer,node,use.resource,DependencyKind::RAW);
                        if(write) {edge(cell.writer,node,use.resource,DependencyKind::WAW);for(auto reader:cell.readers)edge(reader,node,use.resource,DependencyKind::WAR);cell.writer=node;cell.readers.clear();}
                        else cell.readers.push_back(node);
                    }
            };
            for(const auto& use:trailing.reads)touch(use,false);for(const auto& use:trailing.writes)touch(use,true);
        }
        for(size_t pass=0;pass<plan.passes.size()+plan.trailing.size();++pass) {
            uint64_t live=0;for(size_t i=0;i<result.lifetimes.size();++i)if(plan.resources[i].origin==ResourceOrigin::Transient&&result.lifetimes[i].first<=pass&&result.lifetimes[i].last>=pass)live+=result.lifetimes[i].estimatedBytes;
            result.estimatedPeakBytes=std::max(result.estimatedPeakBytes,live);
        }
        return result;
    }
}
