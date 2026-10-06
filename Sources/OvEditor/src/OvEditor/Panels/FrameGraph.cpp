#include <OvEditor/Panels/FrameGraph.h>
#include <OvUI/Widgets/AWidget.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <numeric>
#include <sstream>
#include <fstream>
#include <OvWindowing/Dialogs/SaveFileDialog.h>
#include <OvUI/Internal/TextureID.h>

using namespace OvRendering::Data;
namespace
{
    constexpr double MiB=1024.0*1024.0;
    class Contents final : public OvUI::Widgets::AWidget
    {
    public:
        explicit Contents(std::function<void()> draw):m_draw(std::move(draw)){}
    private:
        void _Draw_Impl() override {m_draw();}
        std::function<void()> m_draw;
    };
    const char* Origin(ResourceOrigin v)
    {
        switch(v){case ResourceOrigin::Transient:return "Transient";case ResourceOrigin::Imported:return "Imported";
            case ResourceOrigin::History:return "History";default:return "Output";}
    }
    const char* Kind(PlanResourceKind v)
    {
        switch(v){case PlanResourceKind::TextureCube:return "Cube";case PlanResourceKind::Renderbuffer:return "Renderbuffer";
            case PlanResourceKind::Backbuffer:return "Backbuffer";default:return "2D";}
    }
    const char* Access(ResourceAccessType v)
    {
        switch(v){case ResourceAccessType::Sample:return "Sample";case ResourceAccessType::Attachment:return "Attachment";
            case ResourceAccessType::CopySource:return "Copy source";case ResourceAccessType::CopyDestination:return "Copy destination";
            case ResourceAccessType::MipmapSource:return "Mipmap source";case ResourceAccessType::MipmapDestination:return "Mipmap destination";
            case ResourceAccessType::Readback:return "Readback";case ResourceAccessType::Export:return "Export";default:return "Present";}
    }
    const char* Dependency(DependencyKind v)
    {
        switch(v){case DependencyKind::RAW:return "Read after write";case DependencyKind::WAR:return "Write after read";
            case DependencyKind::WAW:return "Write after write";default:return "Explicit order";}
    }
    ImU32 EdgeColor(DependencyKind kind,float alpha=1)
    {
        const ImVec4 colors[]={{.35f,.78f,1,alpha},{1,.72f,.3f,alpha},{1,.4f,.48f,alpha},{.7f,.65f,.95f,alpha}};
        return ImGui::GetColorU32(colors[static_cast<size_t>(kind)]);
    }
    ImVec2 Curve(ImVec2 a,ImVec2 b,ImVec2 c,ImVec2 d,float t)
    {
        const float s=1-t;return {s*s*s*a.x+3*s*s*t*b.x+3*s*t*t*c.x+t*t*t*d.x,
            s*s*s*a.y+3*s*s*t*b.y+3*s*t*t*c.y+t*t*t*d.y};
    }
    float SegmentDistance(ImVec2 p,ImVec2 a,ImVec2 b)
    {
        const float x=b.x-a.x,y=b.y-a.y;
        const float t=std::clamp(((p.x-a.x)*x+(p.y-a.y)*y)/std::max(.001f,x*x+y*y),0.f,1.f);
        return std::hypot(p.x-a.x-t*x,p.y-a.y-t*y);
    }
    const char* Format(baregl::types::EInternalFormat v)
    {
        using enum baregl::types::EInternalFormat;
        switch(v){case RGBA32F:return "RGBA32F";case RGBA16F:return "RGBA16F";case RGBA8:return "RGBA8";
            case R32F:return "R32F";case R16F:return "R16F";case RG32F:return "RG32F";case RGB32F:return "RGB32F";
            case RGB8:return "RGB8";case RGB:return "RGB";case RGBA:return "RGBA";case SRGB8_ALPHA8:return "sRGBA8";
            case DEPTH_COMPONENT:return "Depth";case DEPTH_STENCIL:return "Depth/Stencil";
            default:return "Other format";}
    }
    std::string Lower(std::string value)
    {
        for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;
    }
    bool Matches(const std::string& name,const char* filter){return Lower(name).find(Lower(filter))!=std::string::npos;}
    std::string VersionText(const FrameGraphDiagnostics::Pass::AccessVersion& version)
    {
        if(version.read&&version.write)return std::format("Read v{} -> write v{}",*version.read,*version.write);
        return version.read?std::format("Read v{}",*version.read):version.write?std::format("Write v{}",*version.write):"No content access";
    }
    void ContinueButtonRow(const std::string& label,bool first)
    {
        const auto& style=ImGui::GetStyle();
        const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
        if(!first&&ImGui::GetItemRectMax().x+style.ItemSpacing.x+ImGui::CalcTextSize(label.c_str()).x+2*style.FramePadding.x<=right)ImGui::SameLine();
    }
    ImU32 SlotColor(uint64_t id)
    {
        float r,g,b;ImGui::ColorConvertHSVtoRGB(float(id%13)/13.f,.55f,.8f,r,g,b);
        return ImGui::GetColorU32(ImVec4(r,g,b,1));
    }
    ImU32 GpuHeatColor(uint64_t ns,uint64_t maximum)
    {
        const float cost=maximum?std::clamp(float(double(ns)/maximum),0.f,1.f):0;
        const ImVec4 low={.13f,.23f,.29f,1},middle={.55f,.40f,.17f,1},high={.71f,.24f,.18f,1};
        const auto a=cost<.5f?low:middle,b=cost<.5f?middle:high;const float t=cost<.5f?cost*2:(cost-.5f)*2;
        return ImGui::GetColorU32(ImVec4(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,1));
    }
    std::string GpuCostText(const FrameGraphGpuTimings& timings,size_t pass)
    {
        const auto ns=timings.passNanoseconds[pass];
        return std::format("{:.3f} ms | {:.1f}%",ns/1e6,timings.totalNanoseconds?double(ns)*100/timings.totalNanoseconds:0);
    }
    void Lifetime(const FrameGraphDiagnostics::Resource& resource,size_t passes)
    {
        const auto& life=resource.lifetime;
        if(life.first==size_t(-1)){ImGui::TextDisabled("Unused");return;}
        const float width=std::max(48.f,ImGui::GetContentRegionAvail().x),height=ImGui::GetTextLineHeight();
        const auto pos=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();
        const float count=float(std::max(size_t{1},passes));
        const float start=float(life.first)/count,end=float(std::min(life.last+1,passes))/count;
        ImGui::InvisibleButton("lifetime",{width,height});
        draw->AddRectFilled(pos,{pos.x+width,pos.y+height},ImGui::GetColorU32(ImGuiCol_FrameBg),3);
        draw->AddRectFilled({pos.x+width*start,pos.y},{pos.x+std::max(width*start+2,width*end),pos.y+height},SlotColor(resource.physicalSlot),3);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("P%zu -> P%zu%s",life.first,life.last,life.exported?" (exported through end of plan)":"");
    }
    struct LifetimeRow
    {
        uint64_t slot=0,bytes=0;
        std::vector<size_t> resources;
    };
    std::vector<uint64_t> ScratchProfile(const FrameGraphDiagnostics& graph)
    {
        const auto count=graph.passes.size();std::vector<uint64_t> starts(count),ends(count),result(count);
        for(const auto& resource:graph.resources)if(resource.origin==ResourceOrigin::Transient&&resource.lifetime.first<count) {
            const auto& life=resource.lifetime;starts[life.first]+=life.estimatedBytes;ends[std::min(life.last,count-1)]+=life.estimatedBytes;
        }
        uint64_t live=0;for(size_t p=0;p<count;++p){live+=starts[p];result[p]=live;live-=ends[p];}return result;
    }
}

#include "FrameGraphReport.inl"

OvEditor::Panels::FrameGraph::FrameGraph(const std::string& title,bool opened,const OvUI::Settings::PanelWindowSettings& settings)
    :PanelWindow(title,opened,settings)
{
    minSize={520,360};CreateWidget<Contents>([this]{DrawContents();});
    CloseEvent += [this]{RequireUIThread();m_snapshot.reset();m_graphNodes.clear();m_graphEdges.clear();m_frozen=false;m_graphFit=true;
        m_lifetimeBars.clear();m_lifetimeLiveBytes.clear();m_lifetimeProfileRect={};m_poolEntries.clear();m_exportStatus.clear();
        m_previewRequest.reset();m_previewTicket.reset();m_previewImage.reset();m_previewStatus.clear();m_previewSequence=0;m_gpuTimings.reset();m_captureGpuTimings=false;m_graphGpuHeat=false;};
}
void OvEditor::Panels::FrameGraph::RequireUIThread() const
{
    if(std::this_thread::get_id()!=m_uiThread)throw std::logic_error("Frame Graph panel requires its UI thread");
}
bool OvEditor::Panels::FrameGraph::WantsCapture() const {RequireUIThread();return IsOpened()&&!m_frozen;}
bool OvEditor::Panels::FrameGraph::WantsGpuTimings() const {RequireUIThread();return WantsCapture()&&m_captureGpuTimings;}
void OvEditor::Panels::FrameGraph::UpdateGpuTimings(std::shared_ptr<const FrameGraphGpuTimings> timings)
{
    RequireUIThread();
    if(!IsOpened()){m_gpuTimings.reset();return;}
    // Keep sorted rows under the pointer stable from press to release. A new
    // sample may reorder equal-cost Passes and otherwise cancel the click.
    const bool pressing=ImGui::GetCurrentContext()&&ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if(!m_frozen&&!pressing&&timings)m_gpuTimings=std::move(timings);
}
void OvEditor::Panels::FrameGraph::Update(std::shared_ptr<const FrameGraphDiagnostics> snapshot,const OvRendering::Core::RenderTargetPool::Statistics& pool,
    std::vector<OvRendering::Core::RenderTargetPool::MemoryEntry> entries)
{
    RequireUIThread();m_pool=pool;
    if(!m_budgetEdited)m_budgetMiB=static_cast<int>(std::min(uint64_t{4096},pool.idleBudget/uint64_t(MiB)));
    if(!IsOpened()){m_snapshot.reset();m_poolEntries.clear();m_graphNodes.clear();m_graphEdges.clear();m_lifetimeBars.clear();m_lifetimeLiveBytes.clear();m_frozen=false;return;}
    m_poolEntries=std::move(entries);
    if(!m_frozen&&snapshot)m_snapshot=std::move(snapshot);
    if(m_snapshot){
        if(m_selectedPass>=m_snapshot->passes.size())m_selectedPass=0;
        if(m_selectedResource>=m_snapshot->resources.size())m_selectedResource=0;
        if(m_selectedResource<m_snapshot->resources.size())m_selectedVersion=std::min(m_selectedVersion,m_snapshot->resources[m_selectedResource].finalVersion);
        if(!m_graphSelectionInitialized&&!m_snapshot->passes.empty()) {
            std::vector<size_t> degree(m_snapshot->passes.size());
            for(const auto& edge:m_snapshot->dependencies){++degree[edge.before];++degree[edge.after];}
            m_selectedPass=static_cast<size_t>(std::max_element(degree.begin(),degree.end())-degree.begin());
            m_graphSelectionInitialized=true;m_graphFit=true;
        }
    }
}
OvEditor::Panels::FrameGraph::PoolRequests OvEditor::Panels::FrameGraph::TakePoolRequests()
{
    RequireUIThread();auto result=m_requests;m_requests={};return result;
}
std::optional<FrameGraphPreviewRequest> OvEditor::Panels::FrameGraph::TakePreviewRequest()
{
    RequireUIThread();auto result=std::move(m_previewRequest);m_previewRequest.reset();return WantsCapture()?result:std::nullopt;
}
bool OvEditor::Panels::FrameGraph::TakePreviewClearRequest(){RequireUIThread();const bool result=m_clearPreview;m_clearPreview=false;return result;}
void OvEditor::Panels::FrameGraph::UpdatePreview(std::shared_ptr<const OvCore::Rendering::RenderFrontend::PreviewTicket> ticket)
{
    RequireUIThread();
    if(!IsOpened()){m_previewImage.reset();m_previewTicket.reset();m_previewRequest.reset();return;}
    if(ticket&&ticket->sequence>m_previewSequence){m_previewSequence=ticket->sequence;m_previewTicket=std::move(ticket);m_previewStatus="Capturing; waiting for GPU completion";}
    if(m_previewTicket&&m_previewTicket->completion.IsRetired()) {
        if(!m_previewTicket->error.empty())m_previewStatus=m_previewTicket->error;
        else if(m_previewTicket->completion.Succeeded()&&m_previewTicket->image){m_previewImage=m_previewTicket->image;m_previewStatus="Captured image; click Capture again to refresh";}
        else m_previewStatus="Capture cancelled or failed; the previous image is unchanged.";
        m_previewTicket.reset();
    }
}
void OvEditor::Panels::FrameGraph::QueuePreview()
{
    RequireUIThread();
    if(!WantsCapture()||!m_snapshot||m_selectedResource>=m_snapshot->resources.size())return;
    const auto& resource=m_snapshot->resources[m_selectedResource];
    FrameGraphPreviewRequest request;request.resource=m_selectedResource;request.resourceKey=FrameGraphPreviewResourceKey(*m_snapshot,m_selectedResource);
    request.version=m_selectedVersion;request.mip=static_cast<uint16_t>(std::clamp(m_previewMip,0,int(resource.mips)-1));
    request.layer=static_cast<uint16_t>(std::clamp(m_previewLayer,0,int(resource.layers)-1));request.channel=static_cast<FrameGraphPreviewChannel>(m_previewChannel);
    request.exposure=m_previewExposure;request.rangeMin=m_previewMin;request.rangeMax=m_previewMax;request.toneMap=m_previewToneMap;
    m_previewRequest=request;m_previewStatus="Queued for the next matching product plan";
}
void OvEditor::Panels::FrameGraph::SelectPass(size_t index){m_selectedPass=index;m_flowSelectionIsResource=false;m_requestedTab=0;m_filter[0]=0;}
void OvEditor::Panels::FrameGraph::SelectResource(size_t index){m_selectedResource=index;m_selectedVersion=m_snapshot->resources[index].finalVersion;m_flowSelectionIsResource=true;m_requestedTab=1;m_filter[0]=0;
    if(m_snapshot->resources[index].origin!=ResourceOrigin::Transient)m_transientOnly=false;}

const FrameGraphGpuTimings* OvEditor::Panels::FrameGraph::MatchingGpuTimings() const
{
    return m_gpuTimings&&m_snapshot&&!m_snapshot->passes.empty()&&m_gpuTimings->graph==m_snapshot&&m_gpuTimings->passNanoseconds.size()==m_snapshot->passes.size()?m_gpuTimings.get():nullptr;
}
bool OvEditor::Panels::FrameGraph::FreezeMeasuredPlan(size_t pass)
{
    if(!m_gpuTimings||!m_gpuTimings->graph||m_gpuTimings->graph->passes.empty()||m_gpuTimings->passNanoseconds.size()!=m_gpuTimings->graph->passes.size())return false;
    m_snapshot=m_gpuTimings->graph;m_frozen=true;m_graphFit=true;
    m_selectedPass=std::min(pass,m_snapshot->passes.size()-1);m_flowSelectionIsResource=false;
    if(m_selectedResource>=m_snapshot->resources.size())m_selectedResource=0;
    if(!m_snapshot->resources.empty())m_selectedVersion=std::min(m_selectedVersion,m_snapshot->resources[m_selectedResource].finalVersion);
    return true;
}
void OvEditor::Panels::FrameGraph::ShowGpuHeat(size_t pass)
{
    if(!FreezeMeasuredPlan(pass))return;
    m_graphGpuHeat=true;m_graphFocus=true;m_filter[0]=0;m_requestedTab=2;
}
void OvEditor::Panels::FrameGraph::DrawGpuHeatControls()
{
    const bool available=m_gpuTimings&&m_gpuTimings->graph&&!m_gpuTimings->graph->passes.empty()&&m_gpuTimings->passNanoseconds.size()==m_gpuTimings->graph->passes.size();
    ImGui::BeginDisabled(!available);
    if(ImGui::Checkbox("GPU heat",&m_graphGpuHeat)&&m_graphGpuHeat) {
        const auto& costs=m_gpuTimings->passNanoseconds;
        ShowGpuHeat(static_cast<size_t>(std::max_element(costs.begin(),costs.end())-costs.begin()));
    }
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip(available?
        "Freeze the measured plan and focus its slowest Pass. Colors scale to the most expensive Pass in the whole plan; resources keep their own colors.":"Enable Capture GPU timings in Timings first.");
    ImGui::SameLine();if(ImGui::SmallButton("GPU hotspots"))m_requestedTab=4;
    if(!m_graphGpuHeat)return;
    const auto* timings=MatchingGpuTimings();
    if(!timings) {
        ImGui::TextDisabled("This plan has no matching GPU sample.");
        ImGui::SameLine();ImGui::BeginDisabled(!available);
        if(ImGui::SmallButton("Use measured plan"))ShowGpuHeat(m_selectedPass);
        ImGui::EndDisabled();return;
    }
    const auto maximum=*std::max_element(timings->passNanoseconds.begin(),timings->passNanoseconds.end());
    ImGui::TextColored(ImVec4(.40f,.75f,.95f,1),"Low");ImGui::SameLine();
    ImGui::TextColored(ImVec4(1,.48f,.35f,1),"High");ImGui::SameLine();
    ImGui::TextDisabled("Max %.3f ms | sum %.3f ms | frame %llu",maximum/1e6,timings->totalNanoseconds/1e6,timings->frameID);
}
void OvEditor::Panels::FrameGraph::DrawPassGpuTiming(size_t pass)
{
    if(const auto* timings=MatchingGpuTimings()) {
        ImGui::Text("GPU %s | frame %llu",GpuCostText(*timings,pass).c_str(),timings->frameID);
        if(ImGui::SmallButton("GPU hotspots"))m_requestedTab=4;
    }
}

void OvEditor::Panels::FrameGraph::DrawContents()
{
    RequireUIThread();
    if(ImGui::Checkbox("Freeze plan",&m_frozen)&&!m_frozen)m_graphGpuHeat=false;ImGui::SameLine();
    if(m_snapshot)ImGui::TextDisabled("Snapshot %llu | scope %llu",m_snapshot->captureIndex,m_snapshot->scope);
    else ImGui::TextDisabled("Waiting for the next product frame");
    ImGui::Text("Idle %.1f MiB (%zu) | Leased/pinned %.1f MiB (%zu)",m_pool.idleBytes/MiB,m_pool.idleTargets,m_pool.leasedBytes/MiB,m_pool.leasedTargets);
    if(ImGui::CollapsingHeader("Pool controls")) {
    ImGui::Text("Budget %.0f MiB | Reserve %zu | Peak leased %.1f MiB",m_pool.idleBudget/MiB,m_pool.frameReserve,m_pool.peakLeasedBytes/MiB);
    ImGui::TextDisabled("Allocations %llu | Hits %llu | Aliases %llu | Evictions %llu",m_pool.allocations,m_pool.hits,m_pool.aliases,m_pool.evictions);
    ImGui::SetNextItemWidth(160);
    if(ImGui::InputInt("Idle budget (MiB)",&m_budgetMiB,16,64)){m_budgetMiB=std::clamp(m_budgetMiB,0,4096);m_budgetEdited=true;}
    ImGui::SameLine();if(ImGui::Button("Apply")){m_requests.budget=uint64_t(m_budgetMiB)*uint64_t(MiB);m_budgetEdited=false;}
    ImGui::SameLine();if(ImGui::Button("Trim idle"))m_requests.trimIdle=true;
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Release idle scratch only. Active frames and external pins remain protected; the budget stays unchanged.");
    ImGui::TextDisabled("Estimated GPU storage, not total VRAM. Budget 0 disables idle caching.");
    bool autoTrim=m_pool.unusedFrameLimit!=0;
    if(ImGui::Checkbox("Auto trim unused",&autoTrim))m_requests.unusedFrameLimit=autoTrim?60:0;
    ImGui::SameLine();ImGui::TextDisabled("After %llu unused graphs | Reclaimed %llu",m_pool.unusedFrameLimit,m_pool.autoEvictions);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Old sizes and unused effects expire. Current spare sets, in-flight frames and external pins stay protected.");
    }
    if(!m_snapshot)return;
    ImGui::Separator();
    ImGui::Text("%zu passes | %zu resources | %zu physical scratch slots",m_snapshot->passes.size(),m_snapshot->resources.size(),m_snapshot->physicalTransientTargets);
    ImGui::TextDisabled("Scratch: declared %.1f MiB | live peak %.1f MiB | physical %.1f MiB",
        m_snapshot->logicalTransientBytes/MiB,m_snapshot->estimatedPeakBytes/MiB,m_snapshot->physicalTransientBytes/MiB);
    ImGui::SetNextItemWidth(-1);if(ImGui::InputTextWithHint("##filter","Filter passes or resources",m_filter.data(),m_filter.size()))m_graphFit=true;
    DrawLocator();ImGui::SameLine();
    if(ImGui::Button("Copy JSON")){ImGui::SetClipboardText(BuildReport().c_str());m_exportStatus="Full plan and live pool copied.";}
    ImGui::SameLine();if(ImGui::Button("Save JSON...")) {
        OvWindowing::Dialogs::SaveFileDialog dialog("Export Frame Graph diagnostics");
        dialog.SetInitialFilename("framegraph.json");dialog.DefineExtension("JSON diagnostics",".json");
        using enum OvWindowing::Dialogs::EExplorerFlags;
        dialog.Show(OVERWRITEPROMPT|PATHMUSTEXIST|NOCHANGEDIR|DONTADDTORECENT);
        if(dialog.HasSucceeded()) {
            std::string error;m_exportStatus=SaveReport(dialog.GetSelectedFilePath(),error)?"Saved full plan and live pool.":error;
        }
    }
    if(!m_exportStatus.empty())ImGui::TextWrapped("%s",m_exportStatus.c_str());
    if(ImGui::BeginTabBar("graph-tabs")) {
        const int request=m_requestedTab;m_requestedTab=-1;
        if(ImGui::BeginTabItem("Dependencies",nullptr,request==2?ImGuiTabItemFlags_SetSelected:0)){DrawDependencies();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Passes",nullptr,request==0?ImGuiTabItemFlags_SetSelected:0)){DrawPasses();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Resources",nullptr,request==1?ImGuiTabItemFlags_SetSelected:0)){DrawResources();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Lifetimes",nullptr,request==5?ImGuiTabItemFlags_SetSelected:0)){DrawLifetimes();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Memory",nullptr,request==6?ImGuiTabItemFlags_SetSelected:0)){DrawMemory();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Preview",nullptr,request==3?ImGuiTabItemFlags_SetSelected:0)){DrawPreview();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Timings",nullptr,request==4?ImGuiTabItemFlags_SetSelected:0)){DrawTimings();ImGui::EndTabItem();}
        ImGui::EndTabBar();
    }
}
void OvEditor::Panels::FrameGraph::DrawLocator()
{
    if(ImGui::Button("Locate..."))ImGui::OpenPopup("graph-locator");
    ImGui::SetNextWindowSize({480,350},ImGuiCond_Appearing);
    if(!ImGui::BeginPopup("graph-locator"))return;
    ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##locate","Find a Pass or resource by name or P#/R#",m_locator.data(),m_locator.size());
    struct Match {bool resource;size_t index;std::string label;};std::vector<Match> results;
    for(size_t i=0;i<m_snapshot->passes.size();++i) {
        auto label=std::format("P{}  {}",i,m_snapshot->passes[i].name);if(Matches(label,m_locator.data()))results.push_back({false,i,std::move(label)});
    }
    for(size_t i=0;i<m_snapshot->resources.size();++i) {
        auto label=std::format("R{}  {}",i,m_snapshot->resources[i].name);if(Matches(label,m_locator.data()))results.push_back({true,i,std::move(label)});
    }
    ImGui::TextDisabled("%zu matches | select to focus its graph",results.size());
    ImGui::BeginChild("locator-results",{0,0});
    ImGuiListClipper clipper;clipper.Begin(static_cast<int>(results.size()));
    while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
        const auto& item=results[row];
        if(ImGui::Selectable(item.label.c_str())) {
            m_flowSelectionIsResource=item.resource;
            if(item.resource){m_selectedResource=item.index;m_selectedVersion=m_snapshot->resources[item.index].finalVersion;m_graphResourceFlow=true;}
            else m_selectedPass=item.index;
            m_graphFocus=true;m_graphFit=true;m_requestedTab=2;m_filter[0]=0;ImGui::CloseCurrentPopup();
        }
    }
    if(results.empty())ImGui::TextDisabled("No matches");ImGui::EndChild();ImGui::EndPopup();
}

void OvEditor::Panels::FrameGraph::DrawPasses()
{
    const auto& graph=*m_snapshot;
    if(graph.passes.empty()){ImGui::TextDisabled("No passes");return;}
    const float height=std::max(120.f,ImGui::GetContentRegionAvail().y);
    ImGui::BeginChild("pass-list",{std::clamp(ImGui::GetContentRegionAvail().x*.38f,180.f,300.f),height},ImGuiChildFlags_Borders);
    std::vector<size_t> visible;for(size_t i=0;i<graph.passes.size();++i) {
        const auto& pass=graph.passes[i];bool match=Matches(pass.name,m_filter.data());
        for(const auto& access:pass.accesses)match|=Matches(graph.resources[access.resource.index].name,m_filter.data());
        if(match)visible.push_back(i);
    }
    if(!visible.empty()&&std::find(visible.begin(),visible.end(),m_selectedPass)==visible.end())m_selectedPass=visible.front();
    ImGuiListClipper clipper;clipper.Begin(static_cast<int>(visible.size()));
    while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
        const auto index=visible[row];ImGui::PushID(static_cast<int>(index));
        if(ImGui::Selectable(std::format("P{}  {}",index,graph.passes[index].name).c_str(),m_selectedPass==index))m_selectedPass=index;
        ImGui::PopID();
    }
    if(visible.empty())ImGui::TextDisabled("No matches");ImGui::EndChild();ImGui::SameLine();
    ImGui::BeginChild("pass-detail",{0,height},ImGuiChildFlags_Borders);
    if(visible.empty()){ImGui::TextDisabled("No passes match this filter");ImGui::EndChild();return;}
    const auto selectedPass=m_selectedPass;
    const auto& pass=graph.passes[selectedPass];ImGui::TextWrapped("P%zu  %s",selectedPass,pass.name.c_str());
    if(MatchingGpuTimings()) {
        DrawPassGpuTiming(selectedPass);
        ImGui::SameLine();if(ImGui::SmallButton("Show GPU heat"))ShowGpuHeat(selectedPass);
    }
    if(ImGui::SmallButton("Show dependencies")){m_requestedTab=2;m_graphFocus=true;m_graphFit=true;}
    ImGui::SameLine();if(ImGui::SmallButton("Show lifetimes")){m_requestedTab=5;m_lifetimeAtPass=true;m_lifetimeTransientOnly=false;m_filter[0]=0;}
    ImGui::TextDisabled("View %llu | %zu accesses",pass.viewId,pass.accesses.size());
    for(size_t i=0;i<pass.accesses.size();++i) {
        const auto& access=pass.accesses[i];const auto& resource=graph.resources[access.resource.index];ImGui::PushID(static_cast<int>(i));
        if(ImGui::Selectable(std::format("{}: {}",Access(access.type),resource.name).c_str()))SelectResource(access.resource.index);
        ImGui::TextDisabled("  mip %u +%u, layer %u +%u",access.range.firstMip,access.range.mipCount,access.range.firstLayer,access.range.layerCount);
        if(access.type==ResourceAccessType::Attachment)ImGui::TextDisabled("  %s / %s",access.load==AttachmentLoad::Clear?"Clear":access.load==AttachmentLoad::Load?"Load":"DontCare",access.store==AttachmentStore::Store?"Store":"Discard");
        ImGui::TextDisabled("  %s",VersionText(pass.contentVersions[i]).c_str());
        ImGui::PopID();
    }
    ImGui::SeparatorText("Dependencies");bool any=false;
    for(size_t i=0;i<graph.dependencies.size();++i) {
        const auto& edge=graph.dependencies[i];if(edge.before!=selectedPass&&edge.after!=selectedPass)continue;any=true;
        const auto other=edge.before==selectedPass?edge.after:edge.before;ImGui::PushID(static_cast<int>(i));
        if(ImGui::Selectable(std::format("{} P{}: {}",edge.before==selectedPass?"To":"From",other,graph.passes[other].name).c_str()))SelectPass(other);
        ImGui::TextDisabled("  %s",Dependency(edge.kind));
        if(edge.resource<graph.resources.size())ImGui::TextWrapped("  %s",graph.resources[edge.resource].name.c_str());ImGui::PopID();
    }
    if(!any)ImGui::TextDisabled("No resource/order edges");ImGui::EndChild();
}

void OvEditor::Panels::FrameGraph::DrawDependencies()
{
    DrawGpuHeatControls();
    if(ImGui::Checkbox("Expand canvas",&m_graphExpanded))m_graphFit=true;
    const auto& graph=*m_snapshot;m_graphNodes.clear();m_graphEdges.clear();
    const auto* timings=m_graphGpuHeat?MatchingGpuTimings():nullptr;
    const auto maximum=timings?*std::max_element(timings->passNanoseconds.begin(),timings->passNanoseconds.end()):0;
    if(graph.passes.empty()){ImGui::TextDisabled("No passes");return;}
    if(ImGui::Checkbox("Resource flow",&m_graphResourceFlow))m_graphFit=true;
    ImGui::SameLine();if(m_graphResourceFlow){DrawResourceFlow();return;}
    if(ImGui::Checkbox("Focus selected pass",&m_graphFocus))m_graphFit=true;
    ImGui::SameLine();if(ImGui::SmallButton("Fit"))m_graphFit=true;
    ImGui::SameLine();ImGui::TextDisabled("%.0f%% | Wheel: zoom | Middle drag: pan",m_graphZoom*100);
    const char* labels[]={"RAW","WAR","WAW","Order"};
    for(size_t i=0;i<4;++i) {
        if(i)ImGui::SameLine();ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(EdgeColor(static_cast<DependencyKind>(i))),"%s",labels[i]);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",Dependency(static_cast<DependencyKind>(i)));
    }
    std::vector<bool> matches(graph.passes.size(),false),visible(graph.passes.size(),false);
    const bool filtered=m_filter[0]!=0;
    for(size_t p=0;p<graph.passes.size();++p) {
        if(filtered) {
            matches[p]=Matches(graph.passes[p].name,m_filter.data());
            for(const auto& access:graph.passes[p].accesses)matches[p]=matches[p]||Matches(graph.resources[access.resource.index].name,m_filter.data());
        } else matches[p]=!m_graphFocus||p==m_selectedPass;
    }
    if(filtered&&std::any_of(matches.begin(),matches.end(),[](bool value){return value;})&&!matches[m_selectedPass]) {
        m_selectedPass=static_cast<size_t>(std::find(matches.begin(),matches.end(),true)-matches.begin());m_graphFit=true;
    }
    visible=matches;
    // Keep one-hop context when filtering/focusing so the arrows remain useful.
    for(const auto& edge:graph.dependencies)if(matches[edge.before]||matches[edge.after])visible[edge.before]=visible[edge.after]=true;
    std::vector<size_t> levels(graph.passes.size()),rows(graph.passes.size()),groups(graph.passes.size());
    std::vector<uint64_t> views;size_t maxLevel=0,nodeCount=0,edgeCount=0;
    for(const auto& edge:graph.dependencies)if(visible[edge.before]&&visible[edge.after]) {
        levels[edge.after]=std::max(levels[edge.after],levels[edge.before]+1);++edgeCount;
    }
    for(size_t p=0;p<visible.size();++p)if(visible[p]) {
        const auto view=graph.passes[p].viewId;auto found=std::find(views.begin(),views.end(),view);
        if(found==views.end()){groups[p]=views.size();views.push_back(view);}else groups[p]=found-views.begin();
        maxLevel=std::max(maxLevel,levels[p]);++nodeCount;
    }
    std::vector<std::vector<size_t>> cells(views.size(),std::vector<size_t>(maxLevel+1));
    std::vector<size_t> groupHeights(views.size()),groupStart(views.size());size_t totalRows=0;
    for(size_t p=0;p<visible.size();++p)if(visible[p]) {
        rows[p]=cells[groups[p]][levels[p]]++;groupHeights[groups[p]]=std::max(groupHeights[groups[p]],rows[p]+1);
    }
    for(size_t g=0;g<views.size();++g){groupStart[g]=totalRows;totalRows+=groupHeights[g]+1;}
    ImGui::TextDisabled("%zu / %zu passes | %zu / %zu edges | Click a node or resource edge",nodeCount,graph.passes.size(),edgeCount,graph.dependencies.size());
    const float height=std::max(140.f,ImGui::GetContentRegionAvail().y-(m_graphExpanded?0:125));
    ImGui::BeginChild("dependency-canvas",{0,height},ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    const auto origin=ImGui::GetCursorScreenPos();const auto size=ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("canvas",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const bool hovered=ImGui::IsItemHovered();auto& io=ImGui::GetIO();auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(20,24,31,255));
    if(nodeCount) {
        constexpr float nodeWidth=178,nodeHeight=58,columnStep=232,rowStep=84;
        const float width=maxLevel*columnStep+nodeWidth,extentHeight=(totalRows-2)*rowStep+nodeHeight;
        if(m_graphFit) {
            m_graphZoom=std::clamp(std::min((size.x-32)/width,(size.y-32)/extentHeight),.05f,1.5f);
            m_graphPan={(size.x-width*m_graphZoom)*.5f,(size.y-extentHeight*m_graphZoom)*.5f};m_graphFit=false;
        }
        if(hovered&&io.MouseWheel!=0) {
            const float zoom=std::clamp(m_graphZoom*std::pow(1.18f,io.MouseWheel),.05f,2.5f);
            const float ratio=zoom/m_graphZoom;
            m_graphPan[0]=io.MousePos.x-origin.x-(io.MousePos.x-origin.x-m_graphPan[0])*ratio;
            m_graphPan[1]=io.MousePos.y-origin.y-(io.MousePos.y-origin.y-m_graphPan[1])*ratio;m_graphZoom=zoom;
        }
        if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Middle,0)) {
            m_graphPan[0]+=io.MouseDelta.x;m_graphPan[1]+=io.MouseDelta.y;
        }
        const float grid=32*m_graphZoom;
        if(grid>=8) {
            for(float x=std::fmod(m_graphPan[0],grid);x<size.x;x+=grid)draw->AddLine({origin.x+x,origin.y},{origin.x+x,origin.y+size.y},IM_COL32(255,255,255,10));
            for(float y=std::fmod(m_graphPan[1],grid);y<size.y;y+=grid)draw->AddLine({origin.x,origin.y+y},{origin.x+size.x,origin.y+y},IM_COL32(255,255,255,10));
        }
        std::vector<ImVec2> positions(graph.passes.size());
        size_t hoveredPass=size_t(-1),hoveredEdge=size_t(-1);float edgeDistance=6;
        for(size_t p=0;p<visible.size();++p)if(visible[p]) {
            const ImVec2 pos={origin.x+m_graphPan[0]+levels[p]*columnStep*m_graphZoom,
                origin.y+m_graphPan[1]+(groupStart[groups[p]]+rows[p])*rowStep*m_graphZoom};positions[p]=pos;
            const std::array<float,4> rect={pos.x,pos.y,pos.x+nodeWidth*m_graphZoom,pos.y+nodeHeight*m_graphZoom};
            m_graphNodes.push_back({false,p,rect});
            if(hovered&&io.MousePos.x>=rect[0]&&io.MousePos.x<=rect[2]&&io.MousePos.y>=rect[1]&&io.MousePos.y<=rect[3])hoveredPass=p;
        }
        for(size_t e=0;e<graph.dependencies.size();++e) {
            const auto& edge=graph.dependencies[e];if(!visible[edge.before]||!visible[edge.after])continue;
            const auto from=positions[edge.before],to=positions[edge.after];
            const ImVec2 a={from.x+nodeWidth*m_graphZoom,from.y+nodeHeight*m_graphZoom*.5f},d={to.x,to.y+nodeHeight*m_graphZoom*.5f};
            const float bend=std::max(20.f,(d.x-a.x)*.5f);const ImVec2 b={a.x+bend,a.y},c={d.x-bend,d.y};
            const bool selected=edge.before==m_selectedPass||edge.after==m_selectedPass;
            const auto color=EdgeColor(edge.kind,selected?1.f:.45f);draw->AddBezierCubic(a,b,c,d,color,selected?2.3f:1.2f);
            const float arrow=std::clamp(7*m_graphZoom,3.f,9.f);draw->AddTriangleFilled(d,{d.x-arrow,d.y-arrow*.6f},{d.x-arrow,d.y+arrow*.6f},color);
            if(hovered&&hoveredPass==size_t(-1)) {
                auto prev=a;for(int step=1;step<=24;++step) {
                    const auto point=Curve(a,b,c,d,step/24.f);const auto distance=SegmentDistance(io.MousePos,prev,point);
                    if(distance<edgeDistance){hoveredEdge=e;edgeDistance=distance;}prev=point;
                }
            }
        }
        for(const auto& node:m_graphNodes) {
            if(node.resource)continue;
            const auto p=node.index;const auto& rect=node.rect;const bool selected=p==m_selectedPass;
            const ImVec2 min={rect[0],rect[1]},max={rect[2],rect[3]};
            draw->AddRectFilled(min,max,timings?GpuHeatColor(timings->passNanoseconds[p],maximum):selected?IM_COL32(40,76,105,255):IM_COL32(40,46,58,255),5.f*m_graphZoom);
            draw->AddRect(min,max,selected?IM_COL32(105,202,255,255):p==hoveredPass?IM_COL32(190,200,215,255):IM_COL32(85,96,112,255),5.f*m_graphZoom,ImDrawFlags_None,selected?2.f:1.f);
            const float fontSize=ImGui::GetFontSize()*m_graphZoom;const ImVec4 clip={min.x+5*m_graphZoom,min.y,max.x-5*m_graphZoom,max.y};
            draw->AddText(ImGui::GetFont(),fontSize,{min.x+8*m_graphZoom,min.y+7*m_graphZoom},IM_COL32(230,235,245,255),std::format("P{}  {}",p,graph.passes[p].name).c_str(),nullptr,0,&clip);
            const auto subtitle=timings?GpuCostText(*timings,p):std::format("View {} | {} accesses",graph.passes[p].viewId,graph.passes[p].accesses.size());
            draw->AddText(ImGui::GetFont(),fontSize*.85f,{min.x+8*m_graphZoom,min.y+32*m_graphZoom},timings?IM_COL32(240,235,222,255):IM_COL32(158,174,194,255),subtitle.c_str(),nullptr,0,&clip);
        }
        if(hoveredPass!=size_t(-1)) {
            ImGui::BeginTooltip();ImGui::Text("P%zu  %s",hoveredPass,graph.passes[hoveredPass].name.c_str());
            if(timings)ImGui::Text("GPU %s | frame %llu",GpuCostText(*timings,hoveredPass).c_str(),timings->frameID);
            ImGui::TextDisabled("Click to select; double-click for pass details");ImGui::EndTooltip();
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_selectedPass=hoveredPass;if(m_graphFocus)m_graphFit=true;
                if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))SelectPass(hoveredPass);
            }
        } else if(hoveredEdge!=size_t(-1)) {
            const auto& edge=graph.dependencies[hoveredEdge];
            const char* resource=edge.resource<graph.resources.size()?graph.resources[edge.resource].name.c_str():"No resource (explicit ordering)";
            ImGui::SetTooltip("P%zu -> P%zu\n%s\n%s",edge.before,edge.after,Dependency(edge.kind),resource);
            if(edge.resource<graph.resources.size()&&ImGui::IsMouseClicked(ImGuiMouseButton_Left))SelectResource(edge.resource);
        }
    } else draw->AddText({origin.x+16,origin.y+16},ImGui::GetColorU32(ImGuiCol_TextDisabled),"No passes match this filter");
    ImGui::EndChild();
    if(m_graphExpanded)return;
    ImGui::BeginChild("dependency-detail",{0,0});
    if(filtered&&std::none_of(matches.begin(),matches.end(),[](bool value){return value;})) {
        ImGui::TextDisabled("No pass or resource matches this filter");ImGui::EndChild();return;
    }
    const auto& pass=graph.passes[m_selectedPass];ImGui::TextWrapped("Selected: P%zu  %s",m_selectedPass,pass.name.c_str());
    DrawPassGpuTiming(m_selectedPass);
    if(ImGui::SmallButton("Pass details"))SelectPass(m_selectedPass);
    for(size_t a=0;a<pass.accesses.size();++a) {
        const auto& access=pass.accesses[a];ImGui::PushID(static_cast<int>(a));
        if(ImGui::Selectable(std::format("{}: {}",Access(access.type),graph.resources[access.resource.index].name).c_str()))SelectResource(access.resource.index);ImGui::PopID();
    }
    ImGui::EndChild();
}
void OvEditor::Panels::FrameGraph::DrawResourceFlow()
{
    const auto& graph=*m_snapshot;
    const auto* timings=m_graphGpuHeat?MatchingGpuTimings():nullptr;
    const auto maximum=timings?*std::max_element(timings->passNanoseconds.begin(),timings->passNanoseconds.end()):0;
    if(ImGui::Checkbox("Versions",&m_graphVersions))m_graphFit=true;
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Each writing pass advances content once. Version nodes share storage; they are not texture copies.");
    ImGui::SameLine();
    if(ImGui::Checkbox("Focus selection",&m_graphFocus))m_graphFit=true;
    ImGui::SameLine();if(ImGui::SmallButton("Fit"))m_graphFit=true;
    ImGui::SameLine();ImGui::TextDisabled("%.0f%% | Wheel: zoom | Middle drag: pan",m_graphZoom*100);
    const ImU32 readColor=IM_COL32(85,190,255,255),writeColor=IM_COL32(255,168,75,255),rwColor=IM_COL32(199,132,255,255);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(readColor),"Resource to Pass  read");ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(writeColor),"Pass to Resource  write");ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(rwColor),"Load write");

    const bool filtered=m_filter[0]!=0;
    std::vector<bool> matches(graph.passes.size(),false),visiblePass(graph.passes.size(),false),visibleResource(graph.resources.size(),false);
    for(size_t p=0;p<graph.passes.size();++p) {
        if(filtered) {
            matches[p]=Matches(graph.passes[p].name,m_filter.data());
            for(const auto& access:graph.passes[p].accesses)matches[p]=matches[p]||Matches(graph.resources[access.resource.index].name,m_filter.data());
        } else if(m_flowSelectionIsResource) {
            matches[p]=std::any_of(graph.passes[p].accesses.begin(),graph.passes[p].accesses.end(),[&](const auto& a){return a.resource.index==m_selectedResource;});
        } else matches[p]=!m_graphFocus||p==m_selectedPass;
    }
    visiblePass=matches;
    if(!m_graphFocus&&!filtered)std::fill(visiblePass.begin(),visiblePass.end(),true);
    if(m_graphFocus&&!filtered&&m_flowSelectionIsResource&&m_selectedResource<graph.resources.size())visibleResource[m_selectedResource]=true;
    if(m_graphFocus&&!filtered&&!m_flowSelectionIsResource) {
        std::vector<bool> selectedResources(graph.resources.size(),false);
        for(size_t p=0;p<matches.size();++p)if(matches[p])for(const auto& access:graph.passes[p].accesses)selectedResources[access.resource.index]=true;
        for(size_t p=0;p<graph.passes.size();++p)if(!matches[p]&&std::any_of(graph.passes[p].accesses.begin(),graph.passes[p].accesses.end(),[&](const auto& access){return selectedResources[access.resource.index];}))
            visiblePass[p]=true;
    }
    if(m_graphFocus||filtered) {
        const auto contextPass=visiblePass;
        for(const auto& edge:graph.dependencies)if(contextPass[edge.before]||contextPass[edge.after])visiblePass[edge.before]=visiblePass[edge.after]=true;
    }
    const auto addEdge=[&](size_t p,size_t r,uint32_t version,bool read,bool write,bool load) {
        auto edge=std::find_if(m_graphEdges.begin(),m_graphEdges.end(),[&](const auto& e){return e.pass==p&&e.resource==r&&e.version==version;});
        if(edge==m_graphEdges.end())m_graphEdges.push_back({p,r,read,write,version,load});
        else {edge->read|=read;edge->write|=write;edge->load|=load;}
    };
    for(size_t p=0;p<graph.passes.size();++p)if(visiblePass[p])for(size_t a=0;a<graph.passes[p].accesses.size();++a) {
        const auto& access=graph.passes[p].accesses[a];const auto& version=graph.passes[p].contentVersions[a];
        const size_t r=access.resource.index;visibleResource[r]=true;
        const bool load=access.type==ResourceAccessType::Attachment&&access.load==AttachmentLoad::Load;
        if(m_graphVersions) {
            if(version.read)addEdge(p,r,*version.read,true,false,false);
            if(version.write)addEdge(p,r,*version.write,false,true,load);
        } else addEdge(p,r,0,version.read.has_value(),version.write.has_value(),load);
    }
    if(!filtered&&!m_graphFocus)std::fill(visibleResource.begin(),visibleResource.end(),true);
    if(filtered)for(size_t r=0;r<graph.resources.size();++r)if(Matches(graph.resources[r].name,m_filter.data()))visibleResource[r]=true;
    if(filtered&&std::any_of(matches.begin(),matches.end(),[](bool value){return value;})&&!matches[m_selectedPass]) {
        m_selectedPass=static_cast<size_t>(std::find(matches.begin(),matches.end(),true)-matches.begin());m_flowSelectionIsResource=false;m_graphFit=true;
    }

    using ResourceNode=std::pair<size_t,uint32_t>;
    std::vector<size_t> passOrder;std::vector<ResourceNode> resourceOrder;std::vector<size_t> resourceFirst(graph.resources.size(),size_t(-1));
    for(size_t p=0;p<visiblePass.size();++p)if(visiblePass[p]) {
        passOrder.push_back(p);
        for(const auto& access:graph.passes[p].accesses)resourceFirst[access.resource.index]=std::min(resourceFirst[access.resource.index],p);
    }
    for(const auto& edge:m_graphEdges)resourceOrder.emplace_back(edge.resource,edge.version);
    for(size_t r=0;r<visibleResource.size();++r)if(visibleResource[r]&&std::none_of(resourceOrder.begin(),resourceOrder.end(),[&](const auto& node){return node.first==r;}))
        resourceOrder.emplace_back(r,m_graphVersions?graph.resources[r].finalVersion:0);
    std::sort(resourceOrder.begin(),resourceOrder.end());resourceOrder.erase(std::unique(resourceOrder.begin(),resourceOrder.end()),resourceOrder.end());
    std::stable_sort(resourceOrder.begin(),resourceOrder.end(),[&](const auto& a,const auto& b){
        if(resourceFirst[a.first]!=resourceFirst[b.first])return resourceFirst[a.first]<resourceFirst[b.first];return a<b;
    });
    if(filtered&&passOrder.empty()&&!resourceOrder.empty()) {
        m_selectedResource=resourceOrder.front().first;m_selectedVersion=resourceOrder.front().second;m_flowSelectionIsResource=true;
    }
    if(m_flowSelectionIsResource&&m_graphVersions&&std::find(resourceOrder.begin(),resourceOrder.end(),ResourceNode{m_selectedResource,m_selectedVersion})==resourceOrder.end()) {
        const auto current=std::find_if(resourceOrder.rbegin(),resourceOrder.rend(),[&](const auto& node){return node.first==m_selectedResource;});
        if(current!=resourceOrder.rend())m_selectedVersion=current->second;
    }
    const size_t edgeCount=static_cast<size_t>(std::count_if(m_graphEdges.begin(),m_graphEdges.end(),[&](const auto& e){return visiblePass[e.pass]&&visibleResource[e.resource];}));
    ImGui::TextDisabled("%zu passes + %zu %s | %zu links | plan: %zu passes / %zu resources",passOrder.size(),resourceOrder.size(),m_graphVersions?"versions":"resources",edgeCount,graph.passes.size(),graph.resources.size());
    const float canvasHeight=std::max(150.f,ImGui::GetContentRegionAvail().y-(m_graphExpanded?0:88));
    if(passOrder.empty()&&resourceOrder.empty()) {
        ImGui::BeginChild("resource-flow-canvas",{0,canvasHeight},ImGuiChildFlags_Borders);
        ImGui::TextDisabled("No passes or resources match this filter");ImGui::EndChild();
        if(!m_graphExpanded){ImGui::BeginChild("resource-flow-detail",{0,0});ImGui::TextDisabled("No matching nodes");ImGui::EndChild();}return;
    }
    ImGui::BeginChild("resource-flow-canvas",{0,canvasHeight},ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    const auto origin=ImGui::GetCursorScreenPos();const auto size=ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("resource-flow",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const bool hovered=ImGui::IsItemHovered();auto& io=ImGui::GetIO();auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(20,24,31,255));
    constexpr float nodeWidth=184,nodeHeight=48,rowStep=58,padding=18,columnGap=12,centerGap=48;
    const auto rowCount=[](size_t count,size_t columnCount){return (count+columnCount-1)/columnCount;};
    size_t passColumns=1,resourceColumns=1;float bestScale=-1;
    // Maximize readable node size for the actual canvas aspect ratio. A tall
    // single column can be smaller than two columns even for a short chain.
    for(size_t pc=1;pc<=std::min<size_t>(3,std::max<size_t>(1,passOrder.size()));++pc)
        for(size_t rc=1;rc<=std::min<size_t>(3,std::max<size_t>(1,resourceOrder.size()));++rc) {
            const float width=(pc+rc)*nodeWidth+(pc+rc-2)*columnGap+centerGap+2*padding;
            const float height=std::max(1.f,std::max(rowCount(passOrder.size(),pc),rowCount(resourceOrder.size(),rc))*rowStep);
            const float scale=std::min((size.x-2*padding)/width,(size.y-2*padding)/height);
            if(scale>bestScale){bestScale=scale;passColumns=pc;resourceColumns=rc;}
        }
    const size_t rows=std::max(rowCount(passOrder.size(),passColumns),rowCount(resourceOrder.size(),resourceColumns));
    const float extentHeight=std::max(1.f,rows*rowStep);
    const float passLaneWidth=passColumns*nodeWidth+(passColumns-1)*columnGap;
    const float resourceLaneWidth=resourceColumns*nodeWidth+(resourceColumns-1)*columnGap;
    const float extentWidth=passLaneWidth+centerGap+resourceLaneWidth+2*padding;
    if(m_graphFit) {
        m_graphZoom=std::clamp(std::min((size.x-2*padding)/extentWidth,(size.y-2*padding)/extentHeight),.05f,1.5f);
        m_graphPan={(size.x-extentWidth*m_graphZoom)*.5f,(size.y-extentHeight*m_graphZoom)*.5f};m_graphFit=false;
    }
    if(hovered&&io.MouseWheel!=0) {
        const float zoom=std::clamp(m_graphZoom*std::pow(1.18f,io.MouseWheel),.05f,2.5f),ratio=zoom/m_graphZoom;
        m_graphPan[0]=io.MousePos.x-origin.x-(io.MousePos.x-origin.x-m_graphPan[0])*ratio;
        m_graphPan[1]=io.MousePos.y-origin.y-(io.MousePos.y-origin.y-m_graphPan[1])*ratio;m_graphZoom=zoom;
    }
    if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Middle,0)){m_graphPan[0]+=io.MouseDelta.x;m_graphPan[1]+=io.MouseDelta.y;}
    const float scaledNodeStep=(nodeWidth+columnGap)*m_graphZoom;
    const auto passLaneX=origin.x+padding*m_graphZoom+m_graphPan[0];
    const auto resourceLaneX=passLaneX+(passLaneWidth+centerGap)*m_graphZoom;
    const auto yFor=[&](size_t row){return origin.y+m_graphPan[1]+row*rowStep*m_graphZoom;};
    std::vector<float> passY(graph.passes.size()),passX(graph.passes.size());std::map<ResourceNode,ImVec2> resourcePositions;
    for(size_t row=0;row<passOrder.size();++row){passY[passOrder[row]]=yFor(row/passColumns);passX[passOrder[row]]=passLaneX+(row%passColumns)*scaledNodeStep;}
    for(size_t row=0;row<resourceOrder.size();++row)resourcePositions[resourceOrder[row]]={resourceLaneX+(row%resourceColumns)*scaledNodeStep,yFor(row/resourceColumns)};
    const float fontSize=ImGui::GetFontSize()*m_graphZoom;const auto clip=ImVec4(origin.x,origin.y,origin.x+size.x,origin.y+size.y);
    size_t hoveredNode=size_t(-1);const GraphEdge* hoveredEdge=nullptr;
    float edgeDistance=6;
    for(const auto& edge:m_graphEdges)if(visiblePass[edge.pass]&&visibleResource[edge.resource]) {
        const auto position=resourcePositions.at({edge.resource,edge.version});
        const auto drawFlowEdge=[&](bool read,float offset) {
            const float py=passY[edge.pass]+nodeHeight*m_graphZoom*.5f+offset,ry=position.y+nodeHeight*m_graphZoom*.5f+offset;
            const bool forward=read;const auto color=read?readColor:edge.load?rwColor:writeColor;
            const ImVec2 a=forward?ImVec2(position.x,ry):ImVec2(passX[edge.pass]+nodeWidth*m_graphZoom,py);
            const ImVec2 d=forward?ImVec2(passX[edge.pass]+nodeWidth*m_graphZoom,py):ImVec2(position.x,ry);
            const float bend=(d.x-a.x)*.5f;const ImVec2 b={a.x+bend,a.y},c={d.x-bend,d.y};
            const bool selected=m_flowSelectionIsResource?edge.resource==m_selectedResource&&(!m_graphVersions||edge.version==m_selectedVersion):edge.pass==m_selectedPass;
            draw->AddBezierCubic(a,b,c,d,color,selected?2.f:1.2f);
            const float arrow=std::clamp(7.f*m_graphZoom,3.f,9.f);const float sign=forward?1.f:-1.f;
            draw->AddTriangleFilled(d,{d.x+sign*arrow,d.y-arrow*.6f},{d.x+sign*arrow,d.y+arrow*.6f},color);
            if(hovered&&hoveredNode==size_t(-1)) {
                auto previous=a;for(int step=1;step<=20;++step){const auto point=Curve(a,b,c,d,step/20.f);const float distance=SegmentDistance(io.MousePos,previous,point);
                    if(distance<edgeDistance){edgeDistance=distance;hoveredEdge=&edge;}previous=point;}
            }
        };
        if(edge.write)drawFlowEdge(false,edge.read?-3.f*m_graphZoom:0);
        if(edge.read)drawFlowEdge(true,edge.write?3.f*m_graphZoom:0);
    }
    const auto drawNode=[&](bool resource,size_t index,uint32_t version,const std::array<float,4>& rect,bool selected,const char* title,const char* subtitle) {
        const ImVec2 min={rect[0],rect[1]},max={rect[2],rect[3]};
        const bool underMouse=hovered&&io.MousePos.x>=rect[0]&&io.MousePos.x<=rect[2]&&io.MousePos.y>=rect[1]&&io.MousePos.y<=rect[3];
        const auto fill=!resource&&timings?GpuHeatColor(timings->passNanoseconds[index],maximum):selected?(resource?IM_COL32(78,67,105,255):IM_COL32(40,76,105,255)):resource?IM_COL32(53,46,67,255):IM_COL32(40,46,58,255);
        draw->AddRectFilled(min,max,fill,5.f*m_graphZoom);
        draw->AddRect(min,max,selected?IM_COL32(173,157,255,255):underMouse?IM_COL32(190,200,215,255):resource?IM_COL32(118,100,151,255):IM_COL32(85,96,112,255),5.f*m_graphZoom,ImDrawFlags_None,selected?2.f:1.f);
        const ImVec4 nodeClip={std::max(clip.x,min.x),std::max(clip.y,min.y),std::min(clip.z,max.x-4*m_graphZoom),std::min(clip.w,max.y)};
        draw->AddText(ImGui::GetFont(),fontSize,{min.x+6*m_graphZoom,min.y+6*m_graphZoom},IM_COL32(230,235,245,255),title,nullptr,0,&nodeClip);
        draw->AddText(ImGui::GetFont(),fontSize*.82f,{min.x+6*m_graphZoom,min.y+28*m_graphZoom},!resource&&timings?IM_COL32(240,235,222,255):IM_COL32(158,174,194,255),subtitle,nullptr,0,&nodeClip);
        m_graphNodes.push_back({resource,index,rect,version});
        if(underMouse)hoveredNode=m_graphNodes.size()-1;
    };
    for(size_t row=0;row<passOrder.size();++row) {
        const size_t p=passOrder[row];const float y=passY[p];const auto& pass=graph.passes[p];
        drawNode(false,p,0,{passX[p],y,passX[p]+nodeWidth*m_graphZoom,y+nodeHeight*m_graphZoom},!m_flowSelectionIsResource&&p==m_selectedPass,
            std::format("P{}  {}",p,pass.name).c_str(),(timings?GpuCostText(*timings,p):std::format("View {} | {} accesses",pass.viewId,pass.accesses.size())).c_str());
    }
    for(size_t row=0;row<resourceOrder.size();++row) {
        const auto [r,version]=resourceOrder[row];const auto& resource=graph.resources[r];const auto position=resourcePositions.at(resourceOrder[row]);
        const auto slot=resource.physicalSlot?std::to_string(resource.physicalSlot):resource.origin==ResourceOrigin::Transient?"unused":"external";
        const auto subtitle=std::format("{} {}x{} | slot {}",Format(resource.format),resource.width,resource.height,slot);
        const auto title=m_graphVersions?std::format("R{} v{}  {}",r,version,resource.name):std::format("R{}  {}",r,resource.name);
        drawNode(true,r,version,{position.x,position.y,position.x+nodeWidth*m_graphZoom,position.y+nodeHeight*m_graphZoom},
            m_flowSelectionIsResource&&r==m_selectedResource&&(!m_graphVersions||version==m_selectedVersion),title.c_str(),subtitle.c_str());
    }
    for(const auto& node:m_graphNodes)if(hovered&&io.MousePos.x>=node.rect[0]&&io.MousePos.x<=node.rect[2]&&io.MousePos.y>=node.rect[1]&&io.MousePos.y<=node.rect[3]) {
        if(node.resource) {
            const auto& r=graph.resources[node.index];ImGui::BeginTooltip();
            ImGui::Text("R%zu%s  %s",node.index,m_graphVersions?std::format(" v{}",node.version).c_str():"",r.name.c_str());
            ImGui::Text("%s | %ux%u | %s | slot %llu",Format(r.format),r.width,r.height,Origin(r.origin),r.physicalSlot);
            if(m_graphVersions) {
                ImGui::TextDisabled("Content state; same storage, no retained texture copy.");
                if(node.version==0)ImGui::TextDisabled(r.lifetime.first==size_t(-1)?"Unused resource; no content access":"Initial imported content (for accessed ranges)");
                for(size_t p=0;p<graph.passes.size();++p)for(size_t a=0;a<graph.passes[p].accesses.size();++a) {
                    const auto& access=graph.passes[p].accesses[a];const auto& v=graph.passes[p].contentVersions[a];
                    if(access.resource.index==node.index&&v.write==node.version)
                        ImGui::Text("Produced by P%zu %s | mip %u +%u, layer %u +%u%s",p,graph.passes[p].name.c_str(),access.range.firstMip,access.range.mipCount,access.range.firstLayer,access.range.layerCount,access.store==AttachmentStore::Discard?" (discarded range)":"");
                }
            } else ImGui::Text("Live passes %zu-%zu",r.lifetime.first,r.lifetime.last);
            ImGui::EndTooltip();
        } else {
            ImGui::BeginTooltip();ImGui::Text("P%zu  %s",node.index,graph.passes[node.index].name.c_str());
            if(timings)ImGui::Text("GPU %s | frame %llu",GpuCostText(*timings,node.index).c_str(),timings->frameID);
            ImGui::TextDisabled("Click to select; double-click for pass details");ImGui::EndTooltip();
        }
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if(node.resource){m_selectedResource=node.index;m_selectedVersion=node.version;m_flowSelectionIsResource=true;}else {m_selectedPass=node.index;m_flowSelectionIsResource=false;
                if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))SelectPass(node.index);}
            if(m_graphFocus)m_graphFit=true;
        }
    }
    if(hoveredNode==size_t(-1)&&hoveredEdge) {
        const auto& link=*hoveredEdge;const auto& pass=graph.passes[link.pass];
        ImGui::BeginTooltip();ImGui::Text("P%zu  %s | R%zu%s  %s",link.pass,pass.name.c_str(),link.resource,
            m_graphVersions?std::format(" v{}",link.version).c_str():"",graph.resources[link.resource].name.c_str());
        for(size_t a=0;a<pass.accesses.size();++a) {
            const auto& access=pass.accesses[a];const auto& v=pass.contentVersions[a];
            if(access.resource.index==link.resource&&(!m_graphVersions||v.read==link.version||v.write==link.version))
                ImGui::Text("%s | %s | mip %u +%u, layer %u +%u%s",Access(access.type),VersionText(v).c_str(),access.range.firstMip,access.range.mipCount,access.range.firstLayer,access.range.layerCount,access.store==AttachmentStore::Discard?" | discarded range":"");
        }
        ImGui::EndTooltip();
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)){m_selectedResource=link.resource;m_selectedVersion=link.version;m_flowSelectionIsResource=true;if(m_graphFocus)m_graphFit=true;}
    }
    ImGui::EndChild();
    if(m_graphExpanded)return;
    ImGui::BeginChild("resource-flow-detail",{0,0});
    if(m_flowSelectionIsResource&&m_selectedResource<graph.resources.size()) {
        const auto& selected=graph.resources[m_selectedResource];
        ImGui::TextWrapped("R%zu%s  %s | %s %ux%u | slot %llu",m_selectedResource,m_graphVersions?std::format(" v{}",m_selectedVersion).c_str():"",selected.name.c_str(),Format(selected.format),selected.width,selected.height,selected.physicalSlot);
        ImGui::SameLine();if(ImGui::SmallButton("Resource details"))SelectResource(m_selectedResource);
        ImGui::SameLine();if(ImGui::SmallButton("Preview content")){m_requestedTab=3;QueuePreview();}
        if(m_graphVersions)ImGui::TextDisabled(m_selectedVersion==0?"v0: initial content in accessed ranges (unused nodes have no content).":"Content states share storage; partial writes keep other ranges. No texture copies.");
        size_t shown=0;for(size_t p=0;p<graph.passes.size()&&shown<6;++p)for(size_t a=0;a<graph.passes[p].accesses.size()&&shown<6;++a) {
            const auto& access=graph.passes[p].accesses[a];const auto& version=graph.passes[p].contentVersions[a];
            if(access.resource.index!=m_selectedResource||(m_graphVersions&&version.read!=m_selectedVersion&&version.write!=m_selectedVersion))continue;
            ImGui::PushID(static_cast<int>(p));ImGui::PushID(static_cast<int>(a));
            const auto label=std::format("P{} {}",p,VersionText(version));ContinueButtonRow(label,shown==0);
            if(ImGui::SmallButton(label.c_str())){m_selectedPass=p;m_flowSelectionIsResource=false;if(m_graphFocus)m_graphFit=true;}
            ImGui::PopID();ImGui::PopID();++shown;
        }
    } else if(m_selectedPass<graph.passes.size()) {
        const auto& selected=graph.passes[m_selectedPass];ImGui::TextWrapped("P%zu  %s",m_selectedPass,selected.name.c_str());
        DrawPassGpuTiming(m_selectedPass);
        ImGui::SameLine();if(ImGui::SmallButton("Pass details"))SelectPass(m_selectedPass);
        for(size_t i=0;i<std::min<size_t>(selected.accesses.size(),6);++i) {
            const auto& access=selected.accesses[i];ImGui::PushID(static_cast<int>(i));
            const auto& version=selected.contentVersions[i];
            const auto label=std::format("{}  {}",VersionText(version),graph.resources[access.resource.index].name);ContinueButtonRow(label,i==0);
            if(ImGui::SmallButton(label.c_str())){m_selectedResource=access.resource.index;m_selectedVersion=version.write.value_or(version.read.value_or(0));m_flowSelectionIsResource=true;if(m_graphFocus)m_graphFit=true;}
            ImGui::PopID();
        }
    } else ImGui::TextDisabled("No matching nodes");
    ImGui::EndChild();
}
void OvEditor::Panels::FrameGraph::DrawResources()
{
    const auto& graph=*m_snapshot;ImGui::Checkbox("Transient only",&m_transientOnly);
    const float height=std::max(100.f,ImGui::GetContentRegionAvail().y*.6f);
    if(ImGui::BeginTable("resource-table",5,ImGuiTableFlags_BordersInner|ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollY,{0,height})) {
        ImGui::TableSetupColumn("Resource");ImGui::TableSetupColumn("Origin / extent");ImGui::TableSetupColumn("MiB",ImGuiTableColumnFlags_WidthFixed,55);
        ImGui::TableSetupColumn("Lifetime");ImGui::TableSetupColumn("Physical slot",ImGuiTableColumnFlags_WidthFixed,80);ImGui::TableHeadersRow();
        std::vector<size_t> visible;for(size_t i=0;i<graph.resources.size();++i)if((!m_transientOnly||graph.resources[i].origin==ResourceOrigin::Transient)&&Matches(graph.resources[i].name,m_filter.data()))visible.push_back(i);
        if(!visible.empty()&&std::find(visible.begin(),visible.end(),m_selectedResource)==visible.end())m_selectedResource=visible.front();
        if(visible.empty()){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextDisabled("No matches");}
        ImGuiListClipper clipper;clipper.Begin(static_cast<int>(visible.size()));
        while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
            const auto i=visible[row];const auto& resource=graph.resources[i];ImGui::PushID(static_cast<int>(i));ImGui::TableNextRow();ImGui::TableNextColumn();
            if(ImGui::Selectable(resource.name.c_str(),m_selectedResource==i,ImGuiSelectableFlags_SpanAllColumns)){m_selectedResource=i;m_selectedVersion=resource.finalVersion;m_flowSelectionIsResource=true;}
            ImGui::TextDisabled("R%zu | %s",i,Format(resource.format));ImGui::TableNextColumn();
            ImGui::TextUnformatted(Origin(resource.origin));ImGui::TextDisabled("%ux%u %s",resource.width,resource.height,Kind(resource.kind));ImGui::TableNextColumn();
            ImGui::Text("%.2f",resource.lifetime.estimatedBytes/MiB);ImGui::TableNextColumn();Lifetime(resource,graph.passes.size());ImGui::TableNextColumn();
            if(resource.physicalSlot)ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(SlotColor(resource.physicalSlot)),"#%llu",resource.physicalSlot);
            else ImGui::TextDisabled(resource.origin==ResourceOrigin::Transient?"Unused":"External");ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if(graph.resources.empty())return;
    if((m_transientOnly&&graph.resources[m_selectedResource].origin!=ResourceOrigin::Transient)||!Matches(graph.resources[m_selectedResource].name,m_filter.data()))return;
    const auto selectedResource=m_selectedResource;
    const auto& selected=graph.resources[selectedResource];ImGui::Separator();ImGui::TextWrapped("R%zu  %s",selectedResource,selected.name.c_str());
    ImGui::TextDisabled("%u mips | %u layers | %u samples | version %u",selected.mips,selected.layers,selected.samples,selected.finalVersion);
    ImGui::SameLine();if(ImGui::SmallButton("Show version flow")) {
        m_flowSelectionIsResource=true;m_selectedVersion=selected.finalVersion;m_graphResourceFlow=true;m_graphVersions=true;m_graphFocus=true;m_graphFit=true;m_requestedTab=2;m_filter[0]=0;
    }
    ImGui::SameLine();if(ImGui::SmallButton("Preview content")){m_requestedTab=3;QueuePreview();}
    ImGui::SameLine();if(ImGui::SmallButton("Show lifetime")){m_requestedTab=5;m_lifetimeAtPass=false;m_lifetimeTransientOnly=selected.origin==ResourceOrigin::Transient;m_filter[0]=0;}
    ImGui::BeginChild("resource-detail",{0,0});
    if(selected.physicalSlot) {
        ImGui::TextUnformatted("Same physical slot (nonoverlapping plan lifetimes):");
        for(size_t i=0;i<graph.resources.size();++i)if(i!=selectedResource&&graph.resources[i].physicalSlot==selected.physicalSlot) {
            ImGui::PushID(static_cast<int>(i));if(ImGui::Selectable(graph.resources[i].name.c_str()))m_selectedResource=i;ImGui::PopID();
        }
    }
    ImGui::TextUnformatted("Accessed by:");
    for(size_t p=0;p<graph.passes.size();++p) {
        ImGui::PushID(static_cast<int>(p));
        for(size_t a=0;a<graph.passes[p].accesses.size();++a) {
            const auto& access=graph.passes[p].accesses[a];if(access.resource.index!=selectedResource)continue;
            ImGui::PushID(static_cast<int>(a));
            if(ImGui::Selectable(std::format("P{}  {} ({}, {})",p,graph.passes[p].name,Access(access.type),VersionText(graph.passes[p].contentVersions[a])).c_str()))SelectPass(p);
            ImGui::TextDisabled("  mip %u +%u, layer %u +%u%s",access.range.firstMip,access.range.mipCount,access.range.firstLayer,access.range.layerCount,access.store==AttachmentStore::Discard?" | discarded range":"");
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void OvEditor::Panels::FrameGraph::DrawLifetimes()
{
    const auto& graph=*m_snapshot;const auto count=graph.passes.size();m_lifetimeBars.clear();
    m_lifetimeLiveBytes=ScratchProfile(graph);
    if(!count){m_lifetimeProfileRect={};ImGui::TextDisabled("No passes in this plan");return;}
    m_lifetimePeakPass=static_cast<size_t>(std::max_element(m_lifetimeLiveBytes.begin(),m_lifetimeLiveBytes.end())-m_lifetimeLiveBytes.begin());
    ImGui::Checkbox("Physical slots",&m_lifetimePhysical);ImGui::SameLine();ImGui::Checkbox("Transient only",&m_lifetimeTransientOnly);
    ImGui::SameLine();ImGui::Checkbox("At selected Pass",&m_lifetimeAtPass);
    int selected=static_cast<int>(m_selectedPass);ImGui::SetNextItemWidth(175);
    if(ImGui::SliderInt("Pass",&selected,0,static_cast<int>(count-1),"%d",ImGuiSliderFlags_AlwaysClamp))m_selectedPass=selected;
    ImGui::SameLine();if(ImGui::SmallButton("Peak"))m_selectedPass=m_lifetimePeakPass;
    ImGui::SameLine();ImGui::SetNextItemWidth(100);ImGui::SliderFloat("Zoom",&m_lifetimeZoom,1,8,"%.1fx",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();if(ImGui::SmallButton("Fit"))m_lifetimeZoom=1;
    ImGui::Text("Scratch at P%zu: %.2f MiB | peak %.2f MiB at P%zu",m_selectedPass,m_lifetimeLiveBytes[m_selectedPass]/MiB,m_lifetimeLiveBytes[m_lifetimePeakPass]/MiB,m_lifetimePeakPass);
    std::vector<float> profile;profile.reserve(count);for(const auto bytes:m_lifetimeLiveBytes)profile.push_back(static_cast<float>(bytes/MiB));
    ImGui::PlotHistogram("##scratch-profile",profile.data(),static_cast<int>(count),0,nullptr,0,std::max(1.f,profile[m_lifetimePeakPass]),{ImGui::GetContentRegionAvail().x,42});
    const auto profileMin=ImGui::GetItemRectMin(),profileMax=ImGui::GetItemRectMax();
    m_lifetimeProfileRect={profileMin.x,profileMin.y,profileMax.x,profileMax.y};
    const auto& style=ImGui::GetStyle();const float profileStart=profileMin.x+style.FramePadding.x,profileWidth=std::max(1.f,profileMax.x-profileMin.x-2*style.FramePadding.x);
    const float selectedX=profileStart+profileWidth*(float(m_selectedPass)+.5f)/count;
    ImGui::GetWindowDrawList()->AddLine({selectedX,profileMin.y},{selectedX,profileMax.y},IM_COL32(105,202,255,255),2);
    if(ImGui::IsItemHovered()) {
        const auto p=std::min(count-1,static_cast<size_t>(std::max(0.f,(ImGui::GetIO().MousePos.x-profileStart)/profileWidth)*count));
        ImGui::SetTooltip("P%zu  %s\nLive logical scratch %.3f MiB\nClick to select this Pass",p,graph.passes[p].name.c_str(),m_lifetimeLiveBytes[p]/MiB);
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))m_selectedPass=p;
    }
    std::vector<LifetimeRow> rows;std::map<uint64_t,size_t> slots;size_t shown=0;
    for(size_t r=0;r<graph.resources.size();++r) {
        const auto& resource=graph.resources[r];const auto& life=resource.lifetime;
        if(m_lifetimeTransientOnly&&resource.origin!=ResourceOrigin::Transient)continue;
        if(m_lifetimeAtPass&&(life.first>m_selectedPass||life.last<m_selectedPass))continue;
        bool matches=Matches(resource.name,m_filter.data());
        if(!matches&&m_filter[0])for(const auto& pass:graph.passes)if(Matches(pass.name,m_filter.data())&&std::any_of(pass.accesses.begin(),pass.accesses.end(),[&](const auto& access){return access.resource.index==r;})){matches=true;break;}
        if(!matches)continue;
        const auto slot=m_lifetimePhysical&&resource.origin==ResourceOrigin::Transient?resource.physicalSlot:0;
        size_t row=rows.size();
        if(slot){const auto found=slots.find(slot);if(found!=slots.end())row=found->second;else slots.emplace(slot,row);}
        if(row==rows.size())rows.push_back({slot});
        rows[row].resources.push_back(r);if(life.first<count)rows[row].bytes=std::max(rows[row].bytes,life.estimatedBytes);++shown;
    }
    for(auto& row:rows)std::stable_sort(row.resources.begin(),row.resources.end(),[&](size_t a,size_t b){return graph.resources[a].lifetime.first<graph.resources[b].lifetime.first;});
    std::stable_sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.bytes>b.bytes;});
    const bool selectedVisible=std::any_of(rows.begin(),rows.end(),[&](const auto& row){return std::find(row.resources.begin(),row.resources.end(),m_selectedResource)!=row.resources.end();});
    if(!selectedVisible&&!rows.empty()){m_selectedResource=rows.front().resources.front();m_selectedVersion=graph.resources[m_selectedResource].finalVersion;}
    ImGui::TextDisabled("%zu rows / %zu resources | Whole-plan scratch estimates; horizontal scroll to pan",rows.size(),shown);
    const float labelWidth=160,axisWidth=std::max(float(count)*4,ImGui::GetContentRegionAvail().x-labelWidth-2*style.WindowPadding.x-style.ScrollbarSize-12)*m_lifetimeZoom;
    const float rowHeight=30,headerHeight=22,height=std::max(100.f,ImGui::GetContentRegionAvail().y-102);
    ImGui::SetNextWindowContentSize({labelWidth+axisWidth+8,headerHeight+rowHeight*rows.size()});
    ImGui::BeginChild("lifetime-rows",{0,height},ImGuiChildFlags_Borders,ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(style.ItemSpacing.x,0));
    const auto start=ImGui::GetCursorScreenPos();const float labelX=start.x+ImGui::GetScrollX(),axisViewportX=labelX+labelWidth,axisX=start.x+labelWidth,cell=axisWidth/count;
    const float clipRight=ImGui::GetWindowPos().x+ImGui::GetWindowSize().x;
    auto* draw=ImGui::GetWindowDrawList();const auto font=ImGui::GetFont();const float fontSize=ImGui::GetFontSize();
    ImGui::Dummy({labelWidth+axisWidth,headerHeight});
    const size_t step=std::max<size_t>(1,static_cast<size_t>(std::ceil(46.f/cell)));
    draw->PushClipRect({axisViewportX,start.y},{clipRight,start.y+headerHeight},true);
    for(size_t p=0;p<count;p+=step)draw->AddText({axisX+p*cell,start.y},IM_COL32(158,174,194,255),std::format("P{}",p).c_str());
    draw->PopClipRect();draw->AddText({labelX+4,start.y},IM_COL32(158,174,194,255),"Storage / plan use");
    ImGuiListClipper clipper;clipper.Begin(static_cast<int>(rows.size()),rowHeight);
    while(clipper.Step())for(int rowIndex=clipper.DisplayStart;rowIndex<clipper.DisplayEnd;++rowIndex) {
        const auto& row=rows[rowIndex];const auto pos=ImGui::GetCursorScreenPos();const float y=pos.y;
        ImGui::InvisibleButton(std::format("row{}",rowIndex).c_str(),{labelWidth+axisWidth,rowHeight});
        draw->AddRectFilled({pos.x,y},{pos.x+labelWidth+axisWidth,y+rowHeight-1},rowIndex%2?IM_COL32(30,34,43,255):IM_COL32(24,28,36,255));
        const auto& first=graph.resources[row.resources.front()];const ImVec4 labelClip={labelX,y,axisViewportX-6,y+rowHeight};
        const auto label=row.slot?std::format("Slot #{} | {} res.",row.slot,row.resources.size()):std::format("R{}  {}",row.resources.front(),first.name);
        const auto subtitle=first.origin==ResourceOrigin::Transient?(first.lifetime.first<count?std::format("{:.2f} MiB",row.bytes/MiB):"Unused: no allocation"):std::string(Origin(first.origin))+" | external";
        draw->PushClipRect({axisViewportX,y},{clipRight,y+rowHeight},true);
        for(size_t p=0;p<count;p+=step)draw->AddLine({axisX+p*cell,y},{axisX+p*cell,y+rowHeight},IM_COL32(255,255,255,16));
        draw->AddLine({axisX+(m_selectedPass+.5f)*cell,y},{axisX+(m_selectedPass+.5f)*cell,y+rowHeight},IM_COL32(105,202,255,100));
        for(const auto r:row.resources) {
            const auto& resource=graph.resources[r];const auto& life=resource.lifetime;
            if(life.first>=count){draw->AddText({axisX+4,y+7},ImGui::GetColorU32(ImGuiCol_TextDisabled),resource.origin==ResourceOrigin::Transient?"Unused: no allocation":"Unused in plan");continue;}
            const float left=axisX+life.first*cell+1,right=axisX+(std::min(life.last,count-1)+1)*cell-1;
            const std::array<float,4> rect={left,y+4,std::max(left+1,right),y+rowHeight-4};
            if(rect[2]>axisViewportX&&rect[0]<clipRight)m_lifetimeBars.push_back({r,{std::max(rect[0],axisViewportX),rect[1],std::min(rect[2],clipRight),rect[3]}});
            const auto color=resource.origin==ResourceOrigin::Transient?SlotColor(resource.physicalSlot?resource.physicalSlot:r+1):IM_COL32(65,86,106,255);
            draw->AddRectFilled({rect[0],rect[1]},{rect[2],rect[3]},color,2);
            if(r==m_selectedResource)draw->AddRect({rect[0],rect[1]},{rect[2],rect[3]},IM_COL32(240,245,255,255),2,ImDrawFlags_None,2);
            if(life.exported)draw->AddLine({rect[2]-2,rect[1]},{rect[2]-2,rect[3]},IM_COL32(255,180,85,255),3);
            const ImVec4 textClip={rect[0]+2,rect[1],rect[2]-2,rect[3]};
            const auto title=std::format("R{} {}",r,resource.name);draw->AddText(font,fontSize*.9f,{rect[0]+3,rect[1]+3},IM_COL32(255,255,255,255),title.c_str(),nullptr,0,&textClip);
            if(ImGui::IsItemHovered()&&ImGui::GetIO().MousePos.x>=axisViewportX&&ImGui::GetIO().MousePos.x>=rect[0]&&ImGui::GetIO().MousePos.x<=rect[2]) {
                ImGui::SetTooltip("R%zu  %s\n%s %ux%u | %s | slot %llu\nP%zu -> P%zu%s\nClick to select; double-click for resource details",r,resource.name.c_str(),Format(resource.format),resource.width,resource.height,Origin(resource.origin),resource.physicalSlot,life.first,std::min(life.last,count-1),life.exported?" (exported through plan end)":"");
                if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)){m_selectedResource=r;m_selectedVersion=resource.finalVersion;m_flowSelectionIsResource=true;if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))SelectResource(r);}
            }
        }
        draw->PopClipRect();
        draw->AddRectFilled({labelX,y},{axisViewportX,y+rowHeight-1},rowIndex%2?IM_COL32(30,34,43,255):IM_COL32(24,28,36,255));
        draw->AddText(font,fontSize,{labelX+4,y+2},IM_COL32(220,230,245,255),label.c_str(),nullptr,0,&labelClip);
        draw->AddText(font,fontSize*.82f,{labelX+4,y+17},IM_COL32(158,174,194,255),subtitle.c_str(),nullptr,0,&labelClip);
        if(ImGui::IsItemHovered()&&ImGui::GetIO().MousePos.x<axisViewportX)ImGui::SetTooltip("%s\n%zu logical resource(s) in this row",first.name.c_str(),row.resources.size());
    }
    if(rows.empty())ImGui::TextDisabled("No resources match these filters");
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::BeginChild("lifetime-detail",{0,0});
    if(!rows.empty()) {
        const auto& resource=graph.resources[m_selectedResource];const auto& life=resource.lifetime;
        ImGui::TextWrapped("R%zu  %s | %s %ux%u",m_selectedResource,resource.name.c_str(),Format(resource.format),resource.width,resource.height);
        if(life.first<count) {
            if(ImGui::SmallButton(std::format("First: P{}",life.first).c_str()))m_selectedPass=life.first;
            ImGui::SameLine();if(ImGui::SmallButton(std::format("Last: P{}",std::min(life.last,count-1)).c_str()))m_selectedPass=std::min(life.last,count-1);
            ImGui::SameLine();ImGui::TextDisabled(life.exported?"Exported through plan end":"Inclusive first/last use");
        } else ImGui::TextDisabled("Unused in this plan; no scratch allocation.");
        if(ImGui::SmallButton("Resource details"))SelectResource(m_selectedResource);
        ImGui::SameLine();if(ImGui::SmallButton("Pass details"))SelectPass(m_selectedPass);
        ImGui::TextDisabled("Plan use only; pool storage stays allocated until retirement and caching rules allow release.");
    }
    ImGui::EndChild();
}

void OvEditor::Panels::FrameGraph::DrawPreview()
{
    const auto& graph=*m_snapshot;
    if(graph.resources.empty()){ImGui::TextDisabled("No resource to preview");return;}
    if(ImGui::BeginCombo("Resource",graph.resources[m_selectedResource].name.c_str())) {
        for(size_t r=0;r<graph.resources.size();++r) {
            ImGui::PushID(static_cast<int>(r));
            if(ImGui::Selectable(std::format("R{}  {}",r,graph.resources[r].name).c_str(),r==m_selectedResource)) {
                m_selectedResource=r;m_selectedVersion=graph.resources[r].finalVersion;m_flowSelectionIsResource=true;m_previewMip=m_previewLayer=0;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const auto& resource=graph.resources[m_selectedResource];
    int version=static_cast<int>(m_selectedVersion);ImGui::SetNextItemWidth(130);
    if(ImGui::SliderInt("Version",&version,0,static_cast<int>(resource.finalVersion)))m_selectedVersion=static_cast<uint32_t>(version);
    m_previewMip=std::clamp(m_previewMip,0,int(resource.mips)-1);m_previewLayer=std::clamp(m_previewLayer,0,int(resource.layers)-1);
    ImGui::SameLine();ImGui::SetNextItemWidth(110);ImGui::SliderInt("Mip",&m_previewMip,0,resource.mips-1);
    if(resource.layers>1){ImGui::SameLine();ImGui::SetNextItemWidth(100);const char* faces[]={"+X","-X","+Y","-Y","+Z","-Z"};ImGui::Combo("Face",&m_previewLayer,faces,6);}
    const char* channels[]={"Auto","RGB","Red","Green","Blue","Alpha","Depth"};ImGui::SetNextItemWidth(110);ImGui::Combo("Channel",&m_previewChannel,channels,7);
    const bool depth=m_previewChannel==int(FrameGraphPreviewChannel::Depth)||(m_previewChannel==0&&(uint8_t(resource.aspect)&uint8_t(ResourceAspect::Depth)));
    ImGui::SameLine();
    if(depth) {
        float range[]={m_previewMin,m_previewMax};ImGui::SetNextItemWidth(180);
        if(ImGui::DragFloat2("Depth range",range,.0001f,0,1,"%.5f")){m_previewMin=range[0];m_previewMax=range[1];}
    } else {
        ImGui::SetNextItemWidth(130);ImGui::SliderFloat("EV",&m_previewExposure,-12,12,"%.1f");ImGui::SameLine();ImGui::Checkbox("Tone map",&m_previewToneMap);
    }
    ImGui::BeginDisabled(m_frozen||bool(m_previewRequest)||bool(m_previewTicket));
    if(ImGui::Button("Capture"))QueuePreview();ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button("Clear image")){m_previewImage.reset();m_previewRequest.reset();m_previewTicket.reset();m_previewStatus.clear();m_clearPreview=true;}
    if(m_previewImage){ImGui::SameLine();if(ImGui::Button("Texture Debugger"))OpenPreviewEvent.Invoke(m_previewImage);}
    ImGui::TextDisabled(m_frozen?"Frozen plan: only the captured image is available. Unfreeze to capture.":"One-shot capture from the next matching plan. No continuous GPU copies.");
    if(!m_previewStatus.empty())ImGui::TextWrapped("%s",m_previewStatus.c_str());
    ImGui::BeginChild("preview-image",{0,0},ImGuiChildFlags_Borders);
    if(m_previewImage&&m_previewImage->texture->RetainStorage().GetID()) {
        const auto& image=*m_previewImage;
        ImGui::TextWrapped("%s v%u | mip %u, layer %u | scope %llu | before P%zu",image.resourceName.c_str(),image.version,image.mip,image.layer,image.scope,image.beforePass);
        ImGui::TextDisabled("Source %ux%u | image %ux%u RGBA8 | %.2f MiB",image.sourceWidth,image.sourceHeight,image.width,image.height,image.width*image.height*4/MiB);
        const auto available=ImGui::GetContentRegionAvail();const float scale=std::max(.01f,std::min(available.x/image.width,available.y/image.height));
        OvUI::Internal::TextureID texture(image.texture);texture.RetainForCapture();
        ImGui::Image(texture.id,{image.width*scale,image.height*scale},{0,1},{1,0});
    } else ImGui::TextDisabled("Select a resource/version, mip and channel, then Capture.");
    ImGui::EndChild();
}

void OvEditor::Panels::FrameGraph::DrawMemory()
{
    ImGui::TextWrapped("Pool is live, even when the plan is frozen. Categories below partition physical pool storage; older leases may be earlier frames of the same size.");
    const char* labels[]={"Active: latest plan","Active: older plans","External pins only","Idle: latest reserve","Idle: unused sizes/effects"};
    const uint64_t bytes[]={m_pool.activeCurrentBytes,m_pool.activeOldBytes,m_pool.pinnedOnlyBytes,m_pool.idleReserveBytes,m_pool.idleUnusedBytes};
    const size_t counts[]={m_pool.activeCurrentTargets,m_pool.activeOldTargets,m_pool.pinnedOnlyTargets,m_pool.idleReserveTargets,m_pool.idleUnusedTargets};
    if(ImGui::BeginTable("memory-categories",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInner)) {
        ImGui::TableSetupColumn("Physical pool category");ImGui::TableSetupColumn("MiB");ImGui::TableSetupColumn("Targets");ImGui::TableHeadersRow();
        for(size_t i=0;i<5;++i){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(labels[i]);ImGui::TableNextColumn();ImGui::Text("%.2f",bytes[i]/MiB);ImGui::TableNextColumn();ImGui::Text("%zu",counts[i]);}
        ImGui::EndTable();
    }
    ImGui::Text("Pool total: %.2f MiB",(m_pool.idleBytes+m_pool.leasedBytes)/MiB);
    if(ImGui::CollapsingHeader("Plan resource roles")) {
        ImGui::TextWrapped("Logical descriptors in snapshot %llu. Output/History/Imported storage belongs to its owner; these estimates are not added to the pool total. Other history generations, preview/query objects, meshes and driver overhead are outside this view.",m_snapshot->captureIndex);
        uint64_t roleBytes[4]{};size_t roleCounts[4]{};
        const ResourceOrigin origins[]={ResourceOrigin::CurrentFrame,ResourceOrigin::History,ResourceOrigin::Imported,ResourceOrigin::Transient};
        for(const auto& r:m_snapshot->resources)if(r.lifetime.first<m_snapshot->passes.size()&&r.kind!=PlanResourceKind::Backbuffer)
            for(size_t i=0;i<4;++i)if(r.origin==origins[i]){roleBytes[i]+=r.lifetime.estimatedBytes;++roleCounts[i];}
        if(ImGui::BeginTable("memory-roles",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInner)) {
            ImGui::TableSetupColumn("Plan role");ImGui::TableSetupColumn("Logical MiB");ImGui::TableSetupColumn("Used resources");ImGui::TableHeadersRow();
            for(size_t i=0;i<4;++i){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(Origin(origins[i]));ImGui::TableNextColumn();ImGui::Text("%.2f",roleBytes[i]/MiB);ImGui::TableNextColumn();ImGui::Text("%zu",roleCounts[i]);}
            ImGui::EndTable();
        }
    }
    std::vector<size_t> order(m_poolEntries.size());std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return m_poolEntries[a].bytes>m_poolEntries[b].bytes;});
    ImGui::TextDisabled("Largest pool targets first; age counts successfully realized graphs.");
    if(ImGui::BeginTable("memory-targets",5,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInner|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollY,{0,0})) {
        ImGui::TableSetupColumn("Slot / state");ImGui::TableSetupColumn("Size");ImGui::TableSetupColumn("MiB");ImGui::TableSetupColumn("Age");ImGui::TableSetupColumn("Pins");ImGui::TableHeadersRow();
        if(order.empty()){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextDisabled("No pool targets");}
        ImGuiListClipper clipper;clipper.Begin(static_cast<int>(order.size()));
        while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
            const auto& entry=m_poolEntries[order[row]];const auto category=entry.leased?(entry.requiredByLatest?0:1):entry.pins?2:entry.requiredByLatest?3:4;
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("#%llu  %s",entry.slot,labels[category]);
            if(ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();ImGui::Text("%s %s | %u mips, %u layers, %u samples",Kind(entry.kind),Format(entry.format),entry.mips,entry.layers,entry.samples);
                bool found=false;for(size_t r=0;r<m_snapshot->resources.size();++r)if(m_snapshot->resources[r].physicalSlot==entry.slot){ImGui::Text("Snapshot R%zu: %s",r,m_snapshot->resources[r].name.c_str());found=true;}
                if(!found)ImGui::TextDisabled("No matching scratch slot in the displayed snapshot.");ImGui::EndTooltip();
            }
            ImGui::TableNextColumn();ImGui::Text("%ux%u",entry.width,entry.height);ImGui::TableNextColumn();ImGui::Text("%.2f",entry.bytes/MiB);
            ImGui::TableNextColumn();ImGui::Text("%llu",entry.unusedGraphs);ImGui::TableNextColumn();ImGui::Text("%zu",entry.pins);
        }
        ImGui::EndTable();
    }
}

void OvEditor::Panels::FrameGraph::DrawTimings()
{
    if(ImGui::CollapsingHeader("CPU stages")) {
        const auto& cpu=m_snapshot->cpu;
        if(!cpu.captured)ImGui::TextDisabled("No CPU sample for this diagnostic snapshot.");
        else {
            ImGui::Text("CPU stages: %.3f ms | snapshot %llu",cpu.Total()/1e6,m_snapshot->captureIndex);
            ImGui::TextWrapped("Producer wall time, including joined jobs. Realize includes allocation/owner waits. Excludes extraction, composition, preview, submission, GPU execution and presentation.");
            if(cpu.encodingDeferred)ImGui::TextDisabled("Product encoding is deferred to RenderFrame::Seal; not included here.");
            const char* names[]={"Compile","Realize","Prepare views","Capture diagnostics","Record views","Record operations","Seal commands"};
            const uint64_t values[]={cpu.compile,cpu.realize,cpu.prepare,cpu.diagnostics,cpu.recordViews,cpu.recordOperations,cpu.seal};
            if(ImGui::BeginTable("cpu-stages",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInner)) {
                ImGui::TableSetupColumn("Stage");ImGui::TableSetupColumn("CPU ms");ImGui::TableSetupColumn("Share");ImGui::TableHeadersRow();
                for(size_t i=0;i<7;++i) {
                    ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(names[i]);ImGui::TableNextColumn();
                    if(i==6&&cpu.encodingDeferred)ImGui::TextDisabled("Deferred");else ImGui::Text("%.3f",values[i]/1e6);
                    ImGui::TableNextColumn();ImGui::ProgressBar(cpu.Total()?float(double(values[i])/cpu.Total()):0,{ -1,ImGui::GetTextLineHeight() });
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::Separator();
    ImGui::BeginDisabled(m_frozen);ImGui::Checkbox("Capture GPU timings",&m_captureGpuTimings);ImGui::EndDisabled();
    ImGui::SameLine();ImGui::Checkbox("Slowest first",&m_slowestFirst);
    ImGui::TextDisabled("Samples every 4 plans; reads after GPU completion without waiting.");
    if(!m_gpuTimings){ImGui::TextWrapped(m_frozen?"Unfreeze to capture GPU timings.":m_captureGpuTimings?"Waiting for a successful GPU sample.":"Enable Capture GPU timings to measure Pass costs.");return;}
    const auto& timings=*m_gpuTimings;const auto& graph=*timings.graph;
    ImGui::Text("Pass GPU sum: %.3f ms | %zu passes",timings.totalNanoseconds/1e6,graph.passes.size());
    ImGui::TextDisabled("Measured frame %llu | snapshot %llu | scope %llu",timings.frameID,graph.captureIndex,graph.scope);
    ImGui::TextWrapped("GPU elapsed intervals; excludes CPU recording, swap/vsync and preview conversion. Empty Passes include timestamp overhead.");
    ImGui::TextDisabled("Click a Pass to freeze this measured plan and inspect its resources.");
    if(ImGui::Button("Show GPU heat"))ShowGpuHeat(static_cast<size_t>(std::max_element(timings.passNanoseconds.begin(),timings.passNanoseconds.end())-timings.passNanoseconds.begin()));
    std::vector<size_t> order;
    for(size_t p=0;p<graph.passes.size();++p)if(Matches(graph.passes[p].name,m_filter.data()))order.push_back(p);
    if(m_slowestFirst)std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return timings.passNanoseconds[a]>timings.passNanoseconds[b];});
    if(ImGui::BeginTable("timing-table",4,ImGuiTableFlags_BordersInner|ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollY,{0,0})) {
        ImGui::TableSetupColumn("Pass");ImGui::TableSetupColumn("View",ImGuiTableColumnFlags_WidthFixed,45);
        ImGui::TableSetupColumn("GPU ms",ImGuiTableColumnFlags_WidthFixed,75);ImGui::TableSetupColumn("Share",ImGuiTableColumnFlags_WidthFixed,125);ImGui::TableHeadersRow();
        if(order.empty()){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextDisabled("No matches");}
        ImGuiListClipper clipper;clipper.Begin(static_cast<int>(order.size()));
        while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
            const auto p=order[row];const auto& pass=graph.passes[p];const auto ns=timings.passNanoseconds[p];
            ImGui::PushID(static_cast<int>(p));ImGui::TableNextRow();ImGui::TableNextColumn();
            if(ImGui::Selectable(std::format("P{}  {}",p,pass.name).c_str(),m_frozen&&m_snapshot->scope==graph.scope&&m_selectedPass==p,ImGuiSelectableFlags_SpanAllColumns)) {
                if(FreezeMeasuredPlan(p))SelectPass(p);
            }
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%llu ns | measured scope %llu",pass.name.c_str(),ns,graph.scope);
            ImGui::TableNextColumn();ImGui::Text("%llu",pass.viewId);ImGui::TableNextColumn();ImGui::Text("%.3f",ns/1e6);
            ImGui::TableNextColumn();const float share=timings.totalNanoseconds?float(double(ns)/timings.totalNanoseconds):0;
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,ImVec4(.35f+share*.6f,.65f-share*.3f,.35f,1));
            ImGui::ProgressBar(share,{ImGui::GetContentRegionAvail().x,ImGui::GetTextLineHeight()},std::format("{:.1f}%",share*100).c_str());ImGui::PopStyleColor();ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
