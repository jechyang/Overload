#include <OvRendering/Data/FrameGraphGpuTimings.h>
#include <baregl/ResourceLifetime.h>
#include <baregl/detail/glad/glad.h>
#include <array>

using namespace OvRendering::Data;
namespace
{
    struct Query
    {
        GLuint id=0;
        baregl::ResourceLifetime lifetime{baregl::ResourceKind::QUERY};
    };
    struct Slot
    {
        std::atomic_bool busy{false};
        std::atomic_size_t queryCount{0};
        // Below is accessed only by the GL consumer, until fenced retirement.
        std::vector<std::unique_ptr<Query>> queries;
        std::shared_ptr<const void> domain;
        std::thread::id owner;
        bool Ensure(size_t count) {
            const auto current=baregl::ResourceLifetime::CurrentDomain();
            if(domain!=current){queries.clear();queryCount.store(0);owner={};domain=current;}
            if(owner==std::thread::id{})owner=std::this_thread::get_id();
            if(owner!=std::this_thread::get_id())throw std::logic_error("GPU timing query escaped its GL owner");
            GLint bits=0;glGetQueryiv(GL_TIMESTAMP,GL_QUERY_COUNTER_BITS,&bits);if(bits<=0)return false;
            while(queries.size()<count) {
                auto query=std::make_unique<Query>();glGenQueries(1,&query->id);query->lifetime.Adopt(query->id);
                if(!query->id)return false;queries.push_back(std::move(query));
            }
            queryCount.store(queries.size(),std::memory_order_release);return true;
        }
    };
}
struct FrameGraphGpuTimer::State
{
    std::array<std::shared_ptr<Slot>,MaxPending> slots;
    uint64_t samples=0,skipped=0;
    State(){for(auto& slot:slots)slot=std::make_shared<Slot>();}
};
struct FrameGraphGpuTimingCapture::State
{
    std::shared_ptr<Slot> slot;
    std::shared_ptr<FrameGraphGpuTimings> result;
    RenderFrame::Completion completion;
    std::vector<bool> ended;
    std::atomic_bool released{false};
    bool valid=true,collected=false,initialized=false;
};
FrameGraphGpuTimer::FrameGraphGpuTimer():m_state(std::make_shared<State>()){}
void FrameGraphGpuTimer::RequireProducer() const
{if(std::this_thread::get_id()!=m_producer)throw std::logic_error("GPU timing admission requires its producer");}
std::shared_ptr<FrameGraphGpuTimingCapture> FrameGraphGpuTimer::Capture(std::shared_ptr<const FrameGraphDiagnostics> graph,RenderFrame& frame)
{
    RequireProducer();
    if(!graph||frame.GetCompletion().GetState()!=RenderFrame::State::RECORDING)throw std::invalid_argument("GPU timings require a graph and recording frame");
    if(graph->passes.empty()||graph->passes.size()>MaxPasses){++m_state->skipped;return {};}
    for(const auto& slot:m_state->slots) {
        bool expected=false;if(!slot->busy.compare_exchange_strong(expected,true,std::memory_order_acq_rel))continue;
        try {
            auto state=std::make_shared<FrameGraphGpuTimingCapture::State>();state->slot=slot;state->completion=frame.GetCompletion();
            state->result=std::make_shared<FrameGraphGpuTimings>();state->result->frameID=state->completion.GetFrameID();
            state->result->graph=std::move(graph);state->result->passNanoseconds.resize(state->result->graph->passes.size());state->ended.resize(state->result->passNanoseconds.size());
            auto capture=std::shared_ptr<FrameGraphGpuTimingCapture>(new FrameGraphGpuTimingCapture(std::move(state)));
            frame.OnRetired([capture](bool success){capture->Collect(success);});++m_state->samples;return capture;
        }catch(...){slot->busy.store(false,std::memory_order_release);throw;}
    }
    ++m_state->skipped;return {};
}
FrameGraphGpuTimer::Statistics FrameGraphGpuTimer::GetStatistics() const
{
    RequireProducer();Statistics result{m_state->samples,m_state->skipped};
    for(const auto& slot:m_state->slots){result.pending+=slot->busy.load(std::memory_order_acquire);result.queries+=slot->queryCount.load(std::memory_order_acquire);}return result;
}
FrameGraphGpuTimingCapture::FrameGraphGpuTimingCapture(std::shared_ptr<State> state):m_state(std::move(state)){}
FrameGraphGpuTimingCapture::~FrameGraphGpuTimingCapture()
{if(!m_state->released.exchange(true))m_state->slot->busy.store(false,std::memory_order_release);}
bool FrameGraphGpuTimingCapture::IsRetired() const{return m_state->completion.IsRetired();}
std::shared_ptr<const FrameGraphGpuTimings> FrameGraphGpuTimingCapture::GetResult() const
{
    // GPU consumer writes the result before the frame's retired release-store.
    if(!m_state->completion.Succeeded()||!m_state->collected)return {};return m_state->result;
}
void FrameGraphGpuTimingCapture::Begin(size_t pass)
{
    auto& state=*m_state;if(!state.valid)return;
    try {
        if(pass>=state.ended.size())throw std::out_of_range("GPU timing Pass");
        if(!state.initialized){state.valid=state.slot->Ensure(state.ended.size()*2);state.initialized=true;}
        if(state.valid)glQueryCounter(state.slot->queries[pass*2]->id,GL_TIMESTAMP);
    }catch(...){state.valid=false;}
}
void FrameGraphGpuTimingCapture::End(size_t pass)
{
    auto& state=*m_state;if(!state.valid)return;
    try {glQueryCounter(state.slot->queries.at(pass*2+1)->id,GL_TIMESTAMP);state.ended.at(pass)=true;}catch(...){state.valid=false;}
}
void FrameGraphGpuTimingCapture::Collect(bool success) noexcept
{
    auto& state=*m_state;
    try {
        if(success&&state.valid&&state.initialized&&std::all_of(state.ended.begin(),state.ended.end(),[](bool value){return value;})) {
            bool available=true;
            for(size_t i=0;i<state.ended.size()*2;++i){GLint ready=0;glGetQueryObjectiv(state.slot->queries[i]->id,GL_QUERY_RESULT_AVAILABLE,&ready);available&=ready!=0;}
            if(available) {
                for(size_t p=0;p<state.ended.size();++p) {
                    GLuint64 start=0,end=0;glGetQueryObjectui64v(state.slot->queries[p*2]->id,GL_QUERY_RESULT,&start);glGetQueryObjectui64v(state.slot->queries[p*2+1]->id,GL_QUERY_RESULT,&end);
                    if(end<start){state.valid=false;break;}
                    state.result->passNanoseconds[p]=end-start;state.result->totalNanoseconds+=end-start;
                }
                state.collected=state.valid;
            }
        }
    }catch(...){state.collected=false;}
    state.released.store(true);state.slot->busy.store(false,std::memory_order_release);
}
