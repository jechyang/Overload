#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>
#include <exception>
namespace OvTools::Jobs { class JobSystem; }

namespace OvRendering::Context { class Driver; }

namespace OvRendering::Data
{
    // Single producer, then exclusive consumer. Moving publishes ownership; it does
    // not synchronize threads. Captures must own every resource used by execution.
    // Execute/cancel/destruction of GPU captures currently require the GL owner.
    class RenderFrame;
    class RenderCommandList
    {
    public:
        friend class RenderFrame;
        enum class State { RECORDING, READY, EXECUTING, COMPLETED, FAILED, CANCELLED };
        // Safe to poll from the producer after ownership transfer. This observes
        // CPU command completion, not a GPU fence or a completed presentation.
        class Completion
        {
            friend class RenderCommandList;
        public:
            Completion() = default;
            bool Succeeded() const
            { return m_state && m_state->load(std::memory_order_acquire) == State::COMPLETED; }
        private:
            explicit Completion(std::shared_ptr<std::atomic<State>> state) : m_state(std::move(state)) {}
            std::shared_ptr<std::atomic<State>> m_state;
        };
        Completion GetCompletion() const { return Completion(m_completion); }
        using Operation = std::function<void(Context::Driver&)>;
        // Producer-owned history cursor. Only the shared status/epoch is touched by
        // the consumer; it never retains or calls the scene/effect owning this cursor.
        class History
        {
            friend class RenderCommandList;
        public:
            History() = default;
            History(const History&) = delete;
            History& operator=(const History&) = delete;
            bool PrepareImmediate()
            {
                if (m_epoch && m_epoch->load(std::memory_order_acquire))
                {
                    m_epoch.reset(); m_previous.reset();
                    return true;
                }
                if (m_previous && m_previous->load(std::memory_order_acquire) != State::COMPLETED)
                    throw std::logic_error("Temporal history has an outstanding recording");
                m_previous.reset();
                return false;
            }
        private:
            std::shared_ptr<std::atomic_bool> m_epoch;
            std::shared_ptr<std::atomic<State>> m_previous;
        };

        RenderCommandList() = default;
        ~RenderCommandList()
        {
            if (m_state == State::RECORDING || m_state == State::READY) SetTerminal(State::CANCELLED);
        }
        RenderCommandList(const RenderCommandList&) = delete;
        RenderCommandList& operator=(const RenderCommandList&) = delete;
        RenderCommandList(RenderCommandList&& other) noexcept :
            m_operations(std::move(other.m_operations)), m_state(other.m_state), m_executed(other.m_executed),
            m_completion(std::move(other.m_completion)), m_histories(std::move(other.m_histories)),
            m_children(std::move(other.m_children)), m_deferred(std::move(other.m_deferred))
        { other.m_state = State::CANCELLED; other.m_executed = 0; }
        RenderCommandList& operator=(RenderCommandList&&) = delete;

        // Returns true when the caller must discard speculative CPU history and
        // prepare a full initialization. Register BEFORE advancing that history.
        bool UseHistory(History& history)
        {
            if (m_state != State::RECORDING) throw std::logic_error("Command list is not recording");
            if (history.m_previous == m_completion) return false;
            try
            {
                const bool reset = history.m_epoch && history.m_epoch->load(std::memory_order_acquire);
                auto epoch = !history.m_epoch || reset ? std::make_shared<std::atomic_bool>(false) : history.m_epoch;
                auto previous = reset ? nullptr : history.m_previous;
                if (previous && previous->load(std::memory_order_acquire) == State::COMPLETED) previous.reset();
                const bool internal = std::find(m_children.begin(), m_children.end(), previous) != m_children.end();
                m_histories.push_back({epoch, std::move(previous), internal});
                history.m_epoch = std::move(epoch);
                history.m_previous = m_completion;
                return reset;
            }
            catch (...) { FailRecording(); throw; }
        }

        void Append(Operation operation)
        {
            if (m_state != State::RECORDING || !operation) throw std::logic_error("Command list is not recording");
            try { m_operations.push_back(std::move(operation)); }
            catch (...) { FailRecording(); throw; }
        }
        using OperationFactory = std::function<Operation()>;
        // Reserve the exact execution position now; encode only owned, immutable
        // inputs later. Each factory has an exclusive output slot, never the parent
        // command buffer. GPU read/write dependencies remain in append order.
        void AppendDeferred(OperationFactory factory)
        {
            if (m_state != State::RECORDING || !factory) throw std::logic_error("Invalid deferred recording");
            try
            {
                auto slot = std::make_shared<Deferred>();
                slot->factory = std::move(factory);
                m_deferred.push_back(slot);
                Append([slot](Context::Driver& driver) { slot->operation(driver); });
            }
            catch (...) { FailRecording(); throw; }
        }
        // Group an already-recorded suffix without executing or borrowing its
        // operations. Used for lexical GPU scopes that must live on the consumer.
        using OperationGroup = std::vector<Operation>;
        using GroupWrapper = std::function<void(Context::Driver&, const OperationGroup&)>;
        void WrapOperations(size_t first, GroupWrapper wrapper)
        {
            if (m_state != State::RECORDING || first > m_operations.size() || !wrapper)
                throw std::logic_error("Invalid command scope");
            if (first == m_operations.size()) return;
            try
            {
                OperationGroup group;
                group.reserve(m_operations.size() - first);
                for (size_t i = first; i < m_operations.size(); ++i)
                    group.push_back(std::move(m_operations[i]));
                Operation operation = [group = std::move(group), wrapper = std::move(wrapper)](Context::Driver& driver) {
                    wrapper(driver, group);
                };
                m_operations.resize(first);
                m_operations.push_back(std::move(operation));
            }
            catch (...) { FailRecording(); throw; }
        }
        // Transfer a sealed list into this sequence. All child completion tokens
        // publish only after the whole sequence succeeds or fails.
        void Append(RenderCommandList&& child) { AppendList(std::move(child), false); }
        // Transfer prepared CPU inputs as well, so one frame-level dependency graph
        // can encode several views together. The parent must Seal before submission.
        void AppendRecording(RenderCommandList&& child) { AppendList(std::move(child), true); }
    private:
        void AppendList(RenderCommandList&& child, bool allowRecording)
        {
            if (&child == this || m_state != State::RECORDING ||
                (child.m_state != State::READY && !(allowRecording && child.m_state == State::RECORDING)))
                throw std::logic_error("Only a separate ready list can be appended");
            try
            {
                m_operations.reserve(m_operations.size() + child.m_operations.size());
                m_deferred.reserve(m_deferred.size() + child.m_deferred.size());
                m_histories.reserve(m_histories.size() + child.m_histories.size());
                m_children.reserve(m_children.size() + child.m_children.size() + 1);
                for (auto& history : child.m_histories)
                {
                    if (history.previous == m_completion ||
                        std::find(m_children.begin(), m_children.end(), history.previous) != m_children.end())
                        history.internalPredecessor = true;
                    m_histories.push_back(std::move(history));
                }
                for (auto& operation : child.m_operations) m_operations.push_back(std::move(operation));
                for (auto& deferred : child.m_deferred) m_deferred.push_back(std::move(deferred));
                for (auto& completion : child.m_children) m_children.push_back(std::move(completion));
                m_children.push_back(std::move(child.m_completion));
                child.m_operations.clear(); child.m_deferred.clear(); child.m_histories.clear(); child.m_children.clear();
                child.m_state = State::CANCELLED; // Ownership transferred, not a cancellation publication.
            }
            catch (...) { FailRecording(); child.Cancel(); throw; }
        }
    public:
        void FailRecording()
        {
            if (m_state == State::FAILED) return;
            if (m_state != State::RECORDING) throw std::logic_error("Cannot fail a sealed recording");
            SetTerminal(State::FAILED);
            m_operations.clear(); m_deferred.clear();
        }
        void Seal(OvTools::Jobs::JobSystem* jobs = nullptr)
        {
            if (m_state != State::RECORDING) throw std::logic_error("Command list cannot be sealed");
            try { EncodeDeferred(jobs); m_state = State::READY; }
            catch (...) { FailRecording(); throw; }
        }
        void Cancel()
        {
            if (m_state != State::RECORDING && m_state != State::READY) throw std::logic_error("Command list cannot be cancelled");
            SetTerminal(State::CANCELLED);
            m_operations.clear(); m_deferred.clear();
        }
        void Execute(Context::Driver& driver) { ExecuteRetained(driver, {}, false); }
        State GetState() const { return m_state; }
        size_t GetPendingCount() const { return m_operations.size(); }
        size_t GetExecutedCount() const { return m_executed; }
    private:
        void ExecuteRetained(Context::Driver& driver, const std::function<void()>& finalize, bool retain)
        {
            if (m_state != State::READY) throw std::logic_error("Command list is not ready");
            m_state = State::EXECUTING;
            try
            {
                // Reject invalid or out-of-order histories before ANY GPU operation.
                // The queue must preserve submission order; there is no consumer wait.
                for (const auto& history : m_histories)
                    if (history.epoch->load(std::memory_order_acquire) ||
                        (!history.internalPredecessor && history.previous && history.previous->load(std::memory_order_acquire) != State::COMPLETED))
                        throw std::logic_error("Temporal history is invalid or out of order");
                for (const auto& operation : m_operations) { operation(driver); ++m_executed; }
                if (finalize) finalize();
                SetTerminal(State::COMPLETED, retain);
                if (!retain) m_operations.clear();
            }
            catch (...)
            {
                SetTerminal(State::FAILED, retain);
                if (!retain) m_operations.clear();
                throw;
            }
        }
        void ReleaseRetained() noexcept
        { m_operations.clear(); m_deferred.clear(); m_histories.clear(); m_children.clear(); }
        void InvalidateHistories() noexcept
        { for (const auto& history : m_histories) history.epoch->store(true, std::memory_order_release); }
        void SetTerminal(State state, bool retain = false) noexcept
        {
            if (state != State::COMPLETED)
                for (const auto& history : m_histories) history.epoch->store(true, std::memory_order_release);
            m_state = state;
            for (const auto& child : m_children) child->store(state, std::memory_order_release);
            m_completion->store(state, std::memory_order_release);
            if (!retain) { m_histories.clear(); m_children.clear(); }
        }
        struct Deferred
        {
            OperationFactory factory;
            Operation operation;
        };
        void EncodeDeferred(OvTools::Jobs::JobSystem* jobs);
        struct HistoryUse
        {
            std::shared_ptr<std::atomic_bool> epoch;
            std::shared_ptr<std::atomic<State>> previous;
            bool internalPredecessor = false;
        };
        std::vector<Operation> m_operations;
        State m_state = State::RECORDING;
        size_t m_executed = 0;
        std::shared_ptr<std::atomic<State>> m_completion = std::make_shared<std::atomic<State>>(State::RECORDING);
        std::vector<HistoryUse> m_histories;
        std::vector<std::shared_ptr<std::atomic<State>>> m_children;
        std::vector<std::shared_ptr<Deferred>> m_deferred;
    };
}
