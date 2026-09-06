#define main UIValidationMain
#include "ui_command_validation.cpp"
#undef main
#include <OvRendering/Data/RenderFrame.h>
using List=OvRendering::Data::RenderCommandList;
using RenderFrame=OvRendering::Data::RenderFrame;

struct SyncHooks
{
    inline static PFNGLFENCESYNCPROC fence=glad_glFenceSync;
    inline static PFNGLCLIENTWAITSYNCPROC wait=glad_glClientWaitSync;
    inline static PFNGLDELETESYNCPROC erase=glad_glDeleteSync;
    inline static PFNGLFINISHPROC finish=glad_glFinish;
    inline static bool timeout=false,failWait=false,failFence=false;
    inline static int waits=0,deletes=0,finishes=0;
    inline static GLbitfield flags=0;inline static GLuint64 duration=0;
    static GLsync APIENTRY Fence(GLenum condition,GLbitfield value){return failFence?nullptr:fence(condition,value);}
    static GLenum APIENTRY Wait(GLsync sync,GLbitfield value,GLuint64 ns)
    {++waits;flags=value;duration=ns;return failWait?GL_WAIT_FAILED:timeout?GL_TIMEOUT_EXPIRED:wait(sync,value,ns);}
    static void APIENTRY Delete(GLsync sync){++deletes;erase(sync);}
    static void APIENTRY Finish(){++finishes;finish();}
    SyncHooks()
    {
        fence=glad_glFenceSync;wait=glad_glClientWaitSync;erase=glad_glDeleteSync;finish=glad_glFinish;
        timeout=failWait=failFence=false;waits=deletes=finishes=0;
        glad_glFenceSync=Fence;glad_glClientWaitSync=Wait;glad_glDeleteSync=Delete;glad_glFinish=Finish;
    }
    ~SyncHooks(){glad_glFenceSync=fence;glad_glClientWaitSync=wait;glad_glDeleteSync=erase;glad_glFinish=finish;}
};

void FrameLifecycle(OvRendering::Context::Driver& driver,OvWindowing::Window& window)
{
    SyncHooks hooks;
    auto texture=Icon();const auto id=texture->GetID();std::weak_ptr<baregl::Texture> weak=texture;
    List first;auto firstDone=first.GetCompletion();first.Append([texture](auto&){Clear();});first.Seal();
    List last;auto lastDone=last.GetCompletion();last.Append([firstDone](auto&){Require(!firstDone.Succeeded(),"child completed before tail");});last.Seal();
    RenderFrame frame(42);auto done=frame.GetCompletion();frame.Append(std::move(first));frame.Append(std::move(last));frame.Seal();texture.reset();
    Require(!done.IsRetired()&&!done.Succeeded()&&done.GetFrameID()==42,"invalid ready frame completion");
    Reject([&]{done.RethrowIfFailed();},"unretired exception read");Reject([&]{frame.Wait();},"unsubmitted frame waited");
    int presentations=0;
    Require(frame.Submit(driver,[&]{Require(!firstDone.Succeeded()&&!lastDone.Succeeded(),"children completed before presentation");++presentations;window.SwapBuffers();}),"submission failed");
    Require(firstDone.Succeeded()&&lastDone.Succeeded()&&done.GetState()==RenderFrame::State::SUBMITTED&&!done.Succeeded()&&done.PresentationRequested(),"CPU/GPU completion conflated");
    Require(presentations==1&&!weak.expired()&&glIsTexture(id),"capture released before retirement");
    Reject([&]{frame.Submit(driver);},"double submission");Reject([&]{frame.Cancel();},"submitted cancellation");
    SyncHooks::timeout=true;Require(!frame.Poll()&&SyncHooks::flags==0&&SyncHooks::duration==0&&!weak.expired(),"poll blocked or released timed-out frame");
    SyncHooks::timeout=false;RenderFrame moved(std::move(frame));moved.Wait();
    Require(done.Succeeded()&&weak.expired()&&!glIsTexture(id)&&SyncHooks::deletes==1&&SyncHooks::finishes==0,"GPU retirement lost ownership or leaked fence");
    done.RethrowIfFailed();Require(moved.Poll()&&frame.Poll(),"retired/moved poll not idempotent");
    std::puts("PASS: unified child/presentation completion, zero-time poll, GPU-held texture, move, exactly-once retirement");
}

void FrameFailures(OvRendering::Context::Driver& driver)
{
    for(int failure=0;failure<5;++failure)
    {
        SyncHooks hooks;auto texture=Icon();std::weak_ptr<baregl::Texture> weak=texture;
        List::History history;List prefix;prefix.UseHistory(history);auto prefixDone=prefix.GetCompletion();
        prefix.Append([texture](auto&){Clear();});prefix.Seal();
        List tail;auto tailDone=tail.GetCompletion();tail.Append([failure](auto&){if(failure==0)throw std::runtime_error("tail failed");});tail.Seal();
        RenderFrame frame;frame.Append(std::move(prefix));frame.Append(std::move(tail));frame.Seal();auto done=frame.GetCompletion();texture.reset();
        SyncHooks::failFence=failure==2;
        const bool submitted=frame.Submit(driver,[failure]{if(failure==1)throw std::runtime_error("present failed");});
        Require(submitted==(failure>=3),"wrong submit failure result");
        if(failure<3)Require(!prefixDone.Succeeded()&&!tailDone.Succeeded(),"partial frame reported child success");
        if(failure!=2)Require(!weak.expired()&&!done.IsRetired(),"failing submitted prefix prematurely released");
        if(failure>=3)SyncHooks::failWait=true;
        if(failure==3)Require(frame.Poll(),"failed GPU poll did not retire");else frame.Wait();
        Require(done.IsRetired()&&!done.Succeeded()&&done.GetState()==RenderFrame::State::FAILED&&weak.expired(),"failure retirement missing");
        Reject([&]{done.RethrowIfFailed();},"stored frame error missing");
        Require(SyncHooks::finishes==(failure>=2?1:0),"unexpected synchronous drain");
        List recovery;Require(recovery.UseHistory(history),"failed frame did not poison speculative history");recovery.Cancel();
    }
    {
        List::History history;List child;child.UseHistory(history);child.Seal();auto childDone=child.GetCompletion();RenderFrame::Completion done;
        {RenderFrame abandoned;done=abandoned.GetCompletion();abandoned.Append(std::move(child));abandoned.Seal();}
        Require(done.IsRetired()&&done.GetState()==RenderFrame::State::CANCELLED&&!childDone.Succeeded(),"abandon did not cancel frame");
        List reset;Require(reset.UseHistory(history),"abandon did not invalidate history");reset.Cancel();
    }
    {
        SyncHooks hooks;RenderFrame::Completion done;
        {List child;child.Seal();RenderFrame submitted;submitted.Append(std::move(child));submitted.Seal();done=submitted.GetCompletion();Require(submitted.Submit(driver),"destructor fixture submit");}
        Require(done.Succeeded()&&SyncHooks::deletes==1,"submitted destructor did not drain");
    }
    {
        List valid;valid.Seal();auto done=valid.GetCompletion();RenderFrame frame;frame.Append(std::move(valid));List unsealed;
        Reject([&]{frame.Append(std::move(unsealed));},"unsealed child accepted");
        Require(frame.GetCompletion().IsRetired()&&frame.GetCompletion().GetState()==RenderFrame::State::FAILED&&!done.Succeeded(),"invalid append did not fail aggregate");
    }
    std::puts("PASS: tail/presentation/fence creation/poll wait/blocking wait failures, history recovery, abandonment and destructor drain");
}

void FrameHistories(OvRendering::Context::Driver& driver)
{
    for(bool reverse:{false,true})
    {
        List::History history;int issued=0;
        List a;a.UseHistory(history);a.Append([&](auto&){++issued;});a.Seal();auto aDone=a.GetCompletion();
        List b;b.UseHistory(history);b.Append([&](auto&){++issued;});b.Seal();auto bDone=b.GetCompletion();
        List nested;nested.Append(reverse?std::move(b):std::move(a));nested.Append(reverse?std::move(a):std::move(b));nested.Seal();
        RenderFrame frame;frame.Append(std::move(nested));frame.Seal();Require(frame.Submit(driver)==!reverse,"history order not enforced");
        Require(issued==(reverse?0:2),"out-of-order history touched GPU");
        if(!reverse)
        {
            Require(!frame.GetCompletion().IsRetired()&&aDone.Succeeded()&&bDone.Succeeded(),"nested child CPU completion missing");
            List next;Require(!next.UseHistory(history),"valid pending GPU history reset");next.Append([&](auto&){++issued;});next.Seal();
            RenderFrame successor;successor.Append(std::move(next));successor.Seal();Require(successor.Submit(driver),"successor unnecessarily waits prior GPU fence");successor.Wait();
        }
        frame.Wait();Require(frame.GetCompletion().Succeeded()==!reverse,"history frame outcome");
    }
    {
        List::History history;List child;child.UseHistory(history);child.Seal();
        List parent;parent.Append(std::move(child));Require(!parent.UseHistory(history),"merged history reset");parent.Seal();parent.Execute(driver);
        Require(parent.GetCompletion().Succeeded(),"parent history did not recognize preceding merged child");
    }
    std::puts("PASS: nested ordered histories, mixed parent/child history, reversed dependency rejected before GPU, successor submits before predecessor GPU retirement");
}

void FrameUI(OvRendering::Context::Driver& driver,OvWindowing::Window& window)
{
    std::optional<RenderFrame> frame;std::vector<uint8_t> expected;std::weak_ptr<baregl::Texture> weak;
    {
        auto manager=std::make_unique<OvUI::Core::UIManager>(window);manager->EnableEditorLayoutSave(false);
        OvUI::Modules::Canvas canvas;OvUI::Panels::PanelWindow panel("Whole frame");panel.SetPosition({0,0});panel.SetSize({300,220});panel.autoSize=false;
        auto icon=Icon();weak=icon;panel.CreateWidget<OvUI::Widgets::Visual::Image>(icon,OvMaths::FVector2{50,50});panel.CreateWidget<OvUI::Widgets::Texts::Text>("Frame retirement");
        canvas.AddPanel(panel);manager->SetCanvas(canvas);for(int i=0;i<4;++i){Clear();manager->Render();}expected=Pixels();
        frame.emplace();List background;background.Append([](auto&){Clear();});background.Seal();frame->Append(std::move(background));
        frame->Append(manager->RecordRenderCommands());frame->Seal();manager.reset();icon.reset();
    }
    Require(frame->Submit(driver),"UI frame submit");Require(Pixels()==expected&&!weak.expired(),"owning UI frame image/lifetime differs");
    frame->Wait();Require(weak.expired(),"retired UI frame retained texture");
    std::puts("PASS: exact complete UI frame after source UIManager/context/widgets destruction; texture retained through GPU completion");
}
int main() try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=320;ws.height=240;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
    FrameLifecycle(driver,window);FrameFailures(driver);FrameHistories(driver);FrameUI(driver,window);
    Require(glGetError()==GL_NO_ERROR,"frame validation GL error");return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
