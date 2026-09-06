#define main UIValidationMain
#include "ui_command_validation.cpp"
#undef main
#include <OvRendering/Threading/RenderThread.h>
#include <atomic>
#include <thread>

using RenderThread=OvRendering::Threading::RenderThread;
struct NativeHooks
{
    inline static std::thread::id owner;
    inline static std::atomic<unsigned> wrong=0,deleted=0;
    inline static bool failShader=false,failProgram=false;
    inline static std::vector<GLuint> programs,buffers,textures,shaders;
    inline static decltype(glad_glCreateProgram) createProgram;
    inline static decltype(glad_glCreateShader) createShader;
    inline static decltype(glad_glGenBuffers) genBuffers;
    inline static decltype(glad_glGenTextures) genTextures;
    inline static decltype(glad_glDeleteProgram) deleteProgram;
    inline static decltype(glad_glDeleteShader) deleteShader;
    inline static decltype(glad_glDeleteBuffers) deleteBuffers;
    inline static decltype(glad_glDeleteTextures) deleteTextures;
    inline static decltype(glad_glGetShaderiv) getShader;
    inline static decltype(glad_glGetProgramiv) getProgram;
    static void APIENTRY ForbiddenTextureCreate(GLenum,GLsizei,GLuint*){++wrong;throw std::runtime_error("lazy upload after context close");}
    static void Check(){if(std::this_thread::get_id()!=owner||!glfwGetCurrentContext())++wrong;}
    static GLuint APIENTRY CreateProgram(){Check();auto id=createProgram();programs.push_back(id);return id;}
    static GLuint APIENTRY CreateShader(GLenum kind){Check();auto id=createShader(kind);shaders.push_back(id);return id;}
    static void APIENTRY GenBuffers(GLsizei n,GLuint* ids){Check();genBuffers(n,ids);buffers.insert(buffers.end(),ids,ids+n);}
    static void APIENTRY GenTextures(GLsizei n,GLuint* ids){Check();genTextures(n,ids);textures.insert(textures.end(),ids,ids+n);}
    static void APIENTRY DeleteProgram(GLuint id){Check();++deleted;deleteProgram(id);}
    static void APIENTRY DeleteShader(GLuint id){Check();++deleted;deleteShader(id);}
    static void APIENTRY DeleteBuffers(GLsizei n,const GLuint* ids){Check();deleted+=n;deleteBuffers(n,ids);}
    static void APIENTRY DeleteTextures(GLsizei n,const GLuint* ids){Check();deleted+=n;deleteTextures(n,ids);}
    static void APIENTRY GetShader(GLuint id,GLenum query,GLint* value){getShader(id,query,value);if(failShader&&query==GL_COMPILE_STATUS)*value=GL_FALSE;}
    static void APIENTRY GetProgram(GLuint id,GLenum query,GLint* value){getProgram(id,query,value);if(failProgram&&query==GL_LINK_STATUS)*value=GL_FALSE;}
    NativeHooks()
    {
        owner=std::this_thread::get_id();wrong=deleted=0;failShader=failProgram=false;programs.clear();buffers.clear();textures.clear();shaders.clear();
#define HOOK(Name,save) save=glad_gl##Name;glad_gl##Name=Name;
        HOOK(CreateProgram,createProgram) HOOK(CreateShader,createShader) HOOK(GenBuffers,genBuffers) HOOK(GenTextures,genTextures)
        HOOK(DeleteProgram,deleteProgram) HOOK(DeleteShader,deleteShader) HOOK(DeleteBuffers,deleteBuffers) HOOK(DeleteTextures,deleteTextures)
        HOOK(GetShaderiv,getShader) HOOK(GetProgramiv,getProgram)
#undef HOOK
    }
    static void APIENTRY GetShaderiv(GLuint id,GLenum query,GLint* value){GetShader(id,query,value);}
    static void APIENTRY GetProgramiv(GLuint id,GLenum query,GLint* value){GetProgram(id,query,value);}
    ~NativeHooks()
    {
#define RESTORE(Name,save) glad_gl##Name=save;
        RESTORE(CreateProgram,createProgram) RESTORE(CreateShader,createShader) RESTORE(GenBuffers,genBuffers) RESTORE(GenTextures,genTextures)
        RESTORE(DeleteProgram,deleteProgram) RESTORE(DeleteShader,deleteShader) RESTORE(DeleteBuffers,deleteBuffers) RESTORE(DeleteTextures,deleteTextures)
        RESTORE(GetShaderiv,getShader) RESTORE(GetProgramiv,getProgram)
#undef RESTORE
    }
    static void RequireGone()
    {
        for(auto id:programs)Require(!glIsProgram(id),"ImGui program leaked");
        for(auto id:buffers)Require(!glIsBuffer(id),"ImGui buffer leaked");
        for(auto id:textures)Require(!glIsTexture(id),"ImGui backend texture leaked");
        for(auto id:shaders)Require(!glIsShader(id),"ImGui temporary shader leaked");
        Require(!wrong,"ImGui native operation ran outside owner");
    }
};
struct Fixture
{
    std::shared_ptr<DrawData> draw;
    std::unique_ptr<DrawDataRecorder> recorder;
    std::vector<uint8_t> expected;
};
Fixture Prepare(int iteration,bool failures=false)
{
    Fixture result;auto icon=Icon();BeginContext();auto& source=Frame(icon->GetID(),iteration%4,true);
    Clear();ImGui_ImplOpenGL3_RenderDrawData(&source);result.expected=Pixels();
    ImGui_ImplOpenGL3_DestroyDeviceObjects();ImGui_ImplOpenGL3_NewFrame();
    result.recorder=std::make_unique<DrawDataRecorder>();std::array owners{icon};
    result.draw=std::make_shared<DrawData>(result.recorder->Capture(source,owners));
    if(failures)
    {
        GLuint pbo=0;glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);glBufferData(GL_PIXEL_UNPACK_BUFFER,128,nullptr,GL_STATIC_DRAW);
        for(int mode=0;mode<2;++mode)
        {
            NativeHooks::failShader=mode==0;NativeHooks::failProgram=mode==1;
            Require(!ImGui_ImplOpenGL3_CreateDeviceObjects(),"injected shader/link failure accepted");
            NativeHooks::failShader=NativeHooks::failProgram=false;
            GLint restored=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&restored);Require(GLuint(restored)==pbo,"failed device creation changed unpack binding");
            (void)ImGui_ImplOpenGL3_CaptureRenderState(); // Prior successful generation remains published.
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&pbo);
    }
    return result;
}
int main() try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=320;ws.height=240;ws.samples=1;
    OvWindowing::Window window(device,ws);glfwMakeContextCurrent(nullptr);
    std::shared_ptr<DrawData> stale;
    for(int domain=0;domain<2;++domain)
    {
        std::unique_ptr<NativeHooks> hooks;
        RenderThread owner([&]{window.MakeCurrentContext();auto driver=std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{true});hooks=std::make_unique<NativeHooks>();return driver;},[&]{Require(!NativeHooks::wrong,"wrong owner at shutdown");hooks.reset();glfwMakeContextCurrent(nullptr);});
        owner.Invoke([](auto&){BeginContext();auto& source=Frame(0,0);Clear();ImGui_ImplOpenGL3_RenderDrawData(&source);});
        EndContext();owner.WaitIdle();owner.Invoke([](auto&){NativeHooks::RequireGone();});
        if(!domain)
        {
            for(int i=0;i<32;++i)
            {
                auto fixture=owner.Invoke([&](auto&){return Prepare(i,i==0);});
                // CPU backend/context shutdown now occurs without a current GL
                // context; the retained frame still owns the device generation.
                EndContext(fixture.recorder.get());fixture.recorder.reset();
                owner.Invoke([&](auto&){Clear();fixture.draw->Execute();Require(Pixels()==fixture.expected,"owner UI image changed after foreign backend shutdown");});
                fixture.draw.reset();owner.WaitIdle();
                owner.Invoke([](auto&){NativeHooks::RequireGone();Require(glGetError()==GL_NO_ERROR,"ImGui lifetime GL error");});
            }
            auto old=owner.Invoke([](auto&){return Prepare(0);});EndContext(old.recorder.get());old.recorder.reset();
            owner.Invoke([&](auto&){Clear();old.draw->Execute();});
            std::atomic<bool> newBackendReady=false,released=false;
            std::jthread releaser([draw=std::move(old.draw),&newBackendReady,&released]()mutable{
                while(!newBackendReady.load())std::this_thread::yield();draw.reset();released=true;
            });
            auto next=owner.Invoke([&](auto&){BeginContext();newBackendReady=true;
                auto& source=Frame(0,0,true);Clear();ImGui_ImplOpenGL3_RenderDrawData(&source);
                while(!released.load())std::this_thread::yield();return Pixels();});
            releaser.join();Require(!next.empty(),"concurrent backend fixture was empty");
            EndContext();owner.WaitIdle();owner.Invoke([](auto&){NativeHooks::RequireGone();});
            std::puts("PASS: last old snapshot released on producer while a new ImGui backend builds and renders on owner");
            std::puts("PASS: 32 exact retained UI images, foreign backend/context and final snapshot destruction, owner-only native retirement; failed compile/link preserves state and prior generation");
        }
        else
        {
            auto fixture=owner.Invoke([](auto&){return Prepare(0);});
            const auto before=NativeHooks::deleted.load();stale.reset();
            Require(NativeHooks::deleted==before,"old domain release issued native deletion");
            EndContext(fixture.recorder.get());fixture.recorder.reset();
            owner.Invoke([&](auto&){Clear();fixture.draw->Execute();Require(Pixels()==fixture.expected,"late stale release damaged new UI generation");});
            fixture.draw.reset();owner.WaitIdle();owner.Invoke([](auto&){NativeHooks::RequireGone();});
        }
        auto fixture=owner.Invoke([](auto&){return Prepare(0);});EndContext(fixture.recorder.get());fixture.recorder.reset();stale=std::move(fixture.draw);
        owner.Shutdown();owner.RethrowIfFailed();
        const auto createTextures=glad_glCreateTextures;glad_glCreateTextures=NativeHooks::ForbiddenTextureCreate;
        Reject([&]{stale->Execute();},"closed generation replay was accepted without GL context");
        glad_glCreateTextures=createTextures;
        Require(!NativeHooks::wrong,"closed generation replay touched native GL");
        window.MakeCurrentContext();NativeHooks::RequireGone();glfwMakeContextCurrent(nullptr);
    }
    stale.reset();std::puts("PASS: closed UI generation rejects replay before lazy font upload; late CPU release across a new domain cannot delete new GPU objects");return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
