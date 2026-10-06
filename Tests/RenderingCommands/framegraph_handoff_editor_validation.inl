// Native product UI: memory categories, compact controls, canvas/locator/export.
{
    panel.m_frozen=true;const auto reportPlan=panel.m_snapshot;panel.m_requestedTab=6;tick();tick();
    const auto memoryTab=ImHashStr("Memory",0,tabs->ID);
    Require(tabs->SelectedTabId==memoryTab&&ImGui::GetCurrentContext()->Tables.GetByKey(ImHashStr("memory-categories",0,memoryTab)),
        "Memory did not render through the real product UI");
    const auto checkPoolValues=[&] {
        const auto& s=panel.m_pool;uint64_t bytes=0;for(const auto& e:panel.m_poolEntries)bytes+=e.bytes;
        Require(bytes==s.idleBytes+s.leasedBytes&&panel.m_poolEntries.size()==s.idleTargets+s.leasedTargets&&
            s.activeCurrentBytes+s.activeOldBytes+s.pinnedOnlyBytes+s.idleReserveBytes+s.idleUnusedBytes==bytes,
            "live UI memory entries do not reconcile with physical categories");
    };
    checkPoolValues();image("memory");
    const auto priorAllocations=panel.m_pool.allocations;
    // Asset View renders the active postprocess chain in Edit mode. Game View
    // only resizes its persistent output here, so it cannot test scratch growth.
    fixture.views[0]->SetSize({505,240});tick();tick();tick();checkPoolValues();
    Require(panel.m_snapshot==reportPlan&&panel.m_pool.allocations>priorAllocations,"frozen plan prevented live resized pool diagnostics");
    ImGui::ActivateItemByID(ImHashStr("Plan resource roles",0,memoryTab));tick();image("memory-roles");
    fixture.views[0]->SetSize({545,270});tick();tick();
    // Folded controls still produce the same budget/trim requests.
    ImGui::ActivateItemByID(ImHashStr("Pool controls",0,window->ID));tick();
    panel.m_budgetMiB=128;panel.m_budgetEdited=true;
    ImGui::ActivateItemByID(ImHashStr("Apply",0,window->ID));tick();tick();
    Require(!panel.m_requests.budget&&!panel.m_budgetEdited&&pool->GetStatistics().idleBudget==128ull*1024*1024,
        "folded pool controls lost the actual Apply action");
    ImGui::ActivateItemByID(ImHashStr("Pool controls",0,window->ID));tick();
    panel.m_requestedTab=2;panel.m_graphResourceFlow=true;panel.m_graphFit=true;tick();tick();
    const auto findGraphCanvas=[&](const char* name) {
        const auto id=ImHashStr(name,0,ImHashStr("Dependencies",0,tabs->ID));ImGuiWindow* child=nullptr;
        for(auto* w:ImGui::GetCurrentContext()->Windows)if(w->ChildId==id)child=w;
        Require(child!=nullptr,"graph canvas was not drawn");return child;
    };
    auto* canvas=findGraphCanvas("resource-flow-canvas");const auto normalHeight=canvas->Size.y;
    activateGraphItem("Expand canvas");tick();tick();
    Require(panel.m_graphExpanded&&canvas->Size.y>normalHeight+60,"Expand canvas did not reclaim the resource detail area");image("expanded-canvas");
    panel.m_graphResourceFlow=false;panel.m_graphFit=true;tick();tick();
    auto* dependency=findGraphCanvas("dependency-canvas");const auto expandedHeight=dependency->Size.y;
    activateGraphItem("Expand canvas");tick();tick();
    Require(!panel.m_graphExpanded&&dependency->Size.y<expandedHeight-100,"dependency canvas expansion did not restore details");
    // Real popup result selection focuses the requested Pass/resource.
    const auto locate=[&](bool resource,size_t index) {
        const auto label=resource?std::format("R{}  {}",index,reportPlan->resources[index].name):std::format("P{}  {}",index,reportPlan->passes[index].name);
        const auto prefix=std::format("{}{}  ",resource?"R":"P",index);
        std::copy(prefix.begin(),prefix.end(),panel.m_locator.begin());panel.m_locator[prefix.size()]=0;
        ImGui::ActivateItemByID(ImHashStr("Locate...",0,window->ID));tick();tick();
        auto& stack=ImGui::GetCurrentContext()->OpenPopupStack;Require(!stack.empty()&&stack.back().Window,"locator did not open its native popup");
        auto* popup=stack.back().Window;ImGuiWindow* child=nullptr;
        for(auto* w:ImGui::GetCurrentContext()->Windows)if(w->ChildId==ImHashStr("locator-results",0,popup->ID))child=w;
        Require(child!=nullptr,"locator result list was not drawn");
        ImGui::ActivateItemByID(ImHashStr(label.c_str(),0,child->ID));tick();tick();tick();
        Require(tabs->SelectedTabId==ImHashStr("Dependencies",0,tabs->ID)&&panel.m_graphFocus&&panel.m_filter[0]==0&&
            panel.m_flowSelectionIsResource==resource&&(resource?panel.m_selectedResource==index:panel.m_selectedPass==index),
            "native locator result did not select/focus the exact graph item");
    };
    locate(false,reportPlan->passes.size()-1);locate(true,0);image("located-resource");
    // A filtered view must export all nodes. JSON schema is parsed independently
    // by the Node verifier; the native clipboard action uses the same report.
    std::copy_n("No-such-UI-filter",sizeof("No-such-UI-filter"),panel.m_filter.begin());tick();
    ImGui::ActivateItemByID(ImHashStr("Copy JSON",0,window->ID));tick();
    Require(std::string(ImGui::GetClipboardText())==panel.BuildReport(),"Copy JSON did not export the actual full snapshot");
    std::string error;const auto reportFile=output/(threaded?"framegraph-render-report.json":"framegraph-main-report.json");
    Require(panel.SaveReport(reportFile,error)&&error.empty(),"report file export failed");
    Require(!panel.SaveReport(output/"missing-report-directory"/"report.json",error)&&!error.empty(),"failed export did not report its write error");
    auto escaped=std::make_shared<OvRendering::Data::FrameGraphDiagnostics>(*reportPlan);
    escaped->passes[0].name="quote \" slash \\ tab\t line\n control\x01 中文";panel.m_snapshot=escaped;
    Require(panel.SaveReport(output/(threaded?"framegraph-render-escaped.json":"framegraph-main-escaped.json"),error),"escaped report export failed");
    // An unrelated GPU sample must not be attributed to this snapshot.
    Require(panel.BuildReport().find("\"gpu\":null")!=std::string::npos,"export paired GPU costs with an unrelated plan");
    panel.m_snapshot=reportPlan;panel.m_filter[0]=0;panel.m_exportStatus.clear();panel.m_frozen=false;panel.m_requestedTab=0;tick();tick();
    std::printf("PASS: %s handoff diagnostics: live atomic memory/frozen resize, plan roles, folded controls, both expanded canvases, native Pass/resource locator, full clipboard/file JSON and write errors\n",threaded?"Render":"Main");
}
