#pragma once
#include <OvRendering/Data/RenderCommandList.h>
#include <exception>
#include <cstdint>

namespace OvRendering::Threading { class RenderThread; }

namespace OvRendering::Data
{
    // Exclusive producer then GL-owner consumer. The handoff itself requires an
    // external synchronization protocol. All retirement/destruction is GL-owner work.
    class RenderFrame
    {
        friend class Threading::RenderThread;
        void InvalidateSubmitted(std::exception_ptr error);
        struct Shared;
        struct Storage;
        std::unique_ptr<Storage> m_storage;
        void Retire(bool gpuSuccess) noexcept;
        bool ObserveFence(uint32_t result);
        void AppendImpl(RenderCommandList&& commands, bool prepared);
    public:
        enum class State { RECORDING, READY, SUBMITTED, COMPLETED, FAILED, CANCELLED };
        class Completion
        {
            friend class RenderFrame;
            std::shared_ptr<Shared> m_shared;
            explicit Completion(std::shared_ptr<Shared> shared) : m_shared(std::move(shared)) {}
        public:
            Completion() = default;
            bool operator==(const Completion&) const = default;
            State GetState() const;
            bool IsRetired() const; // CPU captures released after GPU drain, or no submission.
            bool Succeeded() const; // Successful CPU submission and observed GPU completion.
            bool PresentationRequested() const; // Swap callback returned; not physical display visibility.
            uint64_t GetFrameID() const;
            void RethrowIfFailed() const; // Requires retirement before reading the stored exception.
        };
        explicit RenderFrame(uint64_t frameID = 0);
        ~RenderFrame(); // Drains submitted work; queue users should Poll before destruction.
        RenderFrame(RenderFrame&&) noexcept;
        RenderFrame& operator=(RenderFrame&&) = delete;
        RenderFrame(const RenderFrame&) = delete;
        RenderFrame& operator=(const RenderFrame&) = delete;
        void Append(RenderCommandList&& commands);
        void AppendRecording(RenderCommandList&& commands);
        void Seal(OvTools::Jobs::JobSystem* jobs = nullptr);
        void Cancel();
        Completion GetCompletion() const;
        size_t GetExecutedCommandCount() const;
        // The synchronous consumer callback must present its live target/window.
        // Failure is stored; partially submitted captures still wait for GPU retirement.
        bool Submit(Context::Driver& driver, const std::function<void()>& present = {});
        bool Poll(); // Nonblocking; true when retired, including failed/cancelled frames.
        void Wait(); // Backpressure/shutdown path, not a per-frame queue requirement.
    };
}
