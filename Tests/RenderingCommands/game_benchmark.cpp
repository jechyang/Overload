#define GAME_THREAD_TEST_MAIN UnusedGameThreadMain
#include "game_thread_validation.cpp"
#include "product_benchmark.inl"
int main(int argc,char** argv) try
{
    if(const auto result=GameFixtureMain(argc,argv))return result;
    Benchmark::CaptureWait(argc,argv);
    const auto output=std::filesystem::path(argv[1]);
    std::ofstream csv(output/Benchmark::MetricsFile(argc,argv));Benchmark::Header(csv);
    for(int repeat=0;repeat<3;++repeat)for(int order=0;order<3;++order)
    {
        const int mode=repeat%2?2-order:order;Benchmark::Configure(output/"GameProject/Game.ini",mode);
        OvGame::Core::Application app;auto& game=app.m_game;auto& context=app.m_context;
        game.m_showDebugInformation=true;ImGui::GetIO().ConfigInputTextCursorBlink=false;
        Benchmark::Run run(game.m_recordedFrameID+1,384,mode==0);
        Benchmark::MeasureCase(mode,[&]{Benchmark::Measure(game,context,run,[](size_t,bool){},[]{});});
        Benchmark::Write(csv,"game",Benchmark::modes[mode],repeat,run,64);
        std::printf("BENCH: Game %s repeat %d complete\n",Benchmark::modes[mode],repeat);
    }
    std::puts("PASS: nine actual Game benchmark runs completed with exact timing/retirement boundaries");return 0;
}
catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
