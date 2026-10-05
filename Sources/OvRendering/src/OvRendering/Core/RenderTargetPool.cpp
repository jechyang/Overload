#include <OvRendering/Core/RenderTargetPool.h>
#include <baregl/ResourceExecutor.h>
#include <baregl/ResourceLifetime.h>
#include <algorithm>
#include <mutex>
#include <list>
#include <numeric>
#include <sstream>
#include <thread>

using namespace OvRendering;
namespace
{
    struct TargetEntry
    {
        Data::PlanResourceDesc descriptor;
        uint64_t serial=0, lastUse=0;
        bool leased=false;
        size_t pins=0;
        std::shared_ptr<baregl::Texture> texture;
        std::shared_ptr<baregl::Renderbuffer> renderbuffer;
        std::vector<std::shared_ptr<baregl::Framebuffer>> targets;
        std::vector<baregl::Framebuffer::Snapshot> snapshots;
        bool Alive() const { return texture ? texture->RetainStorage().GetID()!=0 : renderbuffer&&renderbuffer->GetID()!=0; }
    };
    std::shared_ptr<TargetEntry> CreateTarget(const Data::PlanResourceDesc& desc)
    {
        desc.Validate();auto entry=std::make_shared<TargetEntry>();entry->descriptor=desc;
        using enum baregl::types::EFramebufferAttachment;
        if(desc.kind==Data::PlanResourceKind::Renderbuffer) {
            entry->renderbuffer=std::make_shared<baregl::Renderbuffer>();
            entry->renderbuffer->Allocate(static_cast<uint16_t>(desc.texture.width),static_cast<uint16_t>(desc.texture.height),desc.texture.internalFormat);
        } else {
            entry->texture=std::make_shared<baregl::Texture>(desc.kind==Data::PlanResourceKind::TextureCube?baregl::types::ETextureType::TEXTURE_CUBE:baregl::types::ETextureType::TEXTURE_2D,"FrameGraph.Transient");
            entry->texture->Allocate(desc.texture);
        }
        if(desc.attachment)for(uint16_t layer=0;layer<desc.layers;++layer) {
            auto target=std::make_shared<baregl::Framebuffer>("FrameGraph.Target");
            const auto attachment=desc.aspect==Data::ResourceAspect::Color?COLOR:desc.aspect==Data::ResourceAspect::Depth?DEPTH:DEPTH_STENCIL;
            if(entry->texture)target->Attach(entry->texture,attachment,0,desc.layers==6?std::optional<uint32_t>(layer):std::nullopt);
            else target->Attach(entry->renderbuffer,attachment);
            if(desc.aspect!=Data::ResourceAspect::Color){target->SetTargetDrawBuffer({});target->SetTargetReadBuffer({});}
            if(!target->Validate())throw std::runtime_error("FrameGraph: invalid transient framebuffer");
            entry->snapshots.push_back(target->RecordTarget());entry->targets.push_back(std::move(target));
        }
        return entry;
    }
}
struct Core::RenderTargetPool::State
{
    mutable std::mutex mutex;
    std::list<std::shared_ptr<TargetEntry>> entries;
    Statistics totals;
    uint64_t budget, clock=0, nextSerial=1;
    bool closed=false, bound=false;
    std::thread::id owner,producer;
    std::shared_ptr<const void> binding;
    bool producerBound=false;
    size_t frameReserve=1;
    std::shared_ptr<const void> domain;
    explicit State(uint64_t value):budget(value){}
    Statistics Stats() const {
        auto result=totals;
        for(const auto& entry:entries)if(entry->leased||entry->pins){result.leasedBytes+=entry->descriptor.EstimatedBytes();++result.leasedTargets;}
            else {result.idleBytes+=entry->descriptor.EstimatedBytes();++result.idleTargets;}
        return result;
    }
    // Extract victims under the lock, destroy on the caller's resource owner.
    std::list<std::shared_ptr<TargetEntry>> Trim()
    {
        std::list<std::shared_ptr<TargetEntry>> removed;
        auto idle=Stats().idleBytes;
        for(;;) {
            auto oldest=entries.end();
            for(auto it=entries.begin();it!=entries.end();++it)if(!(*it)->leased&&!(*it)->pins&&
                (oldest==entries.end()||(*it)->lastUse<(*oldest)->lastUse))oldest=it;
            if(oldest==entries.end()||(!closed&&idle<=budget))break;
            idle-=(*oldest)->descriptor.EstimatedBytes();removed.splice(removed.end(),entries,oldest);++totals.evictions;
        }
        return removed;
    }
    struct Lease
    {
        std::shared_ptr<State> state;
        std::shared_ptr<TargetEntry> entry;
        bool borrowed=false;
        Lease(std::shared_ptr<State> s,std::shared_ptr<TargetEntry> e,bool b=false):state(std::move(s)),entry(std::move(e)),borrowed(b){}
        ~Lease() {
            if(!entry)return;
            std::list<std::shared_ptr<TargetEntry>> removed;
            {std::lock_guard lock(state->mutex);if(borrowed)--entry->pins;else entry->leased=false;entry->lastUse=++state->clock;
                if(!entry->leased&&!entry->pins&&!entry->Alive()) {
                    const auto found=std::find(state->entries.begin(),state->entries.end(),entry);
                    if(found!=state->entries.end())removed.splice(removed.end(),state->entries,found);
                }
                auto trimmed=state->Trim();removed.splice(removed.end(),trimmed);
            }
        }
    };
};
struct Core::RenderResourceMapping::Storage
{
    uint64_t scope=0;
    std::unordered_map<uint64_t,std::vector<Data::RenderPlan::RemappedResource>> remaps;
    struct Resource {
        uint32_t generation=0;
        std::string name;
        baregl::Texture::Snapshot imported;
        std::shared_ptr<TargetEntry> target;
        std::shared_ptr<baregl::Texture> owner;
        std::optional<baregl::Framebuffer::Snapshot> importedTarget;
    };
    // Leases outlive every mapping entry and are released after its snapshots.
    std::vector<std::shared_ptr<void>> leases;
    std::vector<Resource> resources;
    const Resource& Resolve(Data::ResourceHandle handle) const {
        if(handle.scope!=scope) {
            const auto alias=remaps.find(handle.scope);
            if(alias==remaps.end()||handle.index>=alias->second.size()||alias->second[handle.index].generation!=handle.generation)
                throw std::invalid_argument("FrameGraph mapping: foreign or stale fragment handle");
            handle=alias->second[handle.index].target;
        }
        if(handle.scope!=scope||handle.index>=resources.size()||resources[handle.index].generation!=handle.generation)
            throw std::invalid_argument("FrameGraph mapping: foreign or stale handle");
        return resources[handle.index];
    }
};
Core::RenderTargetPool::RenderTargetPool(uint64_t budget):m_state(std::make_shared<State>(budget)){}
Core::RenderTargetPool::~RenderTargetPool(){Close();}
Core::RenderTargetPool::Statistics Core::RenderTargetPool::GetStatistics() const
{std::lock_guard lock(m_state->mutex);return m_state->Stats();}
void Core::RenderTargetPool::SetIdleBudget(uint64_t budget)
{
    baregl::ResourceExecutor::Invoke([&] {
        std::list<std::shared_ptr<TargetEntry>> removed;
        {std::lock_guard lock(m_state->mutex);m_state->budget=budget;removed=m_state->Trim();}
    });
}
void Core::RenderTargetPool::SetFrameReserve(size_t count)
{
    if(!count||count>4)throw std::invalid_argument("FrameGraph: invalid frame reserve");
    std::lock_guard lock(m_state->mutex);if(m_state->producerBound&&m_state->producer!=std::this_thread::get_id())throw std::logic_error("FrameGraph reserve requires its producer");m_state->frameReserve=count;
}
void Core::RenderTargetPool::Close()
{
    std::list<std::shared_ptr<TargetEntry>> removed;
    {std::lock_guard lock(m_state->mutex);m_state->closed=true;removed=m_state->Trim();}
}
std::shared_ptr<const Core::RenderResourceMapping> Core::RenderTargetPool::Realize(const Data::RenderPlan& plan,const Data::CompiledRenderPlan& compiled,bool reuse)
{
    if(compiled.scope!=plan.Scope()||compiled.lifetimes.size()!=plan.resources.size())throw std::invalid_argument("FrameGraph: compiled mapping does not belong to plan");
    const auto binding=baregl::ResourceExecutor::CurrentBinding();
    {std::lock_guard lock(m_state->mutex);
        if(m_state->closed)throw std::logic_error("FrameGraph pool is closed");
        if(!m_state->producerBound){m_state->producerBound=true;m_state->producer=std::this_thread::get_id();m_state->binding=binding;}
        if(m_state->producer!=std::this_thread::get_id()||m_state->binding!=binding)throw std::logic_error("FrameGraph pool belongs to another producer/resource binding");
        if(m_state->bound&&!binding&&(m_state->owner!=std::this_thread::get_id()||m_state->domain!=baregl::ResourceLifetime::CurrentDomain()))
            throw std::logic_error("FrameGraph pool belongs to another GL resource domain");
    }
    auto storage=std::make_shared<RenderResourceMapping::Storage>();storage->scope=plan.Scope();storage->remaps=plan.remaps;storage->resources.resize(plan.resources.size());
    std::vector<size_t> order;
    for(size_t i=0;i<plan.resources.size();++i) {
        const auto& resource=plan.resources[i];storage->resources[i]={resource.generation,resource.name,resource.storage,{}};
        storage->resources[i].owner=resource.textureOwner;storage->resources[i].importedTarget=resource.importedTarget;
        if(resource.origin==Data::ResourceOrigin::Transient&&compiled.lifetimes[i].first!=size_t(-1))order.push_back(i);
    }
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return compiled.lifetimes[a].first<compiled.lifetimes[b].first;});
    struct Slot {std::shared_ptr<TargetEntry> entry;size_t last;bool exported;};std::vector<Slot> slots;
    std::vector<std::shared_ptr<TargetEntry>> pending;
    const auto reserve=[&](const Data::PlanResourceDesc& desc,bool leased) {
        auto entry=std::make_shared<TargetEntry>();entry->descriptor=desc;entry->leased=leased;
        pending.push_back(entry);
        std::lock_guard lock(m_state->mutex);entry->serial=m_state->nextSerial++;entry->lastUse=++m_state->clock;m_state->entries.push_back(entry);
        return entry;
    };
    try {
        // A debugger or material may retain a previous pool generation. Treat
        // that external read as a lease too, including overlapping product frames.
        std::vector<std::shared_ptr<TargetEntry>> externalEntries;
        {std::lock_guard lock(m_state->mutex);
            for(const auto& entry:m_state->entries)if(std::any_of(plan.resources.begin(),plan.resources.end(),[&](const auto& resource){
                return (entry->texture&&resource.storage.GetID()&&entry->texture->RetainStorage()==resource.storage)||
                    (entry->renderbuffer&&resource.renderbuffer==entry->renderbuffer);
            }))externalEntries.push_back(entry);
        }
        for(const auto& entry:externalEntries) {
            auto lease=std::make_shared<State::Lease>(m_state,nullptr,true);
            {std::lock_guard lock(m_state->mutex);lease->entry=entry;++entry->pins;}
            storage->leases.push_back(std::move(lease));
        }
        for(auto index:order) {
            const auto& desc=*plan.resources[index].descriptor;const auto& life=compiled.lifetimes[index];
            auto slot=reuse&&!life.exported?std::find_if(slots.begin(),slots.end(),[&](const auto& slot){return !slot.exported&&slot.last<life.first&&slot.entry->descriptor.Compatible(desc);}):slots.end();
            if(slot!=slots.end()) {
                storage->resources[index].target=slot->entry;slot->last=life.last;
                std::lock_guard lock(m_state->mutex);++m_state->totals.aliases;continue;
            }
            auto lease=std::make_shared<State::Lease>(m_state,nullptr);std::shared_ptr<TargetEntry> entry;
            {std::lock_guard lock(m_state->mutex);
                if(reuse)for(const auto& candidate:m_state->entries)if(!candidate->leased&&!candidate->pins&&candidate->Alive()&&candidate->descriptor.Compatible(desc)) {entry=candidate;++m_state->totals.hits;break;}
                if(entry){entry->leased=true;lease->entry=entry;}
            }
            if(!entry){entry=reserve(desc,true);lease->entry=entry;}
            storage->leases.push_back(std::move(lease));slots.push_back({entry,life.last,life.exported});storage->resources[index].target=entry;
        }
        // Only the producer reserves entries; retirement may concurrently return
        // leases. Spare sets make the bounded steady product cadence CPU-only.
        if(reuse&&m_state->frameReserve>1)for(size_t i=0;i<slots.size();++i) {
            const auto& desc=slots[i].entry->descriptor;
            if(std::any_of(slots.begin(),slots.begin()+i,[&](const auto& slot){return slot.entry->descriptor.Compatible(desc);}))continue;
            const auto needed=static_cast<size_t>(std::count_if(slots.begin(),slots.end(),[&](const auto& slot){return slot.entry->descriptor.Compatible(desc);})) * m_state->frameReserve;
            size_t existing;
            {std::lock_guard lock(m_state->mutex);existing=static_cast<size_t>(std::count_if(m_state->entries.begin(),m_state->entries.end(),[&](const auto& entry){return !entry->pins&&entry->descriptor.Compatible(desc);}));}
            while(existing++<needed)reserve(desc,false);
        }
        if(!pending.empty())baregl::ResourceExecutor::Invoke([&] {
            const auto domain=baregl::ResourceLifetime::CurrentDomain();
            {std::lock_guard lock(m_state->mutex);
                if(!m_state->bound){m_state->bound=true;m_state->owner=std::this_thread::get_id();m_state->domain=domain;}
                if(m_state->owner!=std::this_thread::get_id()||m_state->domain!=domain)throw std::logic_error("FrameGraph pool belongs to another GL resource domain");
            }
            for(auto& entry:pending) {
                auto realized=CreateTarget(entry->descriptor);
                entry->texture=std::move(realized->texture);entry->renderbuffer=std::move(realized->renderbuffer);
                entry->targets=std::move(realized->targets);entry->snapshots=std::move(realized->snapshots);
                std::lock_guard lock(m_state->mutex);++m_state->totals.allocations;
            }
        });
        {std::list<std::shared_ptr<TargetEntry>> removed;std::lock_guard lock(m_state->mutex);
            // Keep only the spare capacity needed by this graph recent. After a
            // resize, untouched spares must not be evicted ahead of old sizes on
            // every retirement. Excess entries retain their original LRU age.
            if(reuse)for(size_t i=0;i<slots.size();++i) {
                const auto& desc=slots[i].entry->descriptor;
                if(std::any_of(slots.begin(),slots.begin()+i,[&](const auto& slot){return slot.entry->descriptor.Compatible(desc);}))continue;
                size_t needed=static_cast<size_t>(std::count_if(slots.begin(),slots.end(),[&](const auto& slot){return slot.entry->descriptor.Compatible(desc);})) * m_state->frameReserve;
                for(const auto& entry:m_state->entries)if(!entry->pins&&entry->leased&&entry->descriptor.Compatible(desc)&&needed)--needed;
                for(const auto& entry:m_state->entries)if(needed&&!entry->pins&&!entry->leased&&entry->Alive()&&entry->descriptor.Compatible(desc)) {
                    entry->lastUse=++m_state->clock;--needed;
                }
            }
            m_state->totals.peakLeasedBytes=std::max(m_state->totals.peakLeasedBytes,m_state->Stats().leasedBytes);removed=m_state->Trim();}
        return std::shared_ptr<const RenderResourceMapping>(new RenderResourceMapping(std::move(storage)));
    }catch(...) {
        // Empty placeholders are not physical idle resources. Leased entries
        // roll back through the same RAII path; unleased spares are unlinked here.
        std::list<std::shared_ptr<TargetEntry>> removed;
        {std::lock_guard lock(m_state->mutex);for(auto it=m_state->entries.begin();it!=m_state->entries.end();) {
            auto current=it++;if(!(*current)->leased&&!(*current)->pins&&!(*current)->Alive())removed.splice(removed.end(),m_state->entries,current);
        }}
        throw;
    }
}
baregl::Texture::Snapshot Core::RenderResourceMapping::TextureStorage(Data::ResourceHandle handle) const
{
    const auto& resource=m_storage->Resolve(handle);
    if(resource.target&&resource.target->texture)return resource.target->texture->RetainStorage();
    if(resource.imported.GetID())return resource.imported;
    throw std::invalid_argument("FrameGraph mapping: resource is not a realized texture");
}
std::shared_ptr<baregl::Texture> Core::RenderResourceMapping::Texture(Data::ResourceHandle handle) const
{
    const auto& resource=m_storage->Resolve(handle);
    if(resource.target&&resource.target->texture)return resource.target->texture;
    if(resource.owner) {
        if(resource.owner->RetainStorage()!=resource.imported)
            throw std::invalid_argument("FrameGraph mapping: imported texture owner changed storage generation");
        return resource.owner;
    }
    throw std::invalid_argument("FrameGraph mapping: no owned texture binding");
}
baregl::Framebuffer::Snapshot Core::RenderResourceMapping::Target(Data::ResourceHandle handle,uint16_t layer) const
{
    const auto& resource=m_storage->Resolve(handle);
    if(resource.importedTarget&&layer==0)return *resource.importedTarget;
    if(!resource.target||layer>=resource.target->snapshots.size())throw std::invalid_argument("FrameGraph mapping: invalid target/layer");
    return resource.target->snapshots[layer];
}
uint64_t Core::RenderResourceMapping::PhysicalID(Data::ResourceHandle handle) const
{
    const auto& resource=m_storage->Resolve(handle);return resource.target?resource.target->serial:0;
}
std::string Core::RenderResourceMapping::Dump() const
{
    std::ostringstream out;out<<"mapping scope="<<m_storage->scope<<'\n';
    for(size_t i=0;i<m_storage->resources.size();++i) {const auto& r=m_storage->resources[i];
        out<<"resource "<<i<<" "<<r.name<<" physical="<<(r.target?r.target->serial:0)<<" texture="<<(r.target&&r.target->texture?r.target->texture->RetainStorage().GetID():r.imported.GetID())<<'\n';}
    return out.str();
}
