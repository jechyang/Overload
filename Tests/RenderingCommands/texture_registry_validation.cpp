#define main UIValidationMain
#include "ui_command_validation.cpp"
#undef main
#include <OvRendering/Utils/ResourceTracking.h>
#include <OvRendering/Threading/RenderThread.h>
#include <atomic>
#include <new>
#include <thread>

namespace Registry = OvRendering::Utils::ResourceTracking;
using RenderThread = OvRendering::Threading::RenderThread;
void AllocateMutable(baregl::Texture& texture)
{
    texture.Allocate({.width=16,.height=17,.minFilter=baregl::types::ETextureFilteringMode::LINEAR,
        .internalFormat=baregl::types::EInternalFormat::RGBA8,.useMipMaps=false,
        .mutableDesc=baregl::data::MutableTextureDesc{}});
}
void ConcurrentRegistry(RenderThread& owner)
{
    auto texture=owner.Invoke([](auto&){return std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D,"registry writer");});
    const auto id=texture->GetID();auto initial=Registry::GetTexture(id);
    Require(initial&&initial->storage.HasOwnerQueue()&&!initial->allocated&&!initial->width&&!initial->height&&initial->name=="registry writer","constructor exposed invalid descriptor metadata");
    const auto ids=Registry::GetTextureIDs();Require(ids==std::vector<uint32_t>{id},"unexpected initial registry");
    owner.Invoke([&](auto&){AllocateMutable(*texture);});
    auto allocated=Registry::GetTexture(id);Require(allocated&&allocated->allocated&&allocated->width==16&&allocated->height==17,"allocation metadata was not published");
    std::atomic<bool> started=false,done=false;std::atomic<unsigned> samples=0;std::exception_ptr failure;
    std::jthread writer([&]{try{owner.Invoke([&](auto&){started=true;while(!samples.load())std::this_thread::yield();
        for(unsigned i=0;i<512;++i){texture->Resize(16+i%48,17+i%48);auto temporary=Icon();}
        Require(glGetError()==GL_NO_ERROR,"registry writer GL error");});}catch(...){failure=std::current_exception();}done=true;});
    while(!started.load()&&!done.load())std::this_thread::yield();
    do
    {
        ++samples;
        auto entry=Registry::GetTexture(id,initial->generation);Require(entry&&entry->allocated&&entry->height==entry->width+1,"torn texture dimensions");
        auto entries=Registry::GetTextures();for(const auto& e:entries)if(e.id==id)Require(e.height==e.width+1&&e.generation==initial->generation,"torn registry list");
        (void)Registry::GetTextureIDs();
    }while(!done.load());
    writer.join();if(failure)std::rethrow_exception(failure);
    Require(!initial->allocated&&!initial->width&&allocated->width==16,"copied metadata changed after resize");
    Require(!Registry::GetTexture(id,initial->generation+100000),"mismatched generation accepted");
    texture.reset();Require(!Registry::GetTexture(id)&&Registry::GetTextures().empty()&&Registry::GetTextureIDs().empty(),"destroyed wrapper remained registered");
    Require(ids==std::vector<uint32_t>{id},"ID list was borrowed");
    owner.Invoke([&](auto&){Require(glIsTexture(id),"metadata snapshot lost retained storage");});
    initial.reset();allocated.reset();owner.WaitIdle();
    owner.Invoke([&](auto&){Require(!glIsTexture(id),"foreign snapshot release leaked GPU storage");});
    std::printf("PASS: 512 owner resizes and temporary creations, %u concurrent stable registry reads, owning metadata and owner retirement\n",samples.load());
}
int main() try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);Registry::TrackResources();Registry::TrackResources();
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=320;ws.height=240;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();
    {
        OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});auto texture=Icon();const auto id=texture->GetID();
        Require(Registry::GetTexture(id)->width==2,"legacy creator snapshot unavailable");
        bool rejected=false,listRejected=false,idsSafe=false;std::thread foreign([&]{
            idsSafe=Registry::GetTextureIDs()==std::vector<uint32_t>{id};
            try{(void)Registry::GetTexture(id);}catch(const std::logic_error&){rejected=true;}
            try{(void)Registry::GetTextures();}catch(const std::logic_error&){listRejected=true;}
        });foreign.join();
        Require(rejected&&listRejected&&idsSafe,"foreign legacy retention or CPU ID query contract violated");
        std::puts("PASS: legacy creator can retain registry entry; foreign thread retention rejected");
    }
    glfwMakeContextCurrent(nullptr);
    alignas(baregl::Texture) std::byte storage[sizeof(baregl::Texture)];uint64_t oldGeneration=0;
    for(int generation=0;generation<2;++generation)
    {
        RenderThread owner([&]{window.MakeCurrentContext();return std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{true});},[]{glfwMakeContextCurrent(nullptr);});
        if(!generation)ConcurrentRegistry(owner);
        auto* texture=owner.Invoke([&](auto&){return std::construct_at(reinterpret_cast<baregl::Texture*>(storage),baregl::types::ETextureType::TEXTURE_2D,"reused address");});
        auto entry=Registry::GetTexture(texture->GetID());Require(entry&&entry->generation>oldGeneration,"wrapper address reuse lost registry generation");
        oldGeneration=entry->generation;owner.Shutdown();owner.RethrowIfFailed();
        Require(!texture->GetID()&&!entry->storage.GetID()&&Registry::GetTextures().empty()&&Registry::GetTextureIDs().empty(),"closed resource domain remained visible");
        std::destroy_at(texture);entry.reset();Registry::TrackResources();
    }
    Require(Registry::GetTextures().empty(),"registry shutdown leak");
    std::puts("PASS: domain close hides retained IDs, post-close wrapper removal and same-address construction in a new domain preserve generation identity");return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
