#define EDITOR_THREAD_TEST_MAIN UnusedFrameGraphEditorThreadMain
#include "editor_thread_validation.cpp"
#include <OvEditor/Panels/FrameGraph.h>
#include <imgui_internal.h>

PFNGLGENQUERIESPROC timingGenerate;
std::atomic_size_t timingAllocations{0};
std::vector<GLuint> timingQueryIDs;
void APIENTRY FrameGraphGenerateQueries(GLsizei count,GLuint* ids)
{
    Require(std::this_thread::get_id()==renderOwner&&glfwGetCurrentContext(),"timing query allocation escaped the GL owner");
    timingGenerate(count,ids);timingAllocations+=count;timingQueryIDs.insert(timingQueryIDs.end(),ids,ids+count);
}

int main(int argc,char** argv) try
{
    producer=std::this_thread::get_id();
    // Keep the frozen product image and native/owned UI oracles independent of
    // the new panel fixture. Do not update the F0 baseline for this feature.
    if(const auto result=EditorFixtureMain(argc,argv))return result;
    const auto output=std::filesystem::path(argv[1]);
    const auto project=output/"SkinningProject";
    for(bool threaded:{false,true}) {
        SetEditorThreadMode(project,threaded);
        OvEditor::Core::Application app(project);
        auto& editor=app.m_editor;auto& context=app.m_context;
        ViewsFixture fixture(editor,context);OwnerChecks checks(context);
        auto& panel=editor.m_panelsManager.GetPanelAs<OvEditor::Panels::FrameGraph>("Frame Graph");
        auto& frontend=editor.m_productFrontend;auto pool=frontend.GetResourcePool();
        Require(!panel.IsOpened()&&!frontend.GetFrameGraphDiagnostics(),"diagnostics were active by default");
        context.window->SetSize(1280,860);
        for(size_t i=0;i<fixture.views.size();++i) {
            fixture.views[i]->SetPosition({0,float(20+i*275)});fixture.views[i]->SetSize({545,270});
        }
        panel.dockable=false;panel.autoSize=false;panel.SetPosition({550,20});panel.SetSize({720,825});
        fixture.canvas.AddPanel(panel);panel.Open();
        std::vector<uint8_t> pixels;
        OnOwner(context,[&](auto&){beforePresent=[&] {
            pixels.resize(1280*860*4);glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
            glReadPixels(0,0,1280,860,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        };});
        const auto tick=[&] {
            editor.PreUpdate();editor.Update(0);editor.PostUpdate();editor.DrainRenderFrames();
            OnOwner(context,[](auto&){Require(glGetError()==GL_NO_ERROR,"diagnostic panel introduced a GL error");});
        };
        for(int i=0;i<4;++i)tick();
        Require(panel.m_snapshot&&frontend.GetFrameGraphDiagnostics(),"opening the panel did not enable product capture");
        const auto initial=panel.m_snapshot;
        Require(initial->passes.size()>20&&initial->resources.size()>10&&initial->physicalTransientTargets>0&&initial->physicalTransientBytes>0,"panel did not capture the actual multiview product graph");
        const auto present=std::find_if(initial->passes.begin(),initial->passes.end(),[](const auto& pass){return pass.name=="Window.Present";});
        Require(present!=initial->passes.end(),"diagnostic capture missed final window presentation");
        const auto passNodes=[&]{return static_cast<size_t>(std::count_if(panel.m_graphNodes.begin(),panel.m_graphNodes.end(),[](const auto& node){return !node.resource;}));};
        const auto resourceNodes=[&]{return static_cast<size_t>(std::count_if(panel.m_graphNodes.begin(),panel.m_graphNodes.end(),[](const auto& node){return node.resource;}));};
        Require(panel.m_graphResourceFlow&&panel.m_graphVersions&&panel.m_graphFocus&&passNodes()>0&&resourceNodes()>0&&passNodes()<initial->passes.size()&&
            std::any_of(panel.m_graphEdges.begin(),panel.m_graphEdges.end(),[](const auto& edge){return edge.read;})&&
            std::any_of(panel.m_graphEdges.begin(),panel.m_graphEdges.end(),[](const auto& edge){return edge.write;}),
            "resource flow did not open on a readable product cluster with separate read/write resource links");
        const size_t focusedPass=panel.m_selectedPass,focusedResource=panel.m_graphEdges.front().resource;
        panel.m_selectedResource=focusedResource;panel.m_flowSelectionIsResource=true;panel.m_graphFit=true;tick();
        Require(resourceNodes()>0&&std::any_of(panel.m_graphNodes.begin(),panel.m_graphNodes.end(),[&](const auto& node){return node.resource&&node.index==focusedResource&&node.version==panel.m_selectedVersion;})&&
            std::any_of(panel.m_graphEdges.begin(),panel.m_graphEdges.end(),[&](const auto& edge){return edge.resource==focusedResource;}),
            "selecting a resource did not focus its connected pass/resource flow");
        panel.m_selectedPass=focusedPass;panel.m_flowSelectionIsResource=false;panel.m_graphFit=true;tick();
        panel.m_graphFocus=false;panel.m_graphFit=true;tick();tick();
        Require(passNodes()==initial->passes.size(),"version view lost product Pass nodes");
        for(const auto& edge:panel.m_graphEdges) {
            const auto& pass=panel.m_snapshot->passes[edge.pass];bool declared=false;
            for(size_t a=0;a<pass.accesses.size();++a)if(pass.accesses[a].resource.index==edge.resource) {
                const auto& v=pass.contentVersions[a];declared|=(edge.read&&v.read==edge.version)||(edge.write&&v.write==edge.version);
            }
            Require(declared&&std::any_of(panel.m_graphNodes.begin(),panel.m_graphNodes.end(),[&](const auto& node){return node.resource&&node.index==edge.resource&&node.version==edge.version;}),
                "version graph link has no declared input/output version or matching resource node");
        }
        Require(std::any_of(panel.m_graphEdges.begin(),panel.m_graphEdges.end(),[](const auto& edge){return edge.read&&edge.version==0;})&&
            std::any_of(panel.m_graphEdges.begin(),panel.m_graphEdges.end(),[](const auto& edge){return edge.write&&edge.load&&edge.version>1;}),
            "real product graph did not show initial content and later Load output versions");
        panel.m_graphVersions=false;panel.m_graphFit=true;tick();
        Require(passNodes()==initial->passes.size()&&resourceNodes()==initial->resources.size(),"full resource-flow view did not render every product pass and resource node");
        panel.m_graphVersions=true;
        panel.m_graphResourceFlow=false;panel.m_graphFit=true;tick();
        Require(passNodes()==initial->passes.size(),"Pass dependency view did not remain available beside resource flow");
        panel.m_selectedPass=static_cast<size_t>(present-initial->passes.begin());tick();
        panel.m_requestedTab=0;tick();
        std::copy_n("Window.Present",sizeof("Window.Present"),panel.m_filter.begin());panel.m_selectedPass=0;tick();
        Require(panel.m_snapshot->passes[panel.m_selectedPass].name=="Window.Present","pass filter left details on a hidden selection");
        std::copy_n("does-not-exist",sizeof("does-not-exist"),panel.m_filter.begin());tick();panel.m_filter[0]=0;tick();
        const auto image=[&](const char* tab) {
            const auto name=std::string("framegraph-")+(threaded?"render":"main")+"-"+tab;
            std::ofstream file(output/(name+".rgba"),std::ios::binary);
            file.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
            std::ofstream(output/(name+"-size.txt"))<<1280<<' '<<860;
        };
        image("passes");
        panel.m_requestedTab=1;panel.m_transientOnly=true;tick();tick();image("resources");
        Require(panel.m_snapshot->resources[panel.m_selectedResource].origin==OvRendering::Data::ResourceOrigin::Transient,"transient filter left details on an imported resource");
        panel.m_filter[0]=0;panel.m_graphFocus=true;panel.m_graphResourceFlow=true;panel.m_flowSelectionIsResource=false;
        const auto composite=std::find_if(panel.m_snapshot->passes.begin(),panel.m_snapshot->passes.end(),[](const auto& pass){return pass.name.find("Bloom")!=std::string::npos;});
        if(composite!=panel.m_snapshot->passes.end())panel.m_selectedPass=static_cast<size_t>(composite-panel.m_snapshot->passes.begin());
        panel.m_graphFit=true;panel.m_requestedTab=2;tick();tick();image("dependencies-focused");
        Require(passNodes()>0&&passNodes()<panel.m_snapshot->passes.size()&&resourceNodes()>0&&
            std::any_of(panel.m_graphNodes.begin(),panel.m_graphNodes.end(),[&](const auto& node){return !node.resource&&node.index==panel.m_selectedPass;}),
            "selected dependency focus omitted the selected pass or failed to reduce an actual product graph");
        const auto loadPass=std::find_if(panel.m_snapshot->passes.begin(),panel.m_snapshot->passes.end(),[](const auto& pass){
            return pass.name.find("Bloom")!=std::string::npos&&std::any_of(pass.contentVersions.begin(),pass.contentVersions.end(),[](const auto& v){return v.read&&v.write;});
        });
        Require(loadPass!=panel.m_snapshot->passes.end(),"version UI fixture has no Bloom Load access");
        const auto loadAccess=static_cast<size_t>(std::find_if(loadPass->contentVersions.begin(),loadPass->contentVersions.end(),[](const auto& v){return v.read&&v.write;})-loadPass->contentVersions.begin());
        panel.m_selectedResource=loadPass->accesses[loadAccess].resource.index;panel.m_selectedVersion=*loadPass->contentVersions[loadAccess].write;
        panel.m_flowSelectionIsResource=true;panel.m_graphFit=true;tick();tick();image("versions-resource");
        const auto versionNode=std::find_if(panel.m_graphNodes.begin(),panel.m_graphNodes.end(),[&](const auto& node){return node.resource&&node.index==panel.m_selectedResource&&node.version==panel.m_selectedVersion;});
        Require(versionNode!=panel.m_graphNodes.end(),"focused version node disappeared");
        // Exercise real ImGui node hit testing instead of changing the selected version directly.
        const auto rect=versionNode->rect;const auto clickedVersion=versionNode->version;auto& io=ImGui::GetIO();io.AddMousePosEvent((rect[0]+rect[2])*.5f,(rect[1]+rect[3])*.5f);
        panel.m_selectedVersion=0;io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();
        Require(panel.m_flowSelectionIsResource&&panel.m_selectedVersion==clickedVersion,"clicking a version node did not select its content state");
        io.AddMousePosEvent(-100,-100);tick();
        std::copy_n("Bloom",sizeof("Bloom"),panel.m_filter.begin());panel.m_graphFit=true;tick();image("dependencies-filtered");
        Require(!panel.m_graphNodes.empty()&&std::any_of(panel.m_graphNodes.begin(),panel.m_graphNodes.end(),[&](const auto& node){
            if(node.resource)return panel.m_snapshot->resources[node.index].name.find("Bloom")!=std::string::npos;
            const auto& pass=panel.m_snapshot->passes[node.index];
            return pass.name.find("Bloom")!=std::string::npos||std::any_of(pass.accesses.begin(),pass.accesses.end(),[&](const auto& access){return panel.m_snapshot->resources[access.resource.index].name.find("Bloom")!=std::string::npos;});
        }),"graph pass/resource filter hid all matching nodes");
        panel.m_filter[0]=0;panel.m_graphFocus=false;
        panel.m_requests.unusedFrameLimit=0;tick();Require(pool->GetStatistics().unusedFrameLimit==0,"panel did not disable automatic unused-frame expiry");
        panel.m_requests.unusedFrameLimit=60;tick();Require(pool->GetStatistics().unusedFrameLimit==60,"panel did not restore automatic unused-frame expiry");
        panel.m_frozen=true;const auto frozen=panel.m_snapshot;tick();
        fixture.views[1]->SetSize({520,250});tick();
        Require(panel.m_snapshot==frozen&&!frontend.GetFrameGraphDiagnostics(),"freeze changed the captured graph or left live capture enabled");
        std::array<bool,10> rejected{};bool cpuRead=false;
        std::thread worker([&] {
            cpuRead=frozen->passes.back().name=="Window.Present"&&!glfwGetCurrentContext();
            try{panel.Update({},{});}catch(const std::logic_error&){rejected[0]=true;}
            try{frontend.GetFrameGraphDiagnostics();}catch(const std::logic_error&){rejected[1]=true;}
            try{frontend.SetDiagnosticsCaptureEnabled(true);}catch(const std::logic_error&){rejected[2]=true;}
            try{frontend.GetFrameGraphPreview();}catch(const std::logic_error&){rejected[3]=true;}
            try{frontend.RequestFrameGraphPreview({},{});}catch(const std::logic_error&){rejected[4]=true;}
            try{frontend.ClearFrameGraphPreview();}catch(const std::logic_error&){rejected[5]=true;}
            try{panel.UpdatePreview({});}catch(const std::logic_error&){rejected[6]=true;}
            try{frontend.GetFrameGraphGpuTimings();}catch(const std::logic_error&){rejected[7]=true;}
            try{frontend.SetGpuTimingCaptureEnabled(true);}catch(const std::logic_error&){rejected[8]=true;}
            try{panel.UpdateGpuTimings({});}catch(const std::logic_error&){rejected[9]=true;}
        });worker.join();
        Require(cpuRead&&std::all_of(rejected.begin(),rejected.end(),[](bool value){return value;}),"diagnostic state escaped UI/producer ownership or frozen values required GL");
        bool invalidCompletion=false;
        try{frontend.RequestFrameGraphPreview({},{});}catch(const std::invalid_argument&){invalidCompletion=true;}
        Require(invalidCompletion,"preview accepted an absent frame completion");
        panel.m_frozen=false;tick();tick();
        Require(panel.m_snapshot!=frozen&&panel.m_snapshot->captureIndex>frozen->captureIndex&&panel.m_snapshot->scope!=frozen->scope,"unfreezing did not resume fresh scoped capture");
        const auto previewResource=std::find_if(panel.m_snapshot->resources.begin(),panel.m_snapshot->resources.end(),[](const auto& r){return r.name=="PostProcess.Tonemapping";});
        Require(previewResource!=panel.m_snapshot->resources.end(),"product fixture has no tonemapped texture to preview");
        panel.m_selectedResource=static_cast<size_t>(previewResource-panel.m_snapshot->resources.begin());panel.m_selectedVersion=previewResource->finalVersion;
        panel.m_previewChannel=0;panel.m_previewMip=panel.m_previewLayer=0;panel.m_requestedTab=3;tick();tick();
        const auto activatePreviewButton=[&](const char* label) {
            const auto window=ImGui::FindWindowByName((panel.name+panel.GetPanelID()).c_str());
            Require(window!=nullptr,"Frame Graph ImGui window is unavailable");
            const auto tabs=ImHashStr("graph-tabs",0,window->ID),preview=ImHashStr("Preview",0,tabs);
            ImGui::ActivateItemByID(ImHashStr(label,0,preview));
        };
        const auto readProductViews=[&] {
            std::vector<std::vector<uint8_t>> bytes;
            OnOwner(context,[&](auto&) {for(auto* view:fixture.views) {
                const auto target=view->GetOutputTarget();const auto [width,height]=target.GetSize();
                bytes.emplace_back(size_t(width)*height*4);target.ReadRGBA8(bytes.back());
            }});return bytes;
        };
        const auto productBeforePreview=readProductViews();
        activatePreviewButton("Capture");
        tick();Require(!panel.m_previewImage,"preview image was published before its frame reached GPU completion");
        tick();tick();
        Require(panel.m_previewImage&&panel.m_previewImage->width<=512&&panel.m_previewImage->height<=512&&pool->GetStatistics().leasedTargets==0,
            "one-shot product preview did not publish a bounded image without source leases");
        Require(readProductViews()==productBeforePreview,"resource preview changed product viewport pixels");
        image("preview-color");
        const auto captured=panel.m_previewImage;
        const auto readPreview=[&] {
            std::vector<uint8_t> bytes(captured->width*captured->height*4);
            OnOwner(context,[&](auto&){glGetTextureImage(captured->texture->GetID(),0,GL_RGBA,GL_UNSIGNED_BYTE,static_cast<GLsizei>(bytes.size()),bytes.data());});return bytes;
        };
        const auto capturedBytes=readPreview();panel.m_frozen=true;tick();fixture.views[1]->SetSize({500,240});tick();
        Require(panel.m_previewImage==captured&&readPreview()==capturedBytes&&!frontend.GetFrameGraphPreview(),"freeze/resize changed the captured image or kept a frontend source capture");
        activatePreviewButton("Capture");tick();Require(!panel.m_previewRequest&&!frontend.GetFrameGraphPreview()&&panel.m_previewImage==captured,"frozen plan allowed a new preview capture");
        panel.m_frozen=false;fixture.views[1]->SetSize({545,270});tick();tick();
        const auto oldSequence=panel.m_previewSequence;
        {
            OvRendering::Data::RenderFrame cancelled;cancelled.Cancel();
            auto ticket=std::make_shared<OvCore::Rendering::RenderFrontend::PreviewTicket>();ticket->sequence=oldSequence+1;
            ticket->image=std::make_shared<OvRendering::Data::FrameGraphPreviewImage>(*captured);ticket->completion=cancelled.GetCompletion();
            panel.UpdatePreview(ticket);Require(panel.m_previewImage==captured&&panel.m_previewStatus.find("cancelled")!=std::string::npos,"cancelled preview replaced the last successful image");
        }
        {
            OvRendering::Data::RenderCommandList commands;commands.Append([](auto&){throw std::runtime_error("Preview frame failure");});commands.Seal();
            OvRendering::Data::RenderFrame failed;failed.Append(std::move(commands));failed.Seal();OnOwner(context,[&](auto& driver){failed.Submit(driver);failed.Wait();});
            auto ticket=std::make_shared<OvCore::Rendering::RenderFrontend::PreviewTicket>();ticket->sequence=oldSequence+2;
            ticket->image=std::make_shared<OvRendering::Data::FrameGraphPreviewImage>(*captured);ticket->completion=failed.GetCompletion();panel.UpdatePreview(ticket);
            Require(panel.m_previewImage==captured,"failed preview frame published a speculative image");
        }
        panel.m_previewSequence=oldSequence;
        auto& debugger=editor.m_panelsManager.GetPanelAs<OvEditor::Panels::TextureDebugger>("Texture Debugger");
        activatePreviewButton("Texture Debugger");tick();debugger.Update(0);
        Require(debugger.IsOpened()&&debugger.m_capture==captured&&debugger.m_textureSelector.currentChoice==-1&&debugger.m_image.textureID.storage==captured->texture->RetainStorage(),
            "Frame Graph preview did not open the same retained image in Texture Debugger");
        activatePreviewButton("Clear image");tick();
        Require(!panel.m_previewImage&&!panel.m_previewRequest&&!panel.m_previewTicket&&!frontend.GetFrameGraphPreview()&&debugger.m_capture==captured,
            "clearing Frame Graph did not release preview state or invalidated an open debugger image");
        debugger.Close();Require(!debugger.m_capture,"closing Texture Debugger retained a captured preview");
        Require(!panel.WantsGpuTimings()&&!frontend.GetFrameGraphGpuTimings(),"GPU timing was enabled without user selection");
        OnOwner(context,[](auto&){timingGenerate=glad_glGenQueries;glad_glGenQueries=FrameGraphGenerateQueries;timingAllocations=0;timingQueryIDs.clear();});
        panel.m_requestedTab=4;tick();tick();
        const auto activateTimingItem=[&](const char* label) {
            const auto window=ImGui::FindWindowByName((panel.name+panel.GetPanelID()).c_str());
            const auto tabs=ImHashStr("graph-tabs",0,window->ID),tab=ImHashStr("Timings",0,tabs);
            ImGui::ActivateItemByID(ImHashStr(label,0,tab));
        };
        const auto productBeforeTiming=readProductViews();activateTimingItem("Capture GPU timings");tick();
        Require(panel.WantsGpuTimings()&&!panel.m_gpuTimings,"GPU timings did not enable on the product boundary or published before retirement");
        for(int i=0;i<15;++i)tick();
        Require(bool(panel.m_gpuTimings),"GPU timings did not publish a completed product sample");
        Require(panel.m_gpuTimings->graph->passes.size()==panel.m_gpuTimings->passNanoseconds.size()&&panel.m_gpuTimings->totalNanoseconds>0,"GPU timing sample had partial/empty Pass results");
        Require(panel.m_gpuTimings->graph->captureIndex<panel.m_snapshot->captureIndex,"timing fixture did not advance the live plan beyond its measured sample");
        Require(readProductViews()==productBeforeTiming,"GPU timing changed product viewport pixels");
        const auto queries=timingAllocations.load();
        for(int i=0;i<16;++i)tick();
        Require(queries>0&&timingAllocations==queries&&queries<=OvRendering::Data::FrameGraphGpuTimer::MaxPending*OvRendering::Data::FrameGraphGpuTimer::MaxPasses*2,"warm GPU timing kept allocating queries or exceeded capacity");
        auto& input=ImGui::GetIO();input.AddMousePosEvent(-100,-100);input.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();
        const auto held=panel.m_gpuTimings;
        auto incoming=std::make_shared<OvRendering::Data::FrameGraphGpuTimings>(*held);incoming->passNanoseconds[0]+=1000000;incoming->totalNanoseconds+=1000000;
        panel.UpdateGpuTimings(incoming);Require(panel.m_gpuTimings==held&&!panel.m_frozen,"live timing rows changed during a held mouse press");
        input.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();tick();
        image("timings");
        // Click the first visible hotspot in the actual ImGui scrolling table.
        const auto window=ImGui::FindWindowByName((panel.name+panel.GetPanelID()).c_str());
        const auto tab=ImHashStr("Timings",0,ImHashStr("graph-tabs",0,window->ID));
        const auto table=ImGui::GetCurrentContext()->Tables.GetByKey(ImHashStr("timing-table",0,tab));
        Require(table!=nullptr,"GPU hotspot table was not rendered");
        input.AddMousePosEvent(table->Columns[0].MinX+20,table->OuterRect.Min.y+ImGui::GetTextLineHeightWithSpacing()*1.5f);
        input.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();input.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();input.AddMousePosEvent(-100,-100);
        const auto measured=panel.m_gpuTimings;const auto hottest=static_cast<size_t>(std::max_element(measured->passNanoseconds.begin(),measured->passNanoseconds.end())-measured->passNanoseconds.begin());
        Require(panel.m_frozen&&panel.m_snapshot==measured->graph&&panel.m_selectedPass==hottest&&!frontend.GetFrameGraphGpuTimings(),"clicking the hottest Pass did not freeze its measured plan or disable queries");
        OnOwner(context,[](auto&){for(const auto id:timingQueryIDs)Require(!glIsQuery(id),"freezing retained timing query objects");});
        tick();tick();
        const auto tabs=ImGui::GetCurrentContext()->TabBars.GetByKey(ImHashStr("graph-tabs",0,window->ID));
        Require(tabs&&tabs->SelectedTabId==ImHashStr("Passes",0,tabs->ID),"hotspot click did not open actual Pass details");
        image("timings-pass");fixture.views[1]->SetSize({530,255});tick();
        Require(panel.m_gpuTimings==measured&&panel.m_snapshot==measured->graph,"resize replaced frozen timing values or their measured graph");
        panel.m_frozen=false;fixture.views[1]->SetSize({545,270});panel.m_requestedTab=4;tick();tick();
        activateTimingItem("Slowest first");tick();Require(!panel.m_slowestFirst,"hotspot order could not switch back to plan order");image("timings-order");
        activateTimingItem("Capture GPU timings");tick();Require(!panel.WantsGpuTimings()&&!frontend.GetFrameGraphGpuTimings(),"disabling timing kept collecting or retaining query pools");
        OnOwner(context,[](auto&){for(const auto id:timingQueryIDs)Require(!glIsQuery(id),"disabled timing retained query objects");glad_glGenQueries=timingGenerate;});
        panel.m_requests.budget=128ull*1024*1024;tick();
        Require(pool->GetStatistics().idleBudget==128ull*1024*1024&&!panel.m_requests.budget,"panel budget request was not applied exactly once at the product boundary");
        const auto before=pool->GetStatistics();Require(before.idleTargets>0,"trim fixture has no retired scratch");
        panel.m_requests.trimIdle=true;tick();const auto after=pool->GetStatistics();
        Require(after.evictions>before.evictions&&after.allocations>before.allocations&&after.idleBudget==before.idleBudget&&!panel.m_requests.trimIdle,"panel trim did not evict idle scratch and rebuild the next product frame safely");
        panel.Close();Require(!panel.m_snapshot,"closing the panel kept its frozen values");tick();
        Require(!panel.m_previewImage&&!frontend.GetFrameGraphPreview(),"closed Frame Graph retained preview state");
        Require(!panel.m_gpuTimings&&!frontend.GetFrameGraphGpuTimings()&&!panel.WantsGpuTimings(),"closed Frame Graph retained timing state");
        Require(!frontend.GetFrameGraphDiagnostics()&&!panel.WantsCapture(),"closed panel kept collecting diagnostics");
        std::printf("PASS: Frame Graph panel on %s owner: versions, preview, timing enable/retirement/matched graph, warm query reuse, actual hotspot click/freeze/resize, plan ordering, disable/close, exact product pixels, thread guards and budget/trim; %zu passes / %zu resources\n",threaded?"Render":"Main",initial->passes.size(),initial->resources.size());
    }
    return 0;
}
catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
