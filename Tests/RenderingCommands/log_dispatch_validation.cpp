#define main UIValidationMain
#include "ui_command_validation.cpp"
#undef main
#include <OvTools/Eventing/MainThreadEvent.h>
#include <OvDebug/Logger.h>
#include <OvRendering/Threading/RenderThread.h>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <set>

void EventProtocol()
{
    OvTools::Eventing::MainThreadEvent<std::string> event(3);
    std::vector<std::string> seen;const auto main=std::this_thread::get_id();
    auto listener=event += [&](const auto& value){Require(std::this_thread::get_id()==main,"callback ran on producer");seen.push_back(value);};
    std::thread worker([&]{std::string text="owned";event.Invoke(text);text="overwritten";Reject([&]{event.DispatchPending();},"worker pump accepted");Reject([&]{event.RemoveListener(listener);},"worker unsubscribe accepted");});worker.join();
    Require(seen.empty(),"background event touched listener");event.DispatchPending();Require(seen==std::vector<std::string>{"owned"},"event borrowed producer data");
    event.Post("old subscriber");event-=listener;int replacement=0;event += [&](const auto&){++replacement;};event.DispatchPending();Require(replacement==0,"replacement received previous subscription message");
    event.RemoveAllListeners();seen.clear();event += [&](const auto& value){seen.push_back(value);};
    for(int i=0;i<5;++i)event.Post(std::to_string(i));Require(event.GetPendingCount()==3&&event.TakeDroppedCount()==2,"event backlog not bounded/observable");event.DispatchPending();Require(seen==std::vector<std::string>{"2","3","4"},"overflow order changed");
    event.RemoveAllListeners();int skipped=0;OvTools::Eventing::ListenerID victim=0,self=0;
    self=event += [&](const auto&){event-=self;event-=victim;event.Post("nested");throw std::runtime_error("callback error");};
    victim=event += [&](const auto&){++skipped;};int survivor=0;event += [&](const auto&){++survivor;};
    Reject([&]{event.Invoke("outer");},"callback error disappeared");Require(skipped==0&&survivor==1&&event.GetPendingCount()==1,"callback mutation/finite batch failed");event.DispatchPending();Require(survivor==2,"pump did not recover after callback exception");
    std::puts("PASS: owning bounded events, main-only dispatch/subscription, generation-safe unsubscribe, callback mutation/exception and finite nested delivery");
}
void ConcurrentLogger(const std::filesystem::path& dir)
{
    using OvDebug::Logger;using OvDebug::ELogMode;
    std::vector<std::string> observed;
    const auto main=std::this_thread::get_id();
    const auto id=Logger::LogEvent += [&](const auto& data){Require(std::this_thread::get_id()==main,"logger callback ran on worker");observed.push_back(data.message);};
    constexpr int count=256, producers=6;
    std::vector<std::thread> workers;
    for(int w=0;w<producers;++w)workers.emplace_back([w]{for(int i=0;i<count;++i)OvDebug::Logger::Log(std::to_string(w)+":"+std::to_string(i),OvDebug::ELogLevel::LOG_INFO,ELogMode::HISTORY);});
    // Concurrent owning reads of HistoryHandler must not borrow its queue.
    std::atomic_bool stop=false;std::thread reader([&]{while(!stop.load())(void)OvDebug::HistoryHandler::GetLogQueue();});
    for(auto& worker:workers)worker.join();stop=true;reader.join();
    Require(observed.empty(),"logging mutated UI before main pump");Logger::DispatchPendingEvents();
    auto history=OvDebug::HistoryHandler::GetLogQueue();Require(history.size()==count*producers&&observed.size()==history.size(),"concurrent logs lost or duplicated");
    std::set<std::string> unique;std::array<int,producers> previous;previous.fill(-1);
    for(const auto& message:observed){Require(history.front().message==message,"sink/event FIFO differs");history.pop();unique.insert(message);const auto colon=message.find(':');const int w=std::stoi(message.substr(0,colon)),i=std::stoi(message.substr(colon+1));Require(i==++previous[w],"per-producer order changed");}
    Require(unique.size()==count*producers,"duplicate producer message");
    Logger::LogEvent-=id;
    size_t delivered=0,warnings=0;
    const auto overflow=Logger::LogEvent += [&](const auto& data){++delivered;if(data.message.starts_with("UI log notification queue skipped 904"))++warnings;};
    std::thread flood([]{for(int i=0;i<5000;++i)OvDebug::Logger::Log("overflow-"+std::to_string(i),OvDebug::ELogLevel::LOG_INFO,ELogMode::HISTORY);});flood.join();
    Require(Logger::LogEvent.GetPendingCount()==4096,"Logger queue is unbounded");Logger::DispatchPendingEvents();
    Require(delivered==4097&&warnings==1&&Logger::LogEvent.GetPendingCount()==0,"overflow warning missing or recursively flooded");Logger::LogEvent-=overflow;
    OvDebug::FileHandler::SetLogFilePath((dir/"threaded.log").string());const auto file=OvDebug::FileHandler::GetLogFilePath();
    std::filesystem::remove(file);
    workers.clear();for(int w=0;w<producers;++w)workers.emplace_back([w]{for(int i=0;i<32;++i)OvDebug::Logger::Log("line-"+std::to_string(w)+":"+std::to_string(i),OvDebug::ELogLevel::LOG_INFO,ELogMode::FILE);});for(auto& worker:workers)worker.join();
    std::ifstream input(file);std::string line;size_t lines=0;while(std::getline(input,line)){Require(line.find("line-")!=std::string::npos,"interleaved file output");++lines;}Require(lines==producers*32,"file sink lost messages");
    std::puts("PASS: six concurrent log producers, 1536 exact sink/event ordered messages, owning history snapshots, bounded 5000-message flood with visible loss count and 192 complete file records");
}
void RenderLogs()
{
    using OvDebug::Logger;
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=320;ws.height=240;ws.samples=1;OvWindowing::Window window(device,ws);glfwMakeContextCurrent(nullptr);
    const auto main=std::this_thread::get_id();int count=0;bool deleted=false;
    auto id=Logger::LogEvent += [&](const auto&){Require(std::this_thread::get_id()==main&&!deleted,"dead/off-thread UI notification");++count;};
    OvRendering::Threading::RenderThread thread([&]{window.MakeCurrentContext();return std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{true});},[]{glfwMakeContextCurrent(nullptr);});
    Require(count==0&&Logger::LogEvent.GetPendingCount()>0,"Driver initialization touched main subscriber");Logger::DispatchPendingEvents();Require(count>0,"Render startup logs missing");const int startup=count;
    thread.Invoke([](auto&){OVLOG_ERROR("Render error notification");});Require(count==startup,"Render task mutated UI");Logger::DispatchPendingEvents();Require(count==startup+1,"Render log not delivered");
    thread.Invoke([](auto&){OVLOG_INFO("message for old UI");});Logger::LogEvent-=id;deleted=true;
    int newCount=0;id=Logger::LogEvent += [&](const auto&){++newCount;};Logger::DispatchPendingEvents();Require(!newCount,"new UI received deleted subscriber's backlog");Logger::LogEvent-=id;thread.Shutdown();
    std::puts("PASS: real fixed Render Driver/task logs delivered only by main pump, pending messages survive UI unsubscribe without stale callback or subscription leakage");
}
int main(int argc,char** argv) try
{
    Require(argc>1,"output directory missing");std::setvbuf(stdout,nullptr,_IONBF,0);
    EventProtocol();ConcurrentLogger(argv[1]);RenderLogs();return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
