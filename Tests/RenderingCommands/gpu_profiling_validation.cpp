#define main UIValidationMain
#include "ui_command_validation.cpp"
#undef main
#include <OvRendering/Utils/Profiling.h>
#include <OvRendering/Threading/RenderThread.h>
#include <thread>
#include <chrono>

namespace
{
    std::thread::id ownerID;
    int failure=0;unsigned wrong=0,finishes=0,deletionCalls=0,counters=0,reads=0;
    std::vector<GLuint> generated,removed;
    decltype(glad_glGenQueries) generate;
    decltype(glad_glDeleteQueries) erase;
    decltype(glad_glFinish) finish;
    decltype(glad_glGetQueryiv) query;
    decltype(glad_glQueryCounter) counter;
    decltype(glad_glGetQueryObjectui64v) readResult;
    void Check(){if(ownerID!=std::this_thread::get_id()||!glfwGetCurrentContext())++wrong;}
    void APIENTRY Generate(GLsizei count,GLuint* ids){Check();generate(count,ids);generated.assign(ids,ids+count);}
    void APIENTRY Erase(GLsizei count,const GLuint* ids){Check();++deletionCalls;removed.insert(removed.end(),ids,ids+count);erase(count,ids);}
    void APIENTRY Finish(){Check();++finishes;finish();}
    void APIENTRY Query(GLenum target,GLenum name,GLint* value){Check();query(target,name,value);if(failure==1)throw std::runtime_error("injected GPU context construction failure");}
    void APIENTRY Counter(GLuint id,GLenum target){Check();++counters;counter(id,target);}
    void APIENTRY Read(GLuint id,GLenum name,GLuint64* value){Check();++reads;readResult(id,name,value);}
    void Verify(bool failed)
    {
        Require(!wrong&&generated.size()==65536&&generated==removed&&deletionCalls==1,"GPU query pool leaked, doubled, or used wrong owner");
        Require(finishes==(failure==1?0u:1u),"unexpected profiling shutdown finish count");
        if(!failed&&tracy::GetProfiler().IsConnected())Require(counters==2&&reads==2,"last GPU zone timestamps not collected at shutdown");
    }
    void LastZone(){TracyGpuZone("GPU profiling final zone");Clear();}
}
void GpuProfilingBeforeInitialize()
{
    Require(!tracy::GetGpuCtx().ptr,"Driver reused stale GPU TLS");ownerID=std::this_thread::get_id();
    wrong=finishes=deletionCalls=counters=reads=0;generated.clear();removed.clear();
#define HOOK(Name,saved,hook) saved=glad_gl##Name;glad_gl##Name=hook;
    HOOK(GenQueries,generate,Generate) HOOK(DeleteQueries,erase,Erase) HOOK(Finish,finish,Finish)
    HOOK(GetQueryiv,query,Query) HOOK(QueryCounter,counter,Counter) HOOK(GetQueryObjectui64v,readResult,Read)
#undef HOOK
}
void GpuProfilingAfterInitialize()
{if(failure==2)throw std::runtime_error("injected Driver construction failure after GPU context");}
int main(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const bool capture=argc>2&&std::string_view(argv[2])=="--capture";
    if(capture)
    {
        std::puts("TRACE READY");const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(!tracy::GetProfiler().IsConnected()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Require(tracy::GetProfiler().IsConnected(),"Tracy capture did not connect");
    }
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=320;ws.height=240;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();
    for(int i=0;i<8;++i)
    {
        {OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
            auto* current=tracy::GetGpuCtx().ptr;Reject([]{TracyGpuContext;},"duplicate profiling context accepted");
            Require(tracy::GetGpuCtx().ptr==current,"duplicate creation replaced live TLS");LastZone();}
        Require(!tracy::GetGpuCtx().ptr,"legacy Driver left dangling GPU TLS");TracyGpuContextDestroy;Verify(false);
    }
    std::puts("PASS: 8 same-thread Driver restarts, exactly 65536 matching query deletions each, duplicate creation rejected, TLS cleared and duplicate destroy harmless");
    glfwMakeContextCurrent(nullptr);
    for(int i=0;i<8;++i)
    {
        bool cleared=false;
        OvRendering::Threading::RenderThread worker([&]{window.MakeCurrentContext();return std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{true});},[&]{cleared=!tracy::GetGpuCtx().ptr;glfwMakeContextCurrent(nullptr);});
        Require(!tracy::GetGpuCtx().ptr,"Render profiling leaked onto main TLS");worker.Invoke([](auto&){LastZone();});
        worker.Shutdown();worker.RethrowIfFailed();Require(cleared,"Render owner detached before clearing profiling TLS");Verify(false);
    }
    for(int mode:{1,2})
    {
        failure=mode;bool cleared=false;
        Reject([&]{OvRendering::Threading::RenderThread worker([&]{window.MakeCurrentContext();return std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{true});},[&]{cleared=!tracy::GetGpuCtx().ptr;glfwMakeContextCurrent(nullptr);});},"injected initialization failure lost");
        Require(cleared,"failed initialization published/leaked GPU TLS");Verify(true);
    }
    failure=0;
    std::puts("PASS: 8 fixed Render owner shutdowns and failures inside GPU/after GPU initialization delete matching query pools before context detach");
    if(capture)std::puts("PASS: connected Tracy collects both timestamps of every final GPU zone during Driver shutdown");
    return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
