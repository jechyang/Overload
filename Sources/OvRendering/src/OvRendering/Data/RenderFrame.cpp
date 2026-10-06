#include <OvRendering/Data/RenderFrame.h>
#include <OvRendering/Context/Driver.h>
#include <baregl/detail/glad/glad.h>

namespace OvRendering::Data
{
    struct RenderFrame::Shared
    {
        explicit Shared(uint64_t value) : id(value) {}
        const uint64_t id;
        std::atomic<State> state{State::RECORDING};
        std::atomic_bool retired{false}, presented{false};
        std::exception_ptr error; // Consumer writes; read only after retired acquire.
    };
    struct RenderFrame::Storage
    {
        explicit Storage(uint64_t id) : shared(std::make_shared<Shared>(id)) {}
        std::shared_ptr<Shared> shared;
        RenderCommandList commands;
        std::vector<std::function<void(bool)>> observers;
        GLsync fence = nullptr;
    };
    RenderFrame::RenderFrame(uint64_t id) : m_storage(std::make_unique<Storage>(id)) {}
    RenderFrame::RenderFrame(RenderFrame&&) noexcept = default;
    RenderFrame::~RenderFrame()
    {
        if (!m_storage) return;
        const auto state = m_storage->shared->state.load(std::memory_order_relaxed);
        if (state == State::RECORDING || state == State::READY) Cancel();
        else Wait();
    }
    RenderFrame::State RenderFrame::Completion::GetState() const
    { return m_shared ? m_shared->state.load(std::memory_order_acquire) : State::CANCELLED; }
    bool RenderFrame::Completion::IsRetired() const
    { return m_shared && m_shared->retired.load(std::memory_order_acquire); }
    bool RenderFrame::Completion::Succeeded() const
    { return IsRetired() && GetState() == State::COMPLETED; }
    bool RenderFrame::Completion::PresentationRequested() const
    { return m_shared && m_shared->presented.load(std::memory_order_acquire); }
    uint64_t RenderFrame::Completion::GetFrameID() const { return m_shared ? m_shared->id : 0; }
    void RenderFrame::Completion::RethrowIfFailed() const
    {
        if (!IsRetired()) throw std::logic_error("Frame is not retired");
        if (m_shared->error) std::rethrow_exception(m_shared->error);
    }
    RenderFrame::Completion RenderFrame::GetCompletion() const
    { return Completion(m_storage ? m_storage->shared : nullptr); }
    void RenderFrame::OnRetired(std::function<void(bool)> observer)
    {
        if(!m_storage||GetCompletion().GetState()!=State::RECORDING||!observer)throw std::invalid_argument("Retirement observer requires a recording frame");
        m_storage->observers.push_back(std::move(observer));
    }
    size_t RenderFrame::GetExecutedCommandCount() const { return m_storage?m_storage->commands.GetExecutedCount():0; }
    void RenderFrame::Append(RenderCommandList&& commands) { AppendImpl(std::move(commands), false); }
    void RenderFrame::AppendRecording(RenderCommandList&& commands) { AppendImpl(std::move(commands), true); }
    void RenderFrame::AppendImpl(RenderCommandList&& commands, bool prepared)
    {
        if (!m_storage || GetCompletion().GetState() != State::RECORDING)
            throw std::logic_error("Frame is not recording");
        try
        {
            if (prepared) m_storage->commands.AppendRecording(std::move(commands));
            else m_storage->commands.Append(std::move(commands));
        }
        catch (...)
        {
            if (m_storage->commands.GetState() == RenderCommandList::State::RECORDING)
                m_storage->commands.FailRecording();
            m_storage->shared->error = std::current_exception();
            m_storage->shared->state.store(State::FAILED, std::memory_order_release);
            Retire(false);
            throw;
        }
    }
    void RenderFrame::Seal(OvTools::Jobs::JobSystem* jobs)
    {
        if (!m_storage || GetCompletion().GetState() != State::RECORDING)
            throw std::logic_error("Frame cannot be sealed");
        try
        {
            m_storage->commands.Seal(jobs);
            m_storage->shared->state.store(State::READY, std::memory_order_release);
        }
        catch (...)
        {
            m_storage->shared->error = std::current_exception();
            m_storage->shared->state.store(State::FAILED, std::memory_order_release);
            Retire(false);
            throw;
        }
    }
    void RenderFrame::Cancel()
    {
        if (!m_storage || (GetCompletion().GetState() != State::RECORDING && GetCompletion().GetState() != State::READY))
            throw std::logic_error("Frame cannot be cancelled");
        m_storage->commands.Cancel();
        m_storage->shared->state.store(State::CANCELLED, std::memory_order_release);
        Retire(false);
    }
    bool RenderFrame::Submit(Context::Driver& driver, const std::function<void()>& present)
    {
        if (!m_storage || GetCompletion().GetState() != State::READY)
            throw std::logic_error("Frame is not ready");
        auto& storage = *m_storage;
        try
        {
            storage.commands.ExecuteRetained(driver, [&] {
                if (present) { present(); storage.shared->presented.store(true, std::memory_order_release); }
                driver.OnFrameCompleted();
                storage.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                if (!storage.fence) throw std::runtime_error("Cannot create frame GPU fence");
                glFlush();
            }, true);
            storage.shared->state.store(State::SUBMITTED, std::memory_order_release);
            return true;
        }
        catch (...)
        {
            storage.shared->error = std::current_exception();
            storage.shared->state.store(State::FAILED, std::memory_order_release);
            // Even a failing operation may have issued GPU work before throwing.
            if (!storage.fence) storage.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            if (storage.fence) glFlush();
            else { glFinish(); Retire(false); }
            return false;
        }
    }
    void RenderFrame::Retire(bool gpuSuccess) noexcept
    {
        auto& storage = *m_storage;
        if (!gpuSuccess) storage.commands.InvalidateHistories();
        const bool success=gpuSuccess&&storage.shared->state.load(std::memory_order_relaxed)==State::SUBMITTED;
        for(const auto& observer:storage.observers)try{observer(success);}catch(...){/* Diagnostics cannot fail a product frame. */}
        storage.observers.clear();
        storage.commands.ReleaseRetained();
        if (gpuSuccess && storage.shared->state.load(std::memory_order_relaxed) == State::SUBMITTED)
            storage.shared->state.store(State::COMPLETED, std::memory_order_release);
        storage.shared->retired.store(true, std::memory_order_release);
    }
    void RenderFrame::InvalidateSubmitted(std::exception_ptr error)
    {
        if (GetCompletion().IsRetired()) return;
        auto& storage = *m_storage;
        if (!storage.shared->error) storage.shared->error = error;
        storage.commands.InvalidateHistories();
        storage.shared->state.store(State::FAILED, std::memory_order_release);
    }
    bool RenderFrame::Poll()
    {
        if (!m_storage || GetCompletion().IsRetired()) return true;
        if (!m_storage->fence) return false;
        return ObserveFence(glClientWaitSync(m_storage->fence, 0, 0));
    }
    bool RenderFrame::ObserveFence(uint32_t result)
    {
        if (result == GL_TIMEOUT_EXPIRED) return false;
        const bool success = result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED;
        if (!success)
        {
            if (!m_storage->shared->error)
                m_storage->shared->error = std::make_exception_ptr(std::runtime_error("Frame GPU wait failed"));
            m_storage->shared->state.store(State::FAILED, std::memory_order_release);
            glFinish(); // Drain before dropping captures, while reporting failure.
        }
        glDeleteSync(m_storage->fence); m_storage->fence = nullptr;
        Retire(success);
        return true;
    }
    void RenderFrame::Wait()
    {
        if (!m_storage || GetCompletion().IsRetired()) return;
        if (!m_storage->fence) throw std::logic_error("Frame has not been submitted");
        // Explicit wait is reserved for backpressure/shutdown. Normal queues Poll.
        while (!ObserveFence(glClientWaitSync(m_storage->fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000))) {}
    }
}
