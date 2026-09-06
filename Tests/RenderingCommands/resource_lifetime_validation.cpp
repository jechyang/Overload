#define main UIValidationMain
#include "ui_command_validation.cpp"
#undef main
#include <baregl/ResourceLifetime.h>
#include <baregl/Buffer.h>
#include <baregl/Framebuffer.h>
#include <baregl/Renderbuffer.h>
#include <baregl/ShaderProgram.h>
#include <baregl/ShaderStage.h>
#include <baregl/VertexArray.h>
#include <OvRendering/Threading/RenderThread.h>
#include <mutex>
#include <condition_variable>
#include <chrono>

using RenderThread=OvRendering::Threading::RenderThread;
using RenderFrame=OvRendering::Data::RenderFrame;
using List=OvRendering::Data::RenderCommandList;
struct DeleteHooks
{
    inline static std::thread::id owner;
    inline static int wrong=0;
    inline static std::array<int,7> counts;
#define ARRAY_HOOK(Name,Index) \
    inline static decltype(glad_glDelete##Name) original##Name; \
    static void APIENTRY Name(GLsizei n,const GLuint* ids){Check();counts[Index]+=n;original##Name(n,ids);}
#define SINGLE_HOOK(Name,Index) \
    inline static decltype(glad_glDelete##Name) original##Name; \
    static void APIENTRY Name(GLuint id){Check();++counts[Index];original##Name(id);}
    ARRAY_HOOK(Textures,0) ARRAY_HOOK(Buffers,1) ARRAY_HOOK(Framebuffers,2)
    ARRAY_HOOK(Renderbuffers,3) SINGLE_HOOK(Program,4) SINGLE_HOOK(Shader,5) ARRAY_HOOK(VertexArrays,6)
#undef ARRAY_HOOK
#undef SINGLE_HOOK
    static void Check(){if(std::this_thread::get_id()!=owner||!glfwGetCurrentContext())++wrong;}
    DeleteHooks()
    {
        owner=std::this_thread::get_id();counts={};wrong=0;
#define INSTALL(Name) original##Name=glad_glDelete##Name;glad_glDelete##Name=Name;
        INSTALL(Textures) INSTALL(Buffers) INSTALL(Framebuffers) INSTALL(Renderbuffers) INSTALL(Program) INSTALL(Shader) INSTALL(VertexArrays)
#undef INSTALL
    }
    ~DeleteHooks()
    {
#define RESTORE(Name) glad_glDelete##Name=original##Name;
        RESTORE(Textures) RESTORE(Buffers) RESTORE(Framebuffers) RESTORE(Renderbuffers) RESTORE(Program) RESTORE(Shader) RESTORE(VertexArrays)
#undef RESTORE
    }
};
struct NativeSet
{
    std::vector<std::shared_ptr<baregl::detail::NativeObject>> objects;
    baregl::Texture::Snapshot texture;
    std::array<GLuint,7> ids;
    NativeSet()
    {
        auto image=Icon();texture=image->RetainStorage();objects.push_back(image);
        objects.push_back(std::make_shared<baregl::Buffer>());
        objects.push_back(std::make_shared<baregl::Framebuffer>());
        objects.push_back(std::make_shared<baregl::Renderbuffer>());
        objects.push_back(std::make_shared<baregl::ShaderProgram>());
        objects.push_back(std::make_shared<baregl::ShaderStage>(baregl::types::EShaderType::VERTEX));
        objects.push_back(std::make_shared<baregl::VertexArray>());
        for(size_t i=0;i<ids.size();++i)ids[i]=objects[i]->GetID();
    }
};
bool Alive(const std::array<GLuint,7>& ids)
{return glIsTexture(ids[0])&&glIsBuffer(ids[1])&&glIsFramebuffer(ids[2])&&glIsRenderbuffer(ids[3])&&glIsProgram(ids[4])&&glIsShader(ids[5])&&glIsVertexArray(ids[6]);}
bool Gone(const std::array<GLuint,7>& ids)
{return !glIsTexture(ids[0])&&!glIsBuffer(ids[1])&&!glIsFramebuffer(ids[2])&&!glIsRenderbuffer(ids[3])&&!glIsProgram(ids[4])&&!glIsShader(ids[5])&&!glIsVertexArray(ids[6]);}
struct Gate
{
    std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;
    void Block(){std::unique_lock lock(mutex);entered=true;changed.notify_all();changed.wait(lock,[&]{return released;});}
    void Await(){std::unique_lock lock(mutex);Require(changed.wait_for(lock,std::chrono::seconds(10),[&]{return entered;}),"owner gate did not start");}
    void Release(){std::lock_guard lock(mutex);released=true;changed.notify_all();}
};
RenderFrame BlockFrame(Gate& gate)
{List list;list.Append([&](auto&){gate.Block();});list.Seal();RenderFrame frame;frame.Append(std::move(list));frame.Seal();return frame;}
void CrossThreadRelease(RenderThread& thread)
{
    for(int i=0;i<32;++i)
    {
        auto resources=thread.Invoke([](auto&){auto result=std::make_unique<NativeSet>();Require(Alive(result->ids),"native fixture was not created");return result;});
        const auto ids=resources->ids;Gate gate;auto done=thread.Submit(BlockFrame(gate));gate.Await();
        resources->objects.clear(); // Last wrapper release has no current GL context.
        resources->texture={};resources.reset();gate.Release();thread.WaitIdle();
        thread.Invoke([&](auto&){Require(Gone(ids)&&!DeleteHooks::wrong,"native deletion leaked or ran on producer");});
        Require(done.Succeeded(),"resource cleanup corrupted frame completion");
    }
    thread.Invoke([](auto&){for(auto count:DeleteHooks::counts)Require(count==32,"native object deleted more/less than once");});
    // Retirement wakes an otherwise idle owner without another Submit/Invoke.
    auto resource=thread.Invoke([](auto&){return Icon();});const auto id=resource->GetID();
    std::thread release([resource=std::move(resource)]()mutable{resource.reset();});release.join();thread.WaitIdle();
    thread.Invoke([&](auto&){Require(!glIsTexture(id),"idle owner did not wake for resource deletion");});
    std::puts("PASS: 32 rounds of seven native GL resource kinds released without producer context, exactly-once owner deletion and idle wake/drain");
}
void FrameStorage(RenderThread& thread)
{
    auto image=thread.Invoke([](auto&){return Icon();});auto storage=image->RetainStorage();const auto id=storage.GetID();
    List list;list.Append([storage](auto&){Require(glIsTexture(storage.GetID()),"frame lost source-destroyed texture");Clear();});list.Seal();
    RenderFrame frame;frame.Append(std::move(list));frame.Seal();image.reset();storage={};
    auto done=thread.Submit(std::move(frame));thread.WaitIdle();
    thread.Invoke([&](auto&){Require(!glIsTexture(id),"frame-retired storage leaked");});Require(done.Succeeded(),"frame storage failed");
    std::puts("PASS: source texture wrapper destroyed on producer, storage retained through actual frame GPU retirement");
}
int main() try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=320;ws.height=240;ws.samples=1;
    OvWindowing::Window window(device,ws);glfwMakeContextCurrent(nullptr);
    std::unique_ptr<NativeSet> survivors;
    for(int generation=0;generation<2;++generation)
    {
        std::unique_ptr<DeleteHooks> hooks;
        RenderThread thread([&]{window.MakeCurrentContext();auto driver=std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{true});hooks=std::make_unique<DeleteHooks>();return driver;},[&]{Require(!DeleteHooks::wrong,"wrong-thread GL deletion");hooks.reset();glfwMakeContextCurrent(nullptr);});
        if(!generation){CrossThreadRelease(thread);FrameStorage(thread);}
        else
        {
            auto fresh=thread.Invoke([](auto&){return std::make_unique<NativeSet>();});const auto ids=fresh->ids;
            survivors.reset(); // Old domain holders cannot delete new domain names.
            thread.Invoke([&](auto&){Require(Alive(ids)&&DeleteHooks::counts==std::array<int,7>{},"late old release deleted current resources");});
            survivors=std::move(fresh);
        }
        if(!generation)survivors=thread.Invoke([](auto&){return std::make_unique<NativeSet>();});
        const auto ids=survivors->ids;
        thread.Shutdown();thread.RethrowIfFailed();
        for(const auto& resource:survivors->objects)Require(!resource->GetID(),"shutdown did not invalidate native ID");
        Require(!survivors->texture.GetID(),"shutdown left a live snapshot ID");
        // Read back deletion on the main thread only after owner detached.
        window.MakeCurrentContext();Require(Gone(ids),"shutdown leaked retained native objects");glfwMakeContextCurrent(nullptr);
    }
    survivors.reset();
    std::puts("PASS: shutdown deletes outstanding GL names, invalidates retained wrapper/snapshot IDs, late CPU release cannot affect a new resource domain");return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
