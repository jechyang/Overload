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
#ifdef PRODUCT_EDITOR
#include <baregl/ResourceLifetime.h>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <GLFW/glfw3.h>
#include <OvEditor/Panels/TextureDebugger.h>
#include "../../Dependencies/baregl/src/baregl/debug/Event.h"
// The actual Application already owns baregl's single Driver. Transfer its GL
// context temporarily to a test worker without constructing a second Driver.
class TextureTestOwner
{
    std::mutex mutex;std::condition_variable changed;
    std::queue<std::function<void()>> tasks;bool closed=false;std::thread worker;
public:
    explicit TextureTestOwner(OvWindowing::Window& window)
    {
        worker=std::thread([&]{window.MakeCurrentContext();baregl::DeletionQueue deletions;
            for(;;){std::function<void()> task;{
                std::unique_lock lock(mutex);changed.wait(lock,[&]{return closed||!tasks.empty();});
                if(tasks.empty())break;task=std::move(tasks.front());tasks.pop();}
                deletions.Drain();task();task={};deletions.Drain();}
            deletions.Close();glfwMakeContextCurrent(nullptr);
        });
    }
    template<class F> auto Invoke(F&& fn)
    {
        using Result=std::invoke_result_t<F>;
        auto task=std::make_shared<std::packaged_task<Result()>>(std::forward<F>(fn));auto result=task->get_future();
        {std::lock_guard lock(mutex);tasks.emplace([task]{(*task)();});}changed.notify_one();return result.get();
    }
    void Shutdown(){if(worker.joinable()){{std::lock_guard lock(mutex);closed=true;}changed.notify_one();worker.join();}}
    ~TextureTestOwner(){Shutdown();}
};
void ValidateTextureDebuggerOwner(OvEditor::Core::Editor& editor, OvEditor::Core::Context& context)
{
    auto& debugger=editor.m_panelsManager.GetPanelAs<OvEditor::Panels::TextureDebugger>("Texture Debugger");
    const auto wasOpened=debugger.IsOpened();const auto original=debugger.m_selectedTexture;
    struct RestoreContext {OvWindowing::Window& window; ~RestoreContext(){window.MakeCurrentContext();}} restore{*context.window};
    glfwMakeContextCurrent(nullptr);
    TextureTestOwner owner(*context.window);
    auto texture=owner.Invoke([](){return std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D,"background debugger");});
    const auto id=texture->GetID();debugger.Open();debugger.Update(0);
    Require(debugger.m_textureSelector.choices.contains(id),"main UI missed managed texture creation");
    debugger.m_textureSelector.ValueChangedEvent.Invoke(id);
    Require(debugger.m_selectedTexture&&!debugger.m_selectedTexture->allocated,"unallocated texture UI metadata invalid");
    owner.Invoke([&](){texture->Allocate({.width=16,.height=17,.minFilter=baregl::types::ETextureFilteringMode::LINEAR,.internalFormat=baregl::types::EInternalFormat::RGBA8,.useMipMaps=false,.mutableDesc=baregl::data::MutableTextureDesc{}});});
    Require(!debugger.m_selectedTexture->allocated,"owner mutated live TextureDebugger selection");debugger.Update(0);
    Require(debugger.m_selectedTexture->width==16&&debugger.m_selectedTexture->height==17,"main UI missed allocation metadata");
    owner.Invoke([&](){texture->Resize(31,32);});debugger.Update(0);
    Require(debugger.m_selectedTexture->width==31&&debugger.m_selectedTexture->height==32,"main UI missed resize metadata");
    bool rejected=false;try{owner.Invoke([&](){debugger.Update(0);});}catch(const std::logic_error&){rejected=true;}
    Require(rejected,"TextureDebugger accepted update on Render owner");
    const auto generation=debugger.m_selectedTexture->generation;
    // Inject re-registration with the same native ID to exercise the generation
    // boundary deterministically; this does not claim physical GL name reuse.
    owner.Invoke([&](){baregl::debug::OnTextureDestroyed(*texture);baregl::debug::OnTextureCreated(*texture);baregl::debug::OnTextureUpdated(*texture);});
    Require(debugger.m_selectedTexture->generation==generation,"writer changed UI generation");debugger.Update(0);
    Require(!debugger.m_selectedTexture,"stale selection silently followed reused native ID");
    debugger.m_textureSelector.ValueChangedEvent.Invoke(id);
    Require(debugger.m_selectedTexture&&debugger.m_selectedTexture->generation>generation,"replacement generation cannot be selected");
    debugger.Close();texture.reset();Require(debugger.m_selectedTexture.has_value(),"background removal mutated UI widgets");
    debugger.Update(0);Require(!debugger.m_selectedTexture,"closed panel retained removed texture");owner.Invoke([]{});
    owner.Invoke([&](){Require(!glIsTexture(id),"real debugger leaked managed texture");});
    owner.Shutdown();context.window->MakeCurrentContext();context.driver->OnFrameCompleted();
    debugger.SetOpened(wasOpened);debugger.m_textureSelector.ValueChangedEvent.Invoke(original ? original->id : 0);
    std::puts("PASS: real TextureDebugger refreshes managed owner create/allocate/resize/remove on main without GL context, rejects worker update and stale registration generation, releases closed-panel selection");
}
#endif
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
#ifdef PRODUCT_EDITOR
        ValidateTextureDebuggerOwner(product,context);
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
