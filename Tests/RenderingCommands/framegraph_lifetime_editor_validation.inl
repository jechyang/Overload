// Included in the Main/Render product fixture; all interaction uses its real UI.
{
    panel.m_frozen=true;const auto lifetimePlan=panel.m_snapshot;panel.m_requestedTab=5;tick();tick();
    const auto lifeTab=ImHashStr("Lifetimes",0,tabs->ID);
    Require(tabs->SelectedTabId==lifeTab&&panel.m_lifetimePhysical&&panel.m_lifetimeTransientOnly&&!panel.m_lifetimeAtPass,
        "Lifetimes did not open the actual physical scratch timeline");
    const auto activateLifeItem=[&](const char* label){ImGui::ActivateItemByID(ImHashStr(label,0,lifeTab));};
    const auto findLifeChild=[&](const char* name) {
        const auto id=ImHashStr(name,0,lifeTab);ImGuiWindow* found=nullptr;
        for(auto* child:ImGui::GetCurrentContext()->Windows)if(child->ChildId==id)found=child;
        Require(found!=nullptr,"lifetime child window was not rendered");return found;
    };
    for(size_t p=0;p<lifetimePlan->passes.size();++p) {
        uint64_t expected=0;for(const auto& r:lifetimePlan->resources)
            if(r.origin==OvRendering::Data::ResourceOrigin::Transient&&r.lifetime.first<=p&&r.lifetime.last>=p)expected+=r.lifetime.estimatedBytes;
        Require(panel.m_lifetimeLiveBytes[p]==expected,"timeline scratch profile disagreed with inclusive compiled lifetimes");
    }
    Require(*std::max_element(panel.m_lifetimeLiveBytes.begin(),panel.m_lifetimeLiveBytes.end())==lifetimePlan->estimatedPeakBytes,
        "timeline peak disagreed with the product compiler");
    image("lifetimes-physical");
    auto* rows=findLifeChild("lifetime-rows");bool alias=false;
    Require(rows->ScrollMax.x==0,"Fit added an unnecessary horizontal scrollbar");
    const auto checkBars=[&] {
        for(const auto& a:panel.m_lifetimeBars)for(const auto& b:panel.m_lifetimeBars)if(a.resource!=b.resource) {
            const auto& ra=lifetimePlan->resources[a.resource];const auto& rb=lifetimePlan->resources[b.resource];
            if(ra.physicalSlot&&ra.physicalSlot==rb.physicalSlot) {
                Require(a.rect[1]==b.rect[1]&&(a.rect[2]<b.rect[0]||b.rect[2]<a.rect[0]),"shared physical slot did not draw disjoint bars on one row");alias=true;
            }
        }
    };
    for(float offset=0;;offset+=std::max(30.f,rows->InnerRect.GetHeight()-30)) {
        ImGui::SetScrollY(rows,std::min(offset,rows->ScrollMax.y));tick();tick();checkBars();
        if(offset>=rows->ScrollMax.y)break;
    }
    Require(alias,"actual product timeline never displayed physical resource reuse");
    ImGui::SetScrollY(rows,0);tick();tick();Require(!panel.m_lifetimeBars.empty(),"timeline has no visible bar to select");
    const auto clicked=panel.m_lifetimeBars.front();panel.m_selectedResource=lifetimePlan->resources.size()-1;
    input.AddMousePosEvent((clicked.rect[0]+clicked.rect[2])*.5f,(clicked.rect[1]+clicked.rect[3])*.5f);
    input.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();input.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();input.AddMousePosEvent(-100,-100);
    Require(panel.m_selectedResource==clicked.resource&&panel.m_snapshot==lifetimePlan,"clicking a lifetime bar lost its resource or frozen plan");
    const auto clickPass=std::min(panel.m_lifetimePeakPass+1,lifetimePlan->passes.size()-1);const auto plot=panel.m_lifetimeProfileRect;
    const auto padding=ImGui::GetStyle().FramePadding.x;
    input.AddMousePosEvent(plot[0]+padding+(plot[2]-plot[0]-2*padding)*(clickPass+.5f)/lifetimePlan->passes.size(),(plot[1]+plot[3])*.5f);
    input.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();input.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();input.AddMousePosEvent(-100,-100);
    Require(panel.m_selectedPass==clickPass,"clicking the memory profile did not select its Pass");
    activateLifeItem("At selected Pass");tick();
    Require(panel.m_lifetimeAtPass&&!panel.m_lifetimeBars.empty(),"active-Pass filter did not expose live resources");
    for(const auto& bar:panel.m_lifetimeBars){const auto& life=lifetimePlan->resources[bar.resource].lifetime;
        Require(life.first<=clickPass&&life.last>=clickPass,"active-Pass filter showed an inactive lifetime");}
    activateLifeItem("Peak");tick();tick();Require(panel.m_selectedPass==panel.m_lifetimePeakPass,"Peak did not select the peak memory Pass");image("lifetimes-peak");
    activateLifeItem("At selected Pass");tick();activateLifeItem("Physical slots");tick();tick();
    Require(!panel.m_lifetimePhysical&&!panel.m_lifetimeAtPass,"logical-resource timeline mode was not selectable");
    for(const auto& a:panel.m_lifetimeBars)for(const auto& b:panel.m_lifetimeBars)if(a.resource!=b.resource)
        Require(a.rect[1]!=b.rect[1],"logical resource mode combined independent resource rows");
    image("lifetimes-logical");
    const auto wholeProfile=panel.m_lifetimeLiveBytes;
    std::copy_n("does-not-exist",sizeof("does-not-exist"),panel.m_filter.begin());tick();
    Require(panel.m_lifetimeBars.empty()&&panel.m_lifetimeLiveBytes==wholeProfile,"name filtering corrupted the full-plan profile or retained stale bars");
    panel.m_filter[0]=0;panel.m_lifetimeZoom=3;tick();tick();
    const auto labelPixels=[&] {
        std::vector<uint8_t> result;const int left=int(rows->Pos.x+ImGui::GetStyle().WindowPadding.x+2),top=int(rows->InnerRect.Min.y+24),bottom=int(rows->InnerRect.Max.y-20);
        for(int y=top;y<bottom;++y)for(int x=left;x<left+145;++x){const auto offset=(size_t(859-y)*1280+x)*4;result.insert(result.end(),pixels.begin()+offset,pixels.begin()+offset+4);}return result;
    };
    const auto labelsBeforePan=labelPixels();
    Require(rows->ScrollMax.x>0,"zoomed timeline has no horizontal navigation");ImGui::SetScrollX(rows,rows->ScrollMax.x);tick();tick();
    Require(rows->Scroll.x>0,"timeline horizontal pan was not applied");image("lifetimes-zoom");
    Require(labelPixels()==labelsBeforePan,"horizontal pan moved or overwrote storage labels");
    activateLifeItem("Fit");tick();ImGui::SetScrollX(rows,0);ImGui::SetScrollY(rows,0);tick();
    activateLifeItem("Transient only");tick();Require(!panel.m_lifetimeTransientOnly,"external resource lifetimes were not selectable");
    const auto external=std::find_if(lifetimePlan->resources.begin(),lifetimePlan->resources.end(),[&](const auto& r){return r.origin!=OvRendering::Data::ResourceOrigin::Transient&&r.lifetime.first<lifetimePlan->passes.size();});
    Require(external!=lifetimePlan->resources.end(),"fixture has no external resource lifetime");
    const auto externalFilter=external->name.substr(0,panel.m_filter.size()-1);
    std::copy(externalFilter.begin(),externalFilter.end(),panel.m_filter.begin());panel.m_filter[externalFilter.size()]=0;tick();tick();
    Require(std::any_of(panel.m_lifetimeBars.begin(),panel.m_lifetimeBars.end(),[&](const auto& bar){return lifetimePlan->resources[bar.resource].origin!=OvRendering::Data::ResourceOrigin::Transient;}),
        "external resource timeline was merged into scratch slots or omitted");
    auto* detail=findLifeChild("lifetime-detail");
    ImGui::ActivateItemByID(ImHashStr("Resource details",0,detail->ID));tick();tick();tick();
    Require(tabs->SelectedTabId==ImHashStr("Resources",0,tabs->ID),"timeline resource detail action did not open Resources");
    const auto resourcesTab=ImHashStr("Resources",0,tabs->ID);ImGui::ActivateItemByID(ImHashStr("Show lifetime",0,resourcesTab));tick();tick();tick();
    Require(tabs->SelectedTabId==lifeTab&&!panel.m_lifetimeTransientOnly,"resource detail action did not return to its external lifetime");
    fixture.views[1]->SetSize({530,250});tick();Require(panel.m_snapshot==lifetimePlan&&panel.m_lifetimeLiveBytes==wholeProfile,"resize changed a frozen lifetime profile");
    fixture.views[1]->SetSize({545,270});panel.m_filter[0]=0;
    // Controlled values cover exported end-of-plan, inclusive last use, unused
    // scratch, and an empty graph; they do not change the product render plan.
    auto edge=std::make_shared<OvRendering::Data::FrameGraphDiagnostics>(*lifetimePlan);edge->passes.resize(4);edge->resources.resize(4);
    for(size_t r=0;r<4;++r){auto& item=edge->resources[r];item.origin=OvRendering::Data::ResourceOrigin::Transient;item.name="Oracle "+std::to_string(r);item.lifetime={};item.physicalSlot=r<2?123:456;}
    constexpr uint64_t unit=1024*1024;edge->resources[0].lifetime={0,0,4*unit,false};edge->resources[1].lifetime={1,2,4*unit,false};
    edge->resources[2].lifetime={2,4,8*unit,true};edge->resources[3].lifetime.estimatedBytes=64*unit;edge->resources[3].physicalSlot=0;
    panel.m_snapshot=edge;panel.m_lifetimePhysical=true;panel.m_lifetimeTransientOnly=true;panel.m_selectedPass=panel.m_selectedResource=0;tick();tick();
    Require(panel.m_lifetimeLiveBytes==std::vector<uint64_t>({4*unit,4*unit,12*unit,8*unit})&&panel.m_lifetimePeakPass==2,
        "export/inclusive/unused scratch memory oracle failed");
    Require(panel.m_lifetimeBars.size()==3,"unused scratch emitted a lifetime bar");
    const auto exported=std::find_if(panel.m_lifetimeBars.begin(),panel.m_lifetimeBars.end(),[](const auto& bar){return bar.resource==2;});
    const auto a=std::find_if(panel.m_lifetimeBars.begin(),panel.m_lifetimeBars.end(),[](const auto& bar){return bar.resource==0;});
    const auto b=std::find_if(panel.m_lifetimeBars.begin(),panel.m_lifetimeBars.end(),[](const auto& bar){return bar.resource==1;});
    Require(a!=panel.m_lifetimeBars.end()&&b!=panel.m_lifetimeBars.end()&&exported!=panel.m_lifetimeBars.end()&&a->rect[1]==b->rect[1]&&a->rect[2]<b->rect[0]&&exported->rect[2]>b->rect[2],
        "exported and alias lifetime endpoints were not drawn correctly");
    edge=std::make_shared<OvRendering::Data::FrameGraphDiagnostics>(*edge);edge->passes.clear();panel.m_snapshot=edge;tick();
    Require(panel.m_lifetimeBars.empty()&&panel.m_lifetimeLiveBytes.empty(),"empty graph retained lifetime geometry or scratch values");
    panel.m_snapshot=lifetimePlan;panel.m_frozen=false;panel.m_requestedTab=0;tick();tick();
    Require(panel.m_snapshot!=lifetimePlan,"lifetime resume did not restore the live product graph");
    std::printf("PASS: %s lifetime timeline: compiled scratch profile/peak, real alias rows, native bar/profile clicks, filters, logical/external resources, zoom/pan, detail handoff, freeze/resize/resume, exported/unused/empty bounds\n",threaded?"Render":"Main");
}
