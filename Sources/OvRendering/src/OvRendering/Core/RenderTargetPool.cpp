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
        uint64_t serial=0, released=0;
        bool leased=false;
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
    std::thread::id owner;
    std::shared_ptr<const void> domain;
    explicit State(uint64_t value):budget(value){}
    Statistics Stats() const {
        auto result=totals;
        for(const auto& entry:entries)if(entry->leased){result.leasedBytes+=entry->descriptor.EstimatedBytes();++result.leasedTargets;}
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
            for(auto it=entries.begin();it!=entries.end();++it)if(!(*it)->leased&&
                (oldest==entries.end()||(*it)->released<(*oldest)->released))oldest=it;
            if(oldest==entries.end()||(!closed&&idle<=budget))break;
            idle-=(*oldest)->descriptor.EstimatedBytes();removed.splice(removed.end(),entries,oldest);++totals.evictions;
        }
        return removed;
    }
    struct Lease
    {
        std::shared_ptr<State> state;
        std::shared_ptr<TargetEntry> entry;
        Lease(std::shared_ptr<State> s,std::shared_ptr<TargetEntry> e):state(std::move(s)),entry(std::move(e)){}
        ~Lease() {
            if(!entry)return;
            std::list<std::shared_ptr<TargetEntry>> removed;
            {std::lock_guard lock(state->mutex);entry->leased=false;entry->released=++state->clock;removed=state->Trim();}
        }
    };
};
struct Core::RenderResourceMapping::Storage
{
    uint64_t scope=0;
    struct Resource {
        uint32_t generation=0;
        std::string name;
        baregl::Texture::Snapshot imported;
        std::shared_ptr<TargetEntry> target;
    };
    // Leases outlive every mapping entry and are released after its snapshots.
    std::vector<std::shared_ptr<void>> leases;
    std::vector<Resource> resources;
    const Resource& Resolve(Data::ResourceHandle handle) const {
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
void Core::RenderTargetPool::Close()
{
    std::list<std::shared_ptr<TargetEntry>> removed;
    {std::lock_guard lock(m_state->mutex);m_state->closed=true;removed=m_state->Trim();}
}
std::shared_ptr<const Core::RenderResourceMapping> Core::RenderTargetPool::Realize(const Data::RenderPlan& plan,const Data::CompiledRenderPlan& compiled,bool reuse)
{
    if(compiled.scope!=plan.Scope()||compiled.lifetimes.size()!=plan.resources.size())throw std::invalid_argument("FrameGraph: compiled mapping does not belong to plan");
    return baregl::ResourceExecutor::Invoke([&]() -> std::shared_ptr<const RenderResourceMapping> {
        const auto domain=baregl::ResourceLifetime::CurrentDomain();
        {std::lock_guard lock(m_state->mutex);
            if(m_state->closed)throw std::logic_error("FrameGraph pool is closed");
            if(!m_state->bound){m_state->bound=true;m_state->owner=std::this_thread::get_id();m_state->domain=domain;}
            if(m_state->owner!=std::this_thread::get_id()||m_state->domain!=domain)throw std::logic_error("FrameGraph pool belongs to another GL resource domain");
        }
        auto storage=std::make_shared<RenderResourceMapping::Storage>();storage->scope=plan.Scope();storage->resources.resize(plan.resources.size());
        std::vector<size_t> order;
        for(size_t i=0;i<plan.resources.size();++i) {
            const auto& resource=plan.resources[i];storage->resources[i]={resource.generation,resource.name,resource.storage,{}};
            if(resource.origin==Data::ResourceOrigin::Transient&&compiled.lifetimes[i].first!=size_t(-1))order.push_back(i);
        }
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return compiled.lifetimes[a].first<compiled.lifetimes[b].first;});
        struct Slot {std::shared_ptr<TargetEntry> entry;size_t last;bool exported;};std::vector<Slot> slots;
        for(auto index:order) {
            const auto& desc=*plan.resources[index].descriptor;const auto& life=compiled.lifetimes[index];
            auto slot=reuse&&!life.exported?std::find_if(slots.begin(),slots.end(),[&](const auto& slot){return !slot.exported&&slot.last<life.first&&slot.entry->descriptor.Compatible(desc);}):slots.end();
            if(slot!=slots.end()) {
                storage->resources[index].target=slot->entry;slot->last=life.last;
                std::lock_guard lock(m_state->mutex);++m_state->totals.aliases;continue;
            }
            std::shared_ptr<TargetEntry> entry;
            // Allocate the lease before reserving an entry so failures cannot strand a reservation.
            auto lease=std::make_shared<State::Lease>(m_state,nullptr);
            {std::lock_guard lock(m_state->mutex);
                if(reuse)for(const auto& candidate:m_state->entries)if(!candidate->leased&&candidate->Alive()&&candidate->descriptor.Compatible(desc)) {entry=candidate;++m_state->totals.hits;break;}
                if(entry){entry->leased=true;lease->entry=entry;}
            }
            if(!entry) {
                entry=CreateTarget(desc);
                std::lock_guard lock(m_state->mutex);entry->serial=m_state->nextSerial++;
                m_state->entries.push_back(entry);entry->leased=true;lease->entry=entry;++m_state->totals.allocations;
            }
            storage->leases.push_back(std::move(lease));slots.push_back({entry,life.last,life.exported});storage->resources[index].target=entry;
            {std::lock_guard lock(m_state->mutex);m_state->totals.peakLeasedBytes=std::max(m_state->totals.peakLeasedBytes,m_state->Stats().leasedBytes);}
        }
        return std::shared_ptr<const RenderResourceMapping>(new RenderResourceMapping(std::move(storage)));
    });
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
    throw std::invalid_argument("FrameGraph mapping: no owned texture binding");
}
baregl::Framebuffer::Snapshot Core::RenderResourceMapping::Target(Data::ResourceHandle handle,uint16_t layer) const
{
    const auto& resource=m_storage->Resolve(handle);
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
