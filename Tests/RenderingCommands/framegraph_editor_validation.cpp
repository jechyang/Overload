#define EDITOR_THREAD_TEST_MAIN UnusedFrameGraphEditorThreadMain
#include "editor_thread_validation.cpp"
#include <OvEditor/Panels/FrameGraph.h>

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
        std::array<bool,3> rejected{};bool cpuRead=false;
        std::thread worker([&] {
            cpuRead=frozen->passes.back().name=="Window.Present"&&!glfwGetCurrentContext();
            try{panel.Update({},{});}catch(const std::logic_error&){rejected[0]=true;}
            try{frontend.GetFrameGraphDiagnostics();}catch(const std::logic_error&){rejected[1]=true;}
            try{frontend.SetDiagnosticsCaptureEnabled(true);}catch(const std::logic_error&){rejected[2]=true;}
        });worker.join();
        Require(cpuRead&&rejected[0]&&rejected[1]&&rejected[2],"diagnostic state escaped UI/producer ownership or frozen values required GL");
        panel.m_frozen=false;tick();tick();
        Require(panel.m_snapshot!=frozen&&panel.m_snapshot->captureIndex>frozen->captureIndex&&panel.m_snapshot->scope!=frozen->scope,"unfreezing did not resume fresh scoped capture");
        panel.m_requests.budget=128ull*1024*1024;tick();
        Require(pool->GetStatistics().idleBudget==128ull*1024*1024&&!panel.m_requests.budget,"panel budget request was not applied exactly once at the product boundary");
        const auto before=pool->GetStatistics();Require(before.idleTargets>0,"trim fixture has no retired scratch");
        panel.m_requests.trimIdle=true;tick();const auto after=pool->GetStatistics();
        Require(after.evictions>before.evictions&&after.allocations>before.allocations&&after.idleBudget==before.idleBudget&&!panel.m_requests.trimIdle,"panel trim did not evict idle scratch and rebuild the next product frame safely");
        panel.Close();Require(!panel.m_snapshot,"closing the panel kept its frozen values");tick();
        Require(!frontend.GetFrameGraphDiagnostics()&&!panel.WantsCapture(),"closed panel kept collecting diagnostics");
        std::printf("PASS: Frame Graph panel on %s owner: full/focused content versions, declared read/write links, v0/Load, version node click, aggregate resource flow, Pass dependency view, filter, live auto-expiry, freeze/resize, thread guards, budget/trim; %zu passes / %zu resources\n",threaded?"Render":"Main",initial->passes.size(),initial->resources.size());
    }
    return 0;
}
catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
