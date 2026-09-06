#define main ProductFixtureMain
#ifdef PRODUCT_EDITOR
#include "../Skinning/editor_validation.cpp"
#include <OvEditor/Core/Application.h>
#include <OvEditor/Panels/Console.h>
#else
#include "game_frame_validation.cpp"
#include <OvGame/Core/Application.h>
#endif
#undef main
#include <OvDebug/Logger.h>
#include <thread>
int main(int argc,char** argv) try
{
    if(const int result=ProductFixtureMain(argc,argv))return result;
    OvRendering::Data::RenderFrame::Completion abandoned;
    auto listenerCounts=[] {return std::array{OvCore::ECS::Actor::CreatedEvent.GetListenerCount(),OvCore::ECS::Actor::DestroyedEvent.GetListenerCount(),OvCore::ECS::Actor::AttachEvent.GetListenerCount(),OvCore::ECS::Actor::DettachEvent.GetListenerCount()};};
    const auto listeners=listenerCounts();
    {
#ifdef PRODUCT_EDITOR
        OvEditor::Core::Application app(std::filesystem::path(argv[1])/"SkinningProject");
        auto& product=app.m_editor;auto& context=app.m_context;
#else
        OvGame::Core::Application app;auto& product=app.m_game;auto& context=app.m_context;
        int deliveredLogs=0;const auto mainThread=std::this_thread::get_id();
        const auto logListener=OvDebug::Logger::LogEvent += [&](const auto& data){if(data.message=="background product Game validation"){Require(std::this_thread::get_id()==mainThread,"Game log dispatched on worker");++deliveredLogs;}};
        std::thread logger([]{OVLOG_INFO("background product Game validation");});logger.join();
        Require(!deliveredLogs,"Game background notification ran before pump");product.PreUpdate();
        Require(deliveredLogs==1,"Game PreUpdate did not dispatch background log");OvDebug::Logger::LogEvent-=logListener;
        std::puts("PASS: real Game PreUpdate delivers worker log on main thread");
#endif
 #ifdef PRODUCT_EDITOR
        Require(context.sceneManager.LoadScene("crowd.ovscene"),"Application project scene load");product.m_editorActions.StartPlaying();
        auto& console=product.m_panelsManager.GetPanelAs<OvEditor::Panels::Console>("Console");
        const auto logsBefore=console.m_logs.size();
        std::thread logger([]{OVLOG_INFO("background product Console validation");});logger.join();
        Require(console.m_logs.size()==logsBefore,"worker log touched real Console widgets");
        product.PreUpdate();
        Require(console.m_logs.size()==logsBefore+1&&console.m_logs.back().data.message=="background product Console validation","Editor PreUpdate did not deliver background Console message");
        std::puts("PASS: real Console stays unchanged during worker logging and receives owning message in Editor PreUpdate");
 #endif
        Require(product.IsFrameSubmissionEnabled(),"Application did not enable the product command loop");
        {ProductFenceHook hook(context.window.get(),4);app.Run();Require(ProductFenceHook::count==4,"Application Run did not submit four frames");}
        Require(!app.IsRunning()&&!product.m_pendingFrame,"Application did not stop with drained frames");
        product.SetFrameSubmissionEnabled(false);context.window->SetShouldClose(false);
        for(int i=0;i<2;++i){product.PreUpdate();product.Update(.02f);product.PostUpdate();}
        product.SetFrameSubmissionEnabled(true);product.Update(.02f);Require(product.m_pendingFrame.has_value(),"shutdown fixture did not prepare a frame");abandoned=product.m_pendingFrame->GetCompletion();
    }
    Require(abandoned.IsRetired()&&abandoned.GetState()==OvRendering::Data::RenderFrame::State::CANCELLED,"Application destruction did not cancel prepared frame");
    Require(listenerCounts()==listeners,"Application leaked Actor destruction subscriptions");
#ifdef PRODUCT_EDITOR
    const auto settings=std::filesystem::path(argv[1])/"SkinningProject/SkinningProject.ovproject";
#else
    const auto settings=std::filesystem::path(argv[1])/"GameProject/Game.ini";
#endif
    std::ifstream saved(settings);const std::string original((std::istreambuf_iterator<char>(saved)),{});saved.close();
    std::ofstream(settings,std::ios::app)<<"render_commands=false\n";
    {
#ifdef PRODUCT_EDITOR
        OvEditor::Core::Application fallback(std::filesystem::path(argv[1])/"SkinningProject");Require(!fallback.m_editor.IsFrameSubmissionEnabled(),"Editor ignored immediate configuration");fallback.m_context.window->SetShouldClose(true);
#else
        OvGame::Core::Application fallback;Require(!fallback.m_game.IsFrameSubmissionEnabled(),"Game ignored immediate configuration");fallback.m_context.window->SetShouldClose(true);
#endif
        fallback.Run();
    }
    std::ofstream(settings)<<original;
    Require(listenerCounts()==listeners,"configured fallback leaked Actor subscriptions");
    std::puts("PASS: actual Application Run defaults to commands, submits/exits after four frames, switches/configures immediate and cancels prepared frame on destruction");return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
