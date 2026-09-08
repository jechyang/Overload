#define EDITOR_THREAD_TEST_MAIN UnusedEditorThreadMain
#include "editor_thread_validation.cpp"
#include "product_benchmark.inl"
int main(int argc,char** argv) try
{
    if(const auto result=EditorFixtureMain(argc,argv))return result;
    Benchmark::CaptureWait(argc,argv);
    const auto output=std::filesystem::path(argv[1]);const auto project=output/"SkinningProject";
    std::ofstream csv(output/Benchmark::MetricsFile(argc,argv));Benchmark::Header(csv);
    const char* names[]={"legacy-recording","serial-encoding","parallel-encoding"};
    for(int repeat=0;repeat<3;++repeat)for(int order=0;order<3;++order)
    {
        const int mode=repeat%2?2-order:order;
        Benchmark::Configure(project/"SkinningProject.ovproject",2);
        OvEditor::Core::Application app(project);auto& editor=app.m_editor;auto& context=app.m_context;
        ViewsFixture fixture(editor,context);editor.SetFrameSubmissionEnabled(true);editor.m_editorActions.StartPlaying();
        for(auto* view:fixture.views)
        {
            view->m_renderer->SetCommandRecordingJobs(mode?context.jobSystem.get():nullptr);
            view->m_renderer->SetParallelCommandRecordingEnabled(mode==2);
        }
        Benchmark::Run run(editor.m_recordedFrameID+1,192,false);
        std::vector<std::shared_ptr<OvEditor::Panels::SceneView::Interaction>> inputs(run.main.size());
        size_t applied=0;
        const auto measure=[&]{Benchmark::Measure(editor,context,run,[&](size_t i,bool updated){
            if(updated) inputs[i]=editor.m_recordedInteraction;
        },[&]{while(applied<inputs.size()&&inputs[applied]&&inputs[applied]->consumed)
            {run.main[applied].applied=Benchmark::Now();inputs[applied++].reset();}});};
        if(mode==0){ZoneScopedN("Stage4 Editor Legacy");measure();}
        else if(mode==1){ZoneScopedN("Stage4 Editor Serial");measure();}
        else {ZoneScopedN("Stage4 Editor Parallel");measure();}
        Benchmark::Write(csv,"editor-recording",names[mode],repeat,run,32);
        std::printf("BENCH: Editor recording %s repeat %d complete\n",names[mode],repeat);
    }
    std::puts("PASS: nine actual Editor recording benchmark runs, fixed Render owner and identical three-view workload");return 0;
}
catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
