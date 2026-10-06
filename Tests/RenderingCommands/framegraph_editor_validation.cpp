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
        panel.m_selectedPass=static_cast<size_t>(present-initial->passes.begin());tick();
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
        std::printf("PASS: Frame Graph panel on %s owner: default off, real product nodes, freeze/resume across resize, CPU-only values, thread guards, budget/trim, close; %zu passes / %zu resources\n",threaded?"Render":"Main",initial->passes.size(),initial->resources.size());
    }
    return 0;
}
catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
