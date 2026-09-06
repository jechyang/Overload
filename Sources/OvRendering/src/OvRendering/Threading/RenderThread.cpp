#include <OvRendering/Threading/RenderThread.h>
#include <OvRendering/Context/Driver.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>
#include <tracy/Tracy.hpp>

namespace OvRendering::Threading
{
    struct RenderThread::Storage
    {
        struct Work
        {
            std::optional<Data::RenderFrame> frame;
            std::function<void()> present;
            std::function<void(Context::Driver&)> task;
        };
        Initialize initialize;
        Finalize finalize;
        std::unique_ptr<Context::Driver> driver;
        std::thread worker;
        std::thread::id owner;
        std::atomic_bool running{false};
        mutable std::mutex mutex;
        std::condition_variable changed;
        std::deque<std::unique_ptr<Work>> queue;
        std::vector<std::unique_ptr<Work>> flying;
        size_t frames = 0, tasks = 0, maxFrames, maxTasks;
        bool closed = false, stopping = false;
        std::exception_ptr error;
        Storage(Initialize init, Finalize end, size_t framesLimit, size_t tasksLimit) :
            initialize(std::move(init)), finalize(std::move(end)), maxFrames(framesLimit), maxTasks(tasksLimit)
        { flying.reserve(maxFrames); }
    };

    RenderThread::RenderThread(Initialize initialize, Finalize finalize, size_t maxFrames, size_t maxTasks) :
        m_storage(std::make_unique<Storage>(std::move(initialize), std::move(finalize), maxFrames, maxTasks))
    {
        if (!m_storage->initialize || !maxFrames || !maxTasks)
            throw std::invalid_argument("Render thread requires initialization and positive capacities");
        std::promise<void> started;
        auto ready = started.get_future();
        m_storage->worker = std::thread([this, started = std::move(started)]() mutable { Run(std::move(started)); });
        try { ready.get(); }
        catch (...) { m_storage->worker.join(); throw; }
    }
    RenderThread::~RenderThread() { Shutdown(); }
    bool RenderThread::IsOwnerThread() const noexcept
    { return m_storage->running.load(std::memory_order_acquire) && std::this_thread::get_id() == m_storage->owner; }

    void RenderThread::Enqueue(std::function<void(Context::Driver&)> task)
    {
        auto& s = *m_storage;
        if (IsOwnerThread()) { task(*s.driver); return; }
        auto work = std::make_unique<Storage::Work>();
        work->task = std::move(task);
        std::unique_lock lock(s.mutex);
        s.changed.wait(lock, [&] { return s.stopping || s.tasks < s.maxTasks; });
        if (s.stopping) throw std::logic_error("Render thread is shut down");
        s.queue.push_back(std::move(work));
        ++s.tasks;
        s.changed.notify_all();
    }
    Data::RenderFrame::Completion RenderThread::Submit(Data::RenderFrame&& frame, std::function<void()> present)
    {
        if (IsOwnerThread()) throw std::logic_error("Render owner cannot wait for frame capacity");
        if (frame.GetCompletion().GetState() != Data::RenderFrame::State::READY)
            throw std::logic_error("Render thread requires a sealed frame");
        auto& s = *m_storage;
        auto work = std::make_unique<Storage::Work>();
        auto completion = frame.GetCompletion();
        std::unique_lock lock(s.mutex);
        s.changed.wait(lock, [&] { return s.closed || s.frames < s.maxFrames; });
        if (s.closed) throw std::logic_error("Render frame admission is closed");
        // Allocate queue storage before consuming caller-owned GPU captures.
        s.queue.push_back(std::move(work));
        s.queue.back()->present = std::move(present);
        s.queue.back()->frame.emplace(std::move(frame));
        ++s.frames;
        s.changed.notify_all();
        return completion;
    }
    void RenderThread::Discard(Data::RenderFrame&& frame)
    {
        // Invoke waits, so the reference remains live even if capacity is full.
        Invoke([&frame](Context::Driver&) { frame.Cancel(); });
    }
    void RenderThread::Close()
    {
        auto& s = *m_storage;
        std::lock_guard lock(s.mutex); s.closed = true; s.changed.notify_all();
    }
    void RenderThread::WaitIdle()
    {
        if (IsOwnerThread()) throw std::logic_error("Render owner cannot wait for itself");
        auto& s = *m_storage;
        std::unique_lock lock(s.mutex);
        s.changed.wait(lock, [&] { return s.frames == 0 && s.tasks == 0; });
    }
    void RenderThread::RethrowIfFailed() const
    {
        std::exception_ptr error;
        { std::lock_guard lock(m_storage->mutex); error = m_storage->error; }
        if (error) std::rethrow_exception(error);
    }
    void RenderThread::Shutdown()
    {
        if (IsOwnerThread()) throw std::logic_error("Render owner cannot join itself");
        auto& s = *m_storage;
        { std::lock_guard lock(s.mutex); s.closed = s.stopping = true; s.changed.notify_all(); }
        if (s.worker.joinable()) s.worker.join();
    }
    void RenderThread::Run(std::promise<void> started) noexcept
    {
        auto& s = *m_storage;
        s.owner = std::this_thread::get_id();
        tracy::SetThreadName("Render");
        try
        {
            s.driver = s.initialize();
            if (!s.driver) throw std::runtime_error("Render initialization returned no driver");
            s.initialize = {}; // Release initialization captures on the owner.
            s.running.store(true, std::memory_order_release);
            started.set_value();
        }
        catch (...)
        {
            const auto error = std::current_exception();
            s.initialize = {}; s.driver.reset();
            try { if (s.finalize) s.finalize(); } catch (...) {}
            s.finalize = {};
            started.set_exception(error);
            return;
        }
        const auto fail = [&](std::exception_ptr error) {
            {
                std::lock_guard lock(s.mutex);
                if (!s.error) s.error = error;
                s.closed = true;
                s.changed.notify_all();
            }
            // Conservative queue failure: drain submitted successors but never
            // publish them as successful after a failed predecessor/history.
            for (auto& work : s.flying) work->frame->InvalidateSubmitted(error);
        };
        for (;;)
        {
            ZoneScopedN("Render queue iteration");
            // Retire in order so a late earlier failure cannot follow a published
            // successful successor. Poll never blocks for GPU completion.
            while (!s.flying.empty() && s.flying.front()->frame->Poll())
            {
                auto completion = s.flying.front()->frame->GetCompletion();
                try { completion.RethrowIfFailed(); } catch (...) { fail(std::current_exception()); }
                s.flying.erase(s.flying.begin());
                { std::lock_guard lock(s.mutex); --s.frames; s.changed.notify_all(); }
            }
            std::unique_ptr<Storage::Work> work;
            bool failed = false;
            {
                std::unique_lock lock(s.mutex);
                if (s.queue.empty())
                {
                    if (s.stopping && !s.frames) break;
                    if (s.flying.empty()) s.changed.wait(lock, [&] { return s.stopping || !s.queue.empty(); });
                    else s.changed.wait_for(lock, std::chrono::milliseconds(1), [&] { return !s.queue.empty(); });
                    continue;
                }
                work = std::move(s.queue.front()); s.queue.pop_front();
                failed = static_cast<bool>(s.error);
            }
            if (work->frame)
            {
                if (failed)
                {
                    work->frame->Cancel(); work.reset();
                    std::lock_guard lock(s.mutex); --s.frames; s.changed.notify_all();
                }
                else
                {
                    const bool submitted = work->frame->Submit(*s.driver, work->present);
                    s.flying.push_back(std::move(work)); // Reserved to maxFrames.
                    if (!submitted) fail(std::make_exception_ptr(std::runtime_error("Render frame submission failed")));
                }
            }
            else
            {
                work->task(*s.driver); // Packaged task transports exceptions.
                work.reset(); // Release captures before publishing idle/capacity.
                std::lock_guard lock(s.mutex); --s.tasks; s.changed.notify_all();
            }
        }
        s.driver.reset();
        try { if (s.finalize) s.finalize(); }
        catch (...) { std::lock_guard lock(s.mutex); if (!s.error) s.error = std::current_exception(); }
        s.finalize = {};
        s.running.store(false, std::memory_order_release);
    }
}
