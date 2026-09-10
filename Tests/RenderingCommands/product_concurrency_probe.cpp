// Linked only by product validation. Production sources get entry/exit probes in
// the build directory; their preparation and recording bodies remain unchanged.
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <thread>
#include <chrono>
#include <stdexcept>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
namespace
{
    const auto mainThread=std::this_thread::get_id();
    struct Phase { bool complete=false;std::array<bool,2> entered{};std::array<std::thread::id,2> threads{};size_t calls=0; };
    std::mutex mutex;std::condition_variable changed;std::array<Phase,2> phases;
    void Verify()
    {
        for(size_t i=0;i<phases.size();++i)
            if(!phases[i].complete){std::fprintf(stderr,"FAIL: product phase %zu did not overlap on two Workers\n",i+1);std::_Exit(1);}
        std::printf("PASS: actual product Prepare and Record overlapped on distinct Workers without GL contexts (%zu/%zu calls)\n",phases[0].calls,phases[1].calls);
    }
    struct Verification { Verification(){std::atexit(Verify);} } verification;
}
void ProductTaskEntry(int phase,size_t slot,size_t count)
{
    if(std::this_thread::get_id()==mainThread)return;
    if(glfwGetCurrentContext())throw std::runtime_error("Product Worker has a GL context");
    std::unique_lock lock(mutex);
    auto& state=phases.at(phase-1);++state.calls;
    if(state.complete||count<2||slot>1)return;
    state.entered[slot]=true;state.threads[slot]=std::this_thread::get_id();changed.notify_all();
    if(!changed.wait_for(lock,std::chrono::seconds(5),[&]{return state.entered[0]&&state.entered[1];}))
        throw std::runtime_error("Product Worker overlap gate timeout");
    if(state.threads[0]==state.threads[1])throw std::runtime_error("Product independent tasks used one Worker");
    state.complete=true;changed.notify_all();
}
