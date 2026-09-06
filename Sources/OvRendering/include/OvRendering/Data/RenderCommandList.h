#pragma once

#include <atomic>
#include <memory>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace OvRendering::Context { class Driver; }

namespace OvRendering::Data
{
    // Single producer, then exclusive consumer. Moving publishes ownership; it does
    // not synchronize threads. Captures must own every resource used by execution.
    // Execute/cancel/destruction of GPU captures currently require the GL owner.
    class RenderCommandList
    {
    public:
        enum class State { RECORDING, READY, EXECUTING, COMPLETED, FAILED, CANCELLED };
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
            m_completion(std::move(other.m_completion)), m_histories(std::move(other.m_histories))
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
                m_histories.push_back({epoch, std::move(previous)});
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
        void FailRecording()
        {
            if (m_state == State::FAILED) return;
            if (m_state != State::RECORDING) throw std::logic_error("Cannot fail a sealed recording");
            SetTerminal(State::FAILED);
            m_operations.clear();
        }
        void Seal()
        {
            if (m_state != State::RECORDING) throw std::logic_error("Command list cannot be sealed");
            m_state = State::READY;
        }
        void Cancel()
        {
            if (m_state != State::RECORDING && m_state != State::READY) throw std::logic_error("Command list cannot be cancelled");
            SetTerminal(State::CANCELLED);
            m_operations.clear();
        }
        void Execute(Context::Driver& driver)
        {
            if (m_state != State::READY) throw std::logic_error("Command list is not ready");
            m_state = State::EXECUTING;
            try
            {
                // Reject invalid or out-of-order histories before ANY GPU operation.
                // The queue must preserve submission order; there is no consumer wait.
                for (const auto& history : m_histories)
                    if (history.epoch->load(std::memory_order_acquire) ||
                        (history.previous && history.previous->load(std::memory_order_acquire) != State::COMPLETED))
                        throw std::logic_error("Temporal history is invalid or out of order");
                for (const auto& operation : m_operations) { operation(driver); ++m_executed; }
                SetTerminal(State::COMPLETED);
                m_operations.clear();
            }
            catch (...)
            {
                SetTerminal(State::FAILED);
                m_operations.clear();
                throw;
            }
        }
        State GetState() const { return m_state; }
        size_t GetPendingCount() const { return m_operations.size(); }
        size_t GetExecutedCount() const { return m_executed; }
    private:
        void SetTerminal(State state) noexcept
        {
            if (state != State::COMPLETED)
                for (const auto& history : m_histories) history.epoch->store(true, std::memory_order_release);
            m_state = state;
            m_completion->store(state, std::memory_order_release);
            m_histories.clear();
        }
        struct HistoryUse
        {
            std::shared_ptr<std::atomic_bool> epoch;
            std::shared_ptr<std::atomic<State>> previous;
        };
        std::vector<Operation> m_operations;
        State m_state = State::RECORDING;
        size_t m_executed = 0;
        std::shared_ptr<std::atomic<State>> m_completion = std::make_shared<std::atomic<State>>(State::RECORDING);
        std::vector<HistoryUse> m_histories;
    };
}
