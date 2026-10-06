#include <OvEditor/Panels/FrameGraph.h>
#include <OvUI/Widgets/AWidget.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

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
    ImU32 SlotColor(uint64_t id)
    {
        float r,g,b;ImGui::ColorConvertHSVtoRGB(float(id%13)/13.f,.55f,.8f,r,g,b);
        return ImGui::GetColorU32(ImVec4(r,g,b,1));
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
}

OvEditor::Panels::FrameGraph::FrameGraph(const std::string& title,bool opened,const OvUI::Settings::PanelWindowSettings& settings)
    :PanelWindow(title,opened,settings)
{
    minSize={520,360};CreateWidget<Contents>([this]{DrawContents();});
    CloseEvent += [this]{RequireUIThread();m_snapshot.reset();m_graphNodes.clear();m_frozen=false;m_graphFit=true;};
}
void OvEditor::Panels::FrameGraph::RequireUIThread() const
{
    if(std::this_thread::get_id()!=m_uiThread)throw std::logic_error("Frame Graph panel requires its UI thread");
}
bool OvEditor::Panels::FrameGraph::WantsCapture() const {RequireUIThread();return IsOpened()&&!m_frozen;}
void OvEditor::Panels::FrameGraph::Update(std::shared_ptr<const FrameGraphDiagnostics> snapshot,const OvRendering::Core::RenderTargetPool::Statistics& pool)
{
    RequireUIThread();m_pool=pool;
    if(!m_budgetEdited)m_budgetMiB=static_cast<int>(std::min(uint64_t{4096},pool.idleBudget/uint64_t(MiB)));
    if(!IsOpened()){m_snapshot.reset();m_graphNodes.clear();m_frozen=false;return;}
    if(!m_frozen&&snapshot)m_snapshot=std::move(snapshot);
    if(m_snapshot){
        if(m_selectedPass>=m_snapshot->passes.size())m_selectedPass=0;
        if(m_selectedResource>=m_snapshot->resources.size())m_selectedResource=0;
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
void OvEditor::Panels::FrameGraph::SelectPass(size_t index){m_selectedPass=index;m_requestedTab=0;m_filter[0]=0;}
void OvEditor::Panels::FrameGraph::SelectResource(size_t index){m_selectedResource=index;m_requestedTab=1;m_filter[0]=0;}

void OvEditor::Panels::FrameGraph::DrawContents()
{
    RequireUIThread();
    ImGui::Checkbox("Freeze plan",&m_frozen);ImGui::SameLine();
    if(m_snapshot)ImGui::TextDisabled("Prepared snapshot %llu | scope %llu",m_snapshot->captureIndex,m_snapshot->scope);
    else ImGui::TextDisabled("Waiting for the next product frame");
    ImGui::TextDisabled("Plan captured before submission; pool counters below are live.");
    ImGui::Text("Idle %.1f MiB (%zu) | Leased/pinned %.1f MiB (%zu)",m_pool.idleBytes/MiB,m_pool.idleTargets,m_pool.leasedBytes/MiB,m_pool.leasedTargets);
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
    if(!m_snapshot)return;
    ImGui::Separator();
    ImGui::Text("%zu passes | %zu resources | %zu physical scratch slots",m_snapshot->passes.size(),m_snapshot->resources.size(),m_snapshot->physicalTransientTargets);
    ImGui::TextDisabled("Scratch: declared %.1f MiB | live peak %.1f MiB | physical %.1f MiB",
        m_snapshot->logicalTransientBytes/MiB,m_snapshot->estimatedPeakBytes/MiB,m_snapshot->physicalTransientBytes/MiB);
    ImGui::SetNextItemWidth(-1);if(ImGui::InputTextWithHint("##filter","Filter passes or resources",m_filter.data(),m_filter.size()))m_graphFit=true;
    if(ImGui::BeginTabBar("graph-tabs")) {
        const int request=m_requestedTab;m_requestedTab=-1;
        if(ImGui::BeginTabItem("Dependencies",nullptr,request==2?ImGuiTabItemFlags_SetSelected:0)){DrawDependencies();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Passes",nullptr,request==0?ImGuiTabItemFlags_SetSelected:0)){DrawPasses();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Resources",nullptr,request==1?ImGuiTabItemFlags_SetSelected:0)){DrawResources();ImGui::EndTabItem();}
        ImGui::EndTabBar();
    }
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
    if(ImGui::SmallButton("Show dependencies")){m_requestedTab=2;m_graphFocus=true;m_graphFit=true;}
    ImGui::TextDisabled("View %llu | %zu accesses",pass.viewId,pass.accesses.size());
    for(size_t i=0;i<pass.accesses.size();++i) {
        const auto& access=pass.accesses[i];const auto& resource=graph.resources[access.resource.index];ImGui::PushID(static_cast<int>(i));
        if(ImGui::Selectable(std::format("{}: {}",Access(access.type),resource.name).c_str()))SelectResource(access.resource.index);
        ImGui::TextDisabled("  mip %u +%u, layer %u +%u",access.range.firstMip,access.range.mipCount,access.range.firstLayer,access.range.layerCount);
        if(access.type==ResourceAccessType::Attachment)ImGui::TextDisabled("  %s / %s",access.load==AttachmentLoad::Clear?"Clear":access.load==AttachmentLoad::Load?"Load":"DontCare",access.store==AttachmentStore::Store?"Store":"Discard");
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
    const auto& graph=*m_snapshot;m_graphNodes.clear();
    if(graph.passes.empty()){ImGui::TextDisabled("No passes");return;}
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
    const float height=std::max(140.f,ImGui::GetContentRegionAvail().y-125);
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
            m_graphNodes.push_back({p,rect});
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
            const auto p=node.pass;const auto& rect=node.rect;const bool selected=p==m_selectedPass;
            const ImVec2 min={rect[0],rect[1]},max={rect[2],rect[3]};
            draw->AddRectFilled(min,max,selected?IM_COL32(40,76,105,255):IM_COL32(40,46,58,255),5.f*m_graphZoom);
            draw->AddRect(min,max,selected?IM_COL32(105,202,255,255):p==hoveredPass?IM_COL32(190,200,215,255):IM_COL32(85,96,112,255),5.f*m_graphZoom,ImDrawFlags_None,selected?2.f:1.f);
            const float fontSize=ImGui::GetFontSize()*m_graphZoom;const ImVec4 clip={min.x+5*m_graphZoom,min.y,max.x-5*m_graphZoom,max.y};
            draw->AddText(ImGui::GetFont(),fontSize,{min.x+8*m_graphZoom,min.y+7*m_graphZoom},IM_COL32(230,235,245,255),std::format("P{}  {}",p,graph.passes[p].name).c_str(),nullptr,0,&clip);
            draw->AddText(ImGui::GetFont(),fontSize*.85f,{min.x+8*m_graphZoom,min.y+32*m_graphZoom},IM_COL32(158,174,194,255),std::format("View {} | {} accesses",graph.passes[p].viewId,graph.passes[p].accesses.size()).c_str(),nullptr,0,&clip);
        }
        if(hoveredPass!=size_t(-1)) {
            ImGui::SetTooltip("P%zu  %s\nClick to select; double-click for pass details",hoveredPass,graph.passes[hoveredPass].name.c_str());
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
    ImGui::BeginChild("dependency-detail",{0,0});
    if(filtered&&std::none_of(matches.begin(),matches.end(),[](bool value){return value;})) {
        ImGui::TextDisabled("No pass or resource matches this filter");ImGui::EndChild();return;
    }
    const auto& pass=graph.passes[m_selectedPass];ImGui::TextWrapped("Selected: P%zu  %s",m_selectedPass,pass.name.c_str());
    if(ImGui::SmallButton("Pass details"))SelectPass(m_selectedPass);
    for(size_t a=0;a<pass.accesses.size();++a) {
        const auto& access=pass.accesses[a];ImGui::PushID(static_cast<int>(a));
        if(ImGui::Selectable(std::format("{}: {}",Access(access.type),graph.resources[access.resource.index].name).c_str()))SelectResource(access.resource.index);ImGui::PopID();
    }
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
            if(ImGui::Selectable(resource.name.c_str(),m_selectedResource==i,ImGuiSelectableFlags_SpanAllColumns))m_selectedResource=i;
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
            if(ImGui::Selectable(std::format("P{}  {} ({})",p,graph.passes[p].name,Access(access.type)).c_str()))SelectPass(p);
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}
