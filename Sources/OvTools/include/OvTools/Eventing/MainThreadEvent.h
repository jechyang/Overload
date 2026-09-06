#pragma once
#include <map>
#include <unordered_map>
#include <utility>
#include <exception>
#include <OvTools/Eventing/Event.h>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <stdexcept>

namespace OvTools::Eventing
{
    // Owns CPU-only payloads from arbitrary producers. Subscription, dispatch and
    // callback destruction belong to the constructing (application main) thread.
    // Do not put objects requiring GL-owner destruction in this queue.
    template<class T> class MainThreadEvent
    {
        static_assert(std::is_nothrow_move_constructible_v<T>);
        struct Entry { T value; uint64_t sequence; ListenerID lastListener; };
        const std::thread::id m_owner = std::this_thread::get_id();
        const size_t m_capacity;
        mutable std::mutex m_mutex;
        std::map<ListenerID, std::function<void(const T&)>> m_callbacks;
        std::deque<Entry> m_pending;
        ListenerID m_nextListener = 1;
        uint64_t m_sequence = 0, m_dropped = 0;
        bool m_dispatching = false; // Owner only.
        void RequireOwner() const
        { if (!IsOwnerThread()) throw std::logic_error("Event operation requires its main thread"); }
    public:
        using Callback = std::function<void(const T&)>;
        explicit MainThreadEvent(size_t capacity = 4096) : m_capacity(capacity)
        { if (!capacity) throw std::invalid_argument("Event capacity must be positive"); }
        bool IsOwnerThread() const noexcept { return std::this_thread::get_id() == m_owner; }
        ListenerID AddListener(Callback callback)
        {
            RequireOwner();
            std::lock_guard lock(m_mutex);
            const auto id = m_nextListener++;
            m_callbacks.emplace(id, std::move(callback));
            return id;
        }
        ListenerID operator+=(Callback callback) { return AddListener(std::move(callback)); }
        bool RemoveListener(ListenerID id)
        {
            RequireOwner();
            // Destroy callbacks outside the mutex; a captured object's destructor
            // may itself log or unsubscribe another listener.
            typename decltype(m_callbacks)::node_type removed;
            { std::lock_guard lock(m_mutex); removed = m_callbacks.extract(id); }
            return !removed.empty();
        }
        bool operator-=(ListenerID id) { return RemoveListener(id); }
        void RemoveAllListeners()
        {
            RequireOwner();
            decltype(m_callbacks) removed;
            { std::lock_guard lock(m_mutex); removed.swap(m_callbacks); }
        }
        uint64_t GetListenerCount() const
        { std::lock_guard lock(m_mutex); return m_callbacks.size(); }
        // Post never invokes user code and can be called while a sink is locked.
        void Post(const T& value)
        {
            std::lock_guard lock(m_mutex);
            if (m_callbacks.empty()) return;
            m_pending.push_back({value, ++m_sequence, m_nextListener - 1});
            if (m_pending.size() > m_capacity) { m_pending.pop_front(); ++m_dropped; }
        }
        void Invoke(const T& value) { Post(value); if (IsOwnerThread()) DispatchPending(); }
        uint64_t TakeDroppedCount()
        { RequireOwner(); std::lock_guard lock(m_mutex); return std::exchange(m_dropped, 0); }
        size_t GetPendingCount() const
        { std::lock_guard lock(m_mutex); return m_pending.size(); }
        // Finite batch: nested/concurrent posts are handled by the next pump.
        // Subscribers added after a post never receive that historical message;
        // removed subscribers are skipped, including removal during a callback.
        void DispatchPending()
        {
            RequireOwner();
            if (m_dispatching) return;
            m_dispatching = true;
            struct Reset { bool& value; ~Reset() { value = false; } } reset{m_dispatching};
            uint64_t cutoff;
            { std::lock_guard lock(m_mutex); cutoff = m_sequence; }
            std::exception_ptr error;
            for (;;)
            {
                std::optional<Entry> entry;
                {
                    std::lock_guard lock(m_mutex);
                    if (m_pending.empty() || m_pending.front().sequence > cutoff) break;
                    entry.emplace(std::move(m_pending.front())); m_pending.pop_front();
                }
                ListenerID previous = 0;
                for (;;)
                {
                    Callback callback;
                    {
                        std::lock_guard lock(m_mutex);
                        const auto found = m_callbacks.upper_bound(previous);
                        if (found == m_callbacks.end() || found->first > entry->lastListener) break;
                        previous = found->first; callback = found->second;
                    }
                    try { callback(entry->value); }
                    catch (...) { if (!error) error = std::current_exception(); }
                }
            }
            if (error) std::rethrow_exception(error);
        }
    };
}
