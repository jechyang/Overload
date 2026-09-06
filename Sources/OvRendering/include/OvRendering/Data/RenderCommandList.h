#pragma once

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
        RenderCommandList() = default;
        RenderCommandList(const RenderCommandList&) = delete;
        RenderCommandList& operator=(const RenderCommandList&) = delete;
        RenderCommandList(RenderCommandList&& other) noexcept :
            m_operations(std::move(other.m_operations)), m_state(other.m_state), m_executed(other.m_executed)
        { other.m_state = State::CANCELLED; other.m_executed = 0; }
        RenderCommandList& operator=(RenderCommandList&&) = delete;

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
            m_state = State::FAILED;
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
            m_state = State::CANCELLED;
            m_operations.clear();
        }
        void Execute(Context::Driver& driver)
        {
            if (m_state != State::READY) throw std::logic_error("Command list is not ready");
            m_state = State::EXECUTING;
            try
            {
                for (const auto& operation : m_operations) { operation(driver); ++m_executed; }
                m_state = State::COMPLETED;
                m_operations.clear();
            }
            catch (...)
            {
                m_state = State::FAILED;
                m_operations.clear();
                throw;
            }
        }
        State GetState() const { return m_state; }
        size_t GetPendingCount() const { return m_operations.size(); }
        size_t GetExecutedCount() const { return m_executed; }
    private:
        std::vector<Operation> m_operations;
        State m_state = State::RECORDING;
        size_t m_executed = 0;
    };
}
