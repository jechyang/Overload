#define main UIValidationMain
#include "ui_command_validation.cpp"
#undef main
#include <OvRendering/Threading/RenderThread.h>
#include <condition_variable>
#include <mutex>
#include <future>
#include <chrono>
using RenderThread = OvRendering::Threading::RenderThread;
using RenderFrame = OvRendering::Data::RenderFrame;
using List = OvRendering::Data::RenderCommandList;
using namespace std::chrono_literals;

struct Hooks
{
    inline static std::thread::id owner;
    inline static PFNGLCLIENTWAITSYNCPROC wait;
    inline static PFNGLDELETETEXTURESPROC erase;
    inline static bool timeout=false, fail=false;
    inline static int wrongThread=0, deletes=0;
    static GLenum APIENTRY Wait(GLsync sync, GLbitfield flags, GLuint64 ns)
    {
        if (std::this_thread::get_id()!=owner || flags || ns) ++wrongThread;
        return fail?GL_WAIT_FAILED:timeout?GL_TIMEOUT_EXPIRED:wait(sync,flags,ns);
    }
    static void APIENTRY Delete(GLsizei n,const GLuint* ids)
    { if(std::this_thread::get_id()!=owner)++wrongThread;deletes+=n;erase(n,ids); }
    Hooks()
    {
        owner=std::this_thread::get_id();timeout=fail=false;wrongThread=deletes=0;
        wait=glad_glClientWaitSync;erase=glad_glDeleteTextures;
        glad_glClientWaitSync=Wait;glad_glDeleteTextures=Delete;
    }
    ~Hooks(){glad_glClientWaitSync=wait;glad_glDeleteTextures=erase;}
};
struct Gate
{
    std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;
    void Block(){std::unique_lock lock(mutex);entered=true;changed.notify_all();changed.wait(lock,[&]{return released;});}
    void Await(){std::unique_lock lock(mutex);Require(changed.wait_for(lock,10s,[&]{return entered;}),"owner gate did not start");}
    void Release(){std::lock_guard lock(mutex);released=true;changed.notify_all();}
};
template<class F> RenderFrame FrameWork(F&& fn)
{List list;list.Append(std::forward<F>(fn));list.Seal();RenderFrame frame;frame.Append(std::move(list));frame.Seal();return frame;}

void Images(RenderThread& thread)
{
    for(int i=0;i<8;++i)
    {
        std::vector<uint8_t> expected,actual;std::weak_ptr<baregl::Texture> weak;
        auto frame=thread.Invoke([&](auto&) {
            Require(thread.IsOwnerThread()&&glfwGetCurrentContext(),"resource creation without owner context");
            auto icon=Icon();weak=icon;BeginContext();auto& source=Frame(icon->GetID(),i,true);
            Clear();ImGui_ImplOpenGL3_RenderDrawData(&source);expected=Pixels();
            ImGui_ImplOpenGL3_DestroyDeviceObjects();ImGui_ImplOpenGL3_NewFrame();
            DrawDataRecorder recorder;std::array owners{icon};auto data=std::make_shared<DrawData>(recorder.Capture(source,owners));
            EndContext(&recorder);
            return FrameWork([data](auto&){Clear();data->Execute();});
        });
        Require(glfwGetCurrentContext()==nullptr,"main acquired render context");
        auto done=thread.Submit(std::move(frame),[&]{Require(thread.IsOwnerThread(),"readback on producer");actual=Pixels();glfwSwapBuffers(glfwGetCurrentContext());});
        thread.WaitIdle();done.RethrowIfFailed();
        Require(done.Succeeded()&&done.PresentationRequested()&&weak.expired()&&actual==expected,"threaded owning UI image/retirement mismatch");
    }
    thread.Invoke([&](auto&){Require(!Hooks::wrongThread&&Hooks::deletes>0&&glGetError()==GL_NO_ERROR,"GL owner/deletion/poll contract violated");});
    std::puts("PASS: 8 exact real GL UI images, destroyed source context, resource upload/draw/readback/delete on fixed Render owner");
}
void Capacity(RenderThread& thread)
{
    Gate gate;
    auto first=FrameWork([&](auto&){gate.Block();Clear();});
    auto a=thread.Submit(std::move(first));gate.Await();
    auto second=FrameWork([](auto&){Clear();});auto b=thread.Submit(std::move(second));
    auto third=FrameWork([](auto&){Clear();});auto original=third.GetCompletion();
    std::promise<void> trying;auto attempted=trying.get_future();
    auto producer=std::async(std::launch::async,[&]{trying.set_value();try{thread.Submit(std::move(third));return false;}catch(const std::logic_error&){return true;}});
    attempted.wait();const bool blocked=producer.wait_for(50ms)==std::future_status::timeout;
    thread.Close();const bool woke=producer.wait_for(5s)==std::future_status::ready;
    gate.Release();Require(woke&&producer.get()&&blocked,"bounded admission/Close wake failed");
    Require(third.GetCompletion()==original&&original.GetState()==RenderFrame::State::READY,"rejected frame was consumed");
    thread.Discard(std::move(third));thread.WaitIdle();
    Require(a.Succeeded()&&b.Succeeded()&&original.GetState()==RenderFrame::State::CANCELLED,"closed queue failed to drain/discard");
    std::puts("PASS: nonblocking producer during Render work, bounded two-frame admission, blocked producer wake, untouched rejection and owner discard");
}
void GPUCapacity(RenderThread& thread)
{
    thread.Invoke([](auto&){Hooks::timeout=true;});
    auto a=thread.Submit(FrameWork([](auto&){Clear();}));auto b=thread.Submit(FrameWork([](auto&){Clear();}));
    thread.Invoke([&](auto&){Require(a.GetState()==RenderFrame::State::SUBMITTED&&b.GetState()==RenderFrame::State::SUBMITTED,"queue waited for predecessor GPU");});
    auto producer=std::async(std::launch::async,[&]{return thread.Submit(FrameWork([](auto&){Clear();}));});
    const bool blocked=producer.wait_for(50ms)==std::future_status::timeout;
    thread.Invoke([](auto&){Hooks::timeout=false;});
    auto c=producer.get();thread.WaitIdle();Require(blocked&&a.Succeeded()&&b.Succeeded()&&c.Succeeded(),"GPU in-flight frames did not retain capacity");
    std::puts("PASS: two GPU in-flight frames, successor CPU submission before prior retirement, capacity released only after GPU retirement");
}
void Failure(RenderThread& thread,int mode)
{
    if(mode==2)
    {
        thread.Invoke([](auto&){Hooks::timeout=true;});
        List::History history;
        auto prepare=[&]{List list;list.UseHistory(history);list.Append([](auto&){Clear();});list.Seal();RenderFrame frame;frame.Append(std::move(list));frame.Seal();return frame;};
        auto a=thread.Submit(prepare());auto b=thread.Submit(prepare());
        thread.Invoke([&](auto&){Require(a.GetState()==RenderFrame::State::SUBMITTED&&b.GetState()==RenderFrame::State::SUBMITTED,"late-failure fixture did not submit both");Hooks::fail=true;});
        thread.WaitIdle();Require(a.GetState()==RenderFrame::State::FAILED&&b.GetState()==RenderFrame::State::FAILED,"GPU late failure published successful successor");
        Reject([&]{b.RethrowIfFailed();},"successor error missing");
        List rebuilt;Require(rebuilt.UseHistory(history),"GPU failed successors did not invalidate speculative history");rebuilt.Cancel();
    }
    else
    {
        Gate gate;int issued=0;
        auto a=thread.Submit(FrameWork([&](auto&){gate.Block();if(!mode)throw std::runtime_error("command failure");}),[&]{throw std::runtime_error("present failure");});
        gate.Await();auto b=thread.Submit(FrameWork([&](auto&){++issued;}));gate.Release();thread.WaitIdle();
        Require(a.GetState()==RenderFrame::State::FAILED&&b.GetState()==RenderFrame::State::CANCELLED&&!issued,"failure did not cancel queued successor");
    }
    Reject([&]{thread.RethrowIfFailed();},"queue failure missing");
    auto rejected=FrameWork([](auto&){});Reject([&]{thread.Submit(std::move(rejected));},"failed queue admitted frame");thread.Discard(std::move(rejected));
    thread.Invoke([](auto&){Hooks::fail=Hooks::timeout=false;Require(!Hooks::wrongThread,"failed fence handling violated owner");});
    std::puts("PASS: frame failure closes admission, queued cancellation/submitted successor invalidation, GPU drain and cleanup tasks remain available");
}
void TaskCapacity(RenderThread& thread)
{
    Gate gate;
    auto first=std::async(std::launch::async,[&]{thread.Invoke([&](auto&){gate.Block();});});gate.Await();
    auto second=std::async(std::launch::async,[&]{try{thread.Invoke([](auto&){});return false;}catch(const std::logic_error&){return true;}});
    const bool blocked=second.wait_for(50ms)==std::future_status::timeout;
    auto shutdown=std::async(std::launch::async,[&]{thread.Shutdown();});
    const bool woke=second.wait_for(5s)==std::future_status::ready;
    gate.Release();first.get();shutdown.get();Require(blocked&&woke&&second.get(),"task capacity or shutdown wake failed");
    std::puts("PASS: bounded active resource task, blocked task rejected at shutdown, accepted task drained before owner/context exit");
}
int main() try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=320;ws.height=240;ws.samples=1;
    OvWindowing::Window window(device,ws);glfwMakeContextCurrent(nullptr);
    bool cleaned=false;Reject([&]{RenderThread invalid([&]()->std::unique_ptr<OvRendering::Context::Driver>{window.MakeCurrentContext();throw std::runtime_error("startup");},[&]{cleaned=true;glfwMakeContextCurrent(nullptr);});},"initialization failure lost");Require(cleaned,"startup cleanup missing");
    for(int mode=0;mode<5;++mode)
    {
        std::unique_ptr<Hooks> hooks;bool detached=false;
        RenderThread thread([&]{window.MakeCurrentContext();auto driver=std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{true});hooks=std::make_unique<Hooks>();return driver;},
            [&]{hooks.reset();glfwMakeContextCurrent(nullptr);detached=true;},2,1);
        for(bool fail:{false,true})
        {
            auto texture=thread.Invoke([](auto&){return Icon();});std::weak_ptr<baregl::Texture> weak=texture;
            auto work=[texture=std::move(texture),fail](auto&){if(fail)throw std::runtime_error("captured resource task");};
            if(fail)Reject([&]{thread.Invoke(std::move(work));},"captured task exception missing");else thread.Invoke(std::move(work));
            Require(weak.expired(),"resource task returned before releasing its captures");
        }
        Reject([&]{thread.Invoke([](auto&){throw std::runtime_error("resource task");});},"task exception lost");
        thread.Invoke([&](auto&){Require(thread.Invoke([](auto&){return 17;})==17,"nested owner invoke deadlock");Reject([&]{thread.WaitIdle();},"owner WaitIdle allowed");});
        if(!mode){Images(thread);GPUCapacity(thread);Capacity(thread);}else if(mode==4)TaskCapacity(thread);else Failure(thread,mode-1);
        thread.Shutdown();Require(detached,"context not detached at shutdown");
        Reject([&]{thread.Invoke([](auto&){});},"task admitted after shutdown");
    }
    std::puts("PASS: startup failure cleanup, task exception transport, owner reentrancy, shutdown join and post-shutdown rejection");return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
