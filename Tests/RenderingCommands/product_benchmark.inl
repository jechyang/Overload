#pragma once
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <vector>
namespace Benchmark
{
    using Clock=std::chrono::steady_clock;
    uint64_t Now(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());}
    struct MainSample {uint64_t start=0,updated=0,end=0,observed=0,applied=0,waitStart=0,admitted=0;size_t queued=0,inFlight=0;};
    struct GpuSample {uint64_t begin=0,beforePresent=0,afterPresent=0,retired=0,gpuBegin=0,gpuEnd=0;};
    struct Run
    {
        uint64_t first;std::vector<MainSample> main;std::vector<GpuSample> gpu;std::vector<GLuint> queries;
        uint64_t currentProducer=0,finished=0;bool immediate=false;
        Run(uint64_t id,size_t count,bool direct):first(id),main(count),gpu(count),queries(count*2),immediate(direct){}
        bool Contains(uint64_t id)const{return id>=first&&id-first<main.size();}
    };
    std::atomic<Run*> active{nullptr};
    thread_local uint64_t consuming=0;
    void Begin(uint64_t id)
    {
        auto* run=active.load();if(!run||!run->Contains(id))return;
        consuming=id;auto& item=run->gpu[id-run->first];item.begin=Now();
        glQueryCounter(run->queries[(id-run->first)*2],GL_TIMESTAMP);
    }
    void BeforePresent()
    {
        auto* run=active.load();if(!run||!run->Contains(consuming))return;
        auto& item=run->gpu[consuming-run->first];
        glQueryCounter(run->queries[(consuming-run->first)*2+1],GL_TIMESTAMP);item.beforePresent=Now();
    }
    void AfterPresent()
    {
        auto* run=active.load();if(run&&run->Contains(consuming))run->gpu[consuming-run->first].afterPresent=Now();
    }
    void Retired(uint64_t id)
    {
        auto* run=active.load();if(run&&run->Contains(id))run->gpu[id-run->first].retired=Now();
    }
    void WaitStart(uint64_t id)
    {
        auto* run=active.load();if(run&&run->Contains(id))run->main[id-run->first].waitStart=Now();
    }
    void Admitted(uint64_t id,size_t queued,size_t inFlight)
    {
        auto* run=active.load();if(!run||!run->Contains(id))return;
        auto& row=run->main[id-run->first];row.admitted=Now();row.queued=queued;row.inFlight=inFlight;
    }
    void Header(std::ostream& out)
    {
        out<<"product,mode,repeat,frame,main_start_ns,main_updated_ns,main_end_ns,render_begin_ns,pre_present_ns,present_return_ns,gpu_begin_ns,gpu_end_ns,retired_ns,completion_observed_ns,interaction_observed_ns,wait_start_ns,admitted_ns,queued_at_admission,inflight_at_admission,run_tail_ns\n";
    }
    void Write(std::ostream& out,const char* product,const char* mode,int repeat,const Run& run,size_t warmup)
    {
        for(size_t i=warmup;i<run.main.size();++i)
        {
            const auto& m=run.main[i];const auto& g=run.gpu[i];
            Require(m.start&&m.updated>=m.start&&m.end>=m.updated&&g.begin>=m.start&&g.beforePresent>=g.begin&&g.afterPresent>=g.beforePresent&&g.gpuEnd>=g.gpuBegin,"incomplete benchmark timing boundaries");
            if(!run.immediate)Require(g.retired>=g.afterPresent&&m.observed>=g.retired,"benchmark missed GPU retirement observation");
            if(m.admitted)Require(m.waitStart>=m.updated&&m.admitted>=m.waitStart&&g.begin>=m.admitted&&m.queued>=1&&m.queued<=m.inFlight&&m.inFlight<=2,"benchmark queue boundaries/capacity changed");
            out<<product<<','<<mode<<','<<repeat<<','<<run.first+i<<','<<m.start<<','<<m.updated<<','<<m.end<<','<<g.begin<<','<<g.beforePresent<<','<<g.afterPresent<<','<<g.gpuBegin<<','<<g.gpuEnd<<','<<g.retired<<','<<m.observed<<','<<m.applied<<','<<m.waitStart<<','<<m.admitted<<','<<m.queued<<','<<m.inFlight<<','<<run.finished<<'\n';
        }
        out.flush();
    }
    template<class Product,class Context,class Prepare,class Observe> void Measure(Product& product,Context& context,Run& run,Prepare prepare,Observe observe)
    {
        OnOwner(context,[&](auto&){glGenQueries(GLsizei(run.queries.size()),run.queries.data());});
        struct Cleanup
        {
            Product& product;Context& context;Run& run;
            ~Cleanup()
            {
                try{product.DrainRenderFrames();OnOwner(context,[](auto&){glFinish();});}catch(...){}
                active=nullptr;
                OnOwner(context,[&](auto&){beforePresent={};glDeleteQueries(GLsizei(run.queries.size()),run.queries.data());});
            }
        } cleanup{product,context,run};
        beforePresent=BeforePresent;active=&run;
        std::vector<OvRendering::Data::RenderFrame::Completion> completions(run.main.size());
        size_t observed=0;
        const auto collect=[&]{
            while(observed<completions.size()&&completions[observed].IsRetired())
            {completions[observed].RethrowIfFailed();run.main[observed].observed=Now();++observed;}
            observe();
        };
        for(size_t i=0;i<run.main.size();++i)
        {
            ZoneScopedN("Benchmark Main Frame");
            run.currentProducer=run.first+i;run.main[i].start=Now();
            prepare(i,false);product.PreUpdate();collect();
            {ZoneScopedN("Benchmark Main Update");product.Update(1.f/60);}
            run.main[i].updated=Now();prepare(i,true);
            if(!run.immediate)completions[i]=product.m_pendingFrame->GetCompletion();
            product.PostUpdate();run.main[i].end=Now();
            if(run.immediate)run.main[i].observed=run.main[i].updated;
            else collect();
        }
        product.DrainRenderFrames();collect();
        OnOwner(context,[&](auto&){
            glFinish();run.finished=Now();
            for(size_t i=0;i<run.gpu.size();++i)
            {glGetQueryObjectui64v(run.queries[i*2],GL_QUERY_RESULT,&run.gpu[i].gpuBegin);glGetQueryObjectui64v(run.queries[i*2+1],GL_QUERY_RESULT,&run.gpu[i].gpuEnd);}
            Require(glGetError()==GL_NO_ERROR,"benchmark GPU timer error");
        });
        Require(run.immediate||observed==run.main.size(),"benchmark did not retire every frame");
    }
    template<class Work> void MeasureCase(int mode, Work work)
    {
        if(mode==0){ZoneScopedN("Benchmark Immediate Case");work();}
        else if(mode==1){ZoneScopedN("Benchmark Commands Case");work();}
        else {ZoneScopedN("Benchmark Threaded Case");work();}
    }
    bool IsCapture(int argc,char** argv){return argc>2&&std::string(argv[2])=="--capture";}
    const char* MetricsFile(int argc,char** argv){return IsCapture(argc,argv)?"product-frame-metrics-connected.csv":"product-frame-metrics.csv";}
    void CaptureWait(int argc,char** argv)
    {
        if(!IsCapture(argc,argv))return;
        std::puts("TRACE READY");std::fflush(stdout);
        const auto deadline=Clock::now()+std::chrono::seconds(30);
        while(!tracy::GetProfiler().IsConnected()&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Require(tracy::GetProfiler().IsConnected(),"benchmark Tracy connection timeout");
    }
    void Configure(const std::filesystem::path& path,int mode)
    {
        OvTools::Filesystem::IniFile settings(path.string());
        settings.Remove("render_thread");settings.Add("render_thread",mode==2);
        settings.Remove("render_commands");settings.Add("render_commands",mode!=0);settings.Rewrite();
    }
    constexpr const char* modes[]{"immediate","commands","threaded"};
}
void BenchmarkBeforeSubmit(uint64_t id){Benchmark::Begin(id);}
void BenchmarkBeforeImmediateRender(){if(auto* run=Benchmark::active.load())Benchmark::Begin(run->currentProducer);}
void BenchmarkAfterPresent(){Benchmark::AfterPresent();}
void BenchmarkRetired(uint64_t id){Benchmark::Retired(id);}
void BenchmarkWaitStart(uint64_t id){Benchmark::WaitStart(id);}
void BenchmarkAdmitted(uint64_t id,size_t queued,size_t flying){Benchmark::Admitted(id,queued,flying);}
