#pragma once
#include <OvRendering/Data/RenderFrame.h>
#include <future>
#include <thread>
#include <type_traits>

namespace OvRendering::Threading
{
    // One fixed GL owner. The caller detaches the context before construction;
    // initialize attaches it and creates Driver here, finalize detaches it after
    // Driver destruction. Window/event/UI operations remain on their own thread.
    class RenderThread final
    {
        struct Storage;
        std::unique_ptr<Storage> m_storage;
        void Run(std::promise<void> started) noexcept;
        void Enqueue(std::function<void(Context::Driver&)> task);
    public:
        using Initialize = std::function<std::unique_ptr<Context::Driver>()>;
        using Finalize = std::function<void()>;
        explicit RenderThread(Initialize initialize, Finalize finalize,
            size_t maxFrames = 2, size_t maxTasks = 64);
        ~RenderThread();
        RenderThread(const RenderThread&) = delete;
        RenderThread& operator=(const RenderThread&) = delete;

        bool IsOwnerThread() const noexcept;
        // Synchronous, bounded resource work. Nested owner calls execute inline.
        // Captures/results must obey GL ownership (no last GPU owner on producer).
        template<class F> auto Invoke(F&& function)
        {
            using Result = std::invoke_result_t<F, Context::Driver&>;
            static_assert(!std::is_reference_v<Result>, "Return an owned value from the Render thread");
            // MSVC future<T> requires move assignment internally; frames are
            // intentionally only move constructible to avoid off-owner release.
            using Transport = std::conditional_t<std::is_void_v<Result>, void, std::unique_ptr<Result>>;
            auto promise = std::make_shared<std::promise<Transport>>();
            auto result = promise->get_future();
            // Keep the callable outside the future's shared state, so its GPU
            // captures are destroyed on the owner even when get() runs elsewhere.
            Enqueue([promise = std::move(promise),
                callable = std::make_shared<std::decay_t<F>>(std::forward<F>(function))](Context::Driver& driver) mutable {
                try
                {
                    if constexpr (std::is_void_v<Result>) { (*callable)(driver); callable.reset(); promise->set_value(); }
                    else
                    {
                        auto value = std::make_unique<Result>((*callable)(driver));
                        callable.reset();
                        promise->set_value(std::move(value));
                    }
                }
                catch (...) { callable.reset(); promise->set_exception(std::current_exception()); }
            });
            if constexpr (std::is_void_v<Result>) result.get();
            else return Result(std::move(*result.get()));
        }
        // Blocks only on capacity, including GPU in-flight frames. On rejection
        // the argument is untouched. Successful admission transfers all captures.
        Data::RenderFrame::Completion Submit(Data::RenderFrame&& frame, std::function<void()> present = {});
        // Owner cancellation for a rejected/prepared frame; available after Close.
        void Discard(Data::RenderFrame&& frame);
        void Close(); // Wakes blocked Submit; resource cleanup remains available.
        void WaitIdle(); // Explicit drain, never called by normal Submit.
        void RethrowIfFailed() const;
        // Close, join producers, release resources via Invoke/Discard, then shut
        // down. Shutdown wakes calls already waiting for task capacity, drains
        // accepted work and joins. No concurrent Shutdown calls; object destruction
        // requires all external callers joined.
        void Shutdown();
    };
}
