#define EDITOR_THREAD_TEST_MAIN UnusedEditorThreadMain
#include "editor_thread_validation.cpp"
#include "product_benchmark.inl"
int main(int argc,char** argv) try
{
    if(const auto result=EditorFixtureMain(argc,argv))return result;
    Benchmark::CaptureWait(argc,argv);
    const auto output=std::filesystem::path(argv[1]);const auto project=output/"SkinningProject";
    std::ofstream csv(output/Benchmark::MetricsFile(argc,argv));Benchmark::Header(csv);
    for(bool picking:{false,true})for(int repeat=0;repeat<3;++repeat)for(int order=0;order<3;++order)
    {
        const int mode=repeat%2?2-order:order;if(picking&&mode==0)continue;
        Benchmark::Configure(project/"SkinningProject.ovproject",mode);
        OvEditor::Core::Application app(project);auto& editor=app.m_editor;auto& context=app.m_context;
        ViewsFixture fixture(editor,context);editor.SetFrameSubmissionEnabled(mode!=0);editor.m_editorActions.StartPlaying();
        auto& sceneView=*static_cast<OvEditor::Panels::SceneView*>(fixture.views[2]);
        Benchmark::Run run(editor.m_recordedFrameID+1,picking?224:384,mode==0);
        std::vector<std::shared_ptr<OvEditor::Panels::SceneView::Interaction>> inputs(run.main.size());
        size_t applied=0;
        Benchmark::MeasureCase(mode,[&]{Benchmark::Measure(editor,context,run,[&](size_t i,bool updated){
            if(!updated&&picking)sceneView.m_hovered=true;
            if(updated)
            {
                if(mode==0)run.main[i].applied=run.main[i].updated;
                else
                {
                    inputs[i]=editor.m_recordedInteraction;
                    if(picking)
                    {
                        Require(inputs[i]&&inputs[i]->readback&&inputs[i]->hovered,"benchmark picking pass was not captured");
                        // Stable synthetic view input; never move the desktop cursor.
                        inputs[i]->mouse={inputs[i]->size.x*.5f,inputs[i]->size.y*.5f};
                        inputs[i]->resizing=false;
                    }
                }
            }
        },[&]{while(applied<inputs.size()&&inputs[applied]&&inputs[applied]->consumed)
        {run.main[applied].applied=Benchmark::Now();inputs[applied++].reset();}});});
        if(mode!=0)for(const auto& item:run.main)Require(item.applied!=0,"benchmark missed interaction application");
        Benchmark::Write(csv,picking?"editor-picking":"editor",Benchmark::modes[mode],repeat,run,picking?32:64);
        std::printf("BENCH: Editor %s %s repeat %d complete\n",picking?"picking":"views",Benchmark::modes[mode],repeat);
    }
    std::puts("PASS: fifteen actual Editor benchmark runs completed with exact timing/retirement boundaries");return 0;
}
catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
