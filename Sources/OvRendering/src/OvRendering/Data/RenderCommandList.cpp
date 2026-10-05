/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/Data/RenderCommandList.h>
#include <OvRendering/Data/RenderFrame.h>
#include <OvTools/Jobs/JobSystem.h>
#include <tracy/Tracy.hpp>

void OvRendering::Data::RenderCommandList::Execute(Context::Driver& driver)
{
    if(m_retained.empty()){ExecuteRetained(driver,{},false);return;}
    if(m_state!=State::READY)throw std::logic_error("Command list is not ready");
    // Immediate lists with pooled storage use the existing frame fence protocol.
    // Queue submission continues to execute without waiting through RenderFrame.
    const auto completion=m_completion;
    RenderFrame frame;frame.Append(std::move(*this));frame.Seal();
    frame.Submit(driver);frame.Wait();
    m_completion=completion;m_executed=frame.GetExecutedCommandCount();
    m_state=completion->load(std::memory_order_acquire);
    if(!frame.GetCompletion().Succeeded()) {
        m_state=State::FAILED;m_completion->store(m_state,std::memory_order_release);
        frame.GetCompletion().RethrowIfFailed();
    }
}

void OvRendering::Data::RenderCommandList::EncodeDeferred(OvTools::Jobs::JobSystem* jobs)
{
    ZoneScopedN("Record Pass Commands");
    const size_t count = m_deferred.size();
    if (!count) return;
    const bool parallel = jobs && jobs->IsMainThread() && jobs->GetWorkerCount() >= 2 && count >= 128;
    const size_t chunks = parallel ? std::max<size_t>(2, std::min<size_t>((count + 127) / 128, std::min<uint32_t>(jobs->GetWorkerCount(), 4) + 1)) : 1;
    struct Result { std::exception_ptr error; bool complete = false; };
    std::vector<Result> results(chunks);
    std::vector<OvTools::Jobs::JobHandle> handles;
    handles.reserve(chunks + 1);
    auto encode = [&](size_t chunk) {
        ZoneScopedN("Record Pass Command Block");
        try
        {
            const size_t begin = count * chunk / chunks, end = count * (chunk + 1) / chunks;
            for (size_t i = begin; i < end; ++i)
            {
                auto& slot = *m_deferred[i];
                slot.operation = slot.factory();
                if (!slot.operation) throw std::logic_error("Recording produced an empty operation");
                slot.factory = {};
            }
            results[chunk].complete = true;
        }
        catch (...) { results[chunk].error = std::current_exception(); }
    };
    bool joined = false;
    try
    {
        if (parallel)
        {
            for (size_t chunk = 1; chunk < chunks; ++chunk)
                handles.push_back(jobs->Submit([&, chunk] { encode(chunk); }));
            encode(0); // Main helps only with this immutable block, never arbitrary jobs.
            // A real fan-in dependency: publication cannot precede any
            // private buffer. Failed jobs are terminal too, so inspect all
            // explicit results on Main after draining the dependency graph.
            handles.push_back(jobs->Submit([&] { joined = true; }, handles));
            { ZoneScopedN("Wait Pass Recording"); jobs->WaitAllPassive(handles); }
        }
        else { encode(0); joined = true; }
    }
    catch (...)
    {
        if (parallel) jobs->WaitAllPassive(handles);
        throw;
    }
    for (const auto& result : results)
    {
        if (result.error) std::rethrow_exception(result.error);
        if (!result.complete) throw std::runtime_error("Recording job retired without output");
    }
    if (!joined) throw std::runtime_error("Recording join retired without publication");
    { ZoneScopedN("Merge Pass Recording"); m_deferred.clear(); }
}
