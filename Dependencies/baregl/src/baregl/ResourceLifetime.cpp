#include <baregl/ResourceLifetime.h>
#include <baregl/detail/glad/glad.h>
#include <atomic>
#include <list>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace baregl::detail
{
    struct DeletionRecord
    {
        ResourceKind kind;
        uint32_t id = 0;
        std::list<std::shared_ptr<DeletionRecord>>::iterator position;
        explicit DeletionRecord(ResourceKind value) : kind(value) {}
    };
    struct DeletionState
    {
        const std::thread::id owner = std::this_thread::get_id();
        std::atomic_bool alive{true};
        mutable std::mutex mutex;
        std::list<std::shared_ptr<DeletionRecord>> records;
        std::list<std::shared_ptr<DeletionRecord>> retired;
        size_t processing = 0;
        std::function<void()> wake;
    };
}
namespace
{
    thread_local std::shared_ptr<baregl::detail::DeletionState> current;
    void Delete(baregl::ResourceKind kind, uint32_t id) noexcept
    {
        if (!id) return;
        using enum baregl::ResourceKind;
        switch (kind)
        {
        case TEXTURE: glDeleteTextures(1, &id); break;
        case BUFFER: glDeleteBuffers(1, &id); break;
        case FRAMEBUFFER: glDeleteFramebuffers(1, &id); break;
        case RENDERBUFFER: glDeleteRenderbuffers(1, &id); break;
        case PROGRAM: glDeleteProgram(id); break;
        case SHADER: glDeleteShader(id); break;
        case VERTEX_ARRAY: glDeleteVertexArrays(1, &id); break;
        case NONE: break;
        }
    }
    void RequireOwner(const baregl::detail::DeletionState& state)
    { if (std::this_thread::get_id() != state.owner) throw std::logic_error("Resource operation requires its GL owner"); }
}
namespace baregl
{
    DeletionQueue::DeletionQueue(std::function<void()> wake)
    {
        if (current) throw std::logic_error("A deletion queue is already bound to this thread");
        m_state = std::make_shared<detail::DeletionState>();
        m_state->wake = std::move(wake);
        current = m_state;
    }
    DeletionQueue::~DeletionQueue() { if (m_state->alive.load(std::memory_order_acquire)) Close(); }
    size_t DeletionQueue::GetPendingCount() const
    { std::lock_guard lock(m_state->mutex); return m_state->retired.size() + m_state->processing; }
    void DeletionQueue::Drain()
    {
        RequireOwner(*m_state);
        std::list<std::shared_ptr<detail::DeletionRecord>> retired;
        {
            std::lock_guard lock(m_state->mutex);
            retired.swap(m_state->retired);
            m_state->processing += retired.size();
        }
        for (const auto& record : retired) Delete(record->kind, record->id);
        { std::lock_guard lock(m_state->mutex); m_state->processing -= retired.size(); }
    }
    size_t DeletionQueue::Close()
    {
        if (!m_state->alive.load(std::memory_order_acquire)) return 0;
        RequireOwner(*m_state);
        std::list<std::shared_ptr<detail::DeletionRecord>> remaining;
        size_t invalidated = 0;
        {
            std::lock_guard lock(m_state->mutex);
            if (!m_state->alive.exchange(false, std::memory_order_acq_rel)) return 0;
            for (const auto& record : m_state->records) if (record->id) ++invalidated;
            remaining.swap(m_state->records);
            remaining.splice(remaining.end(), m_state->retired);
        }
        // Local shared owners protect records against concurrent late CPU release.
        // Delete outside the lock, including synchronous GL diagnostic callbacks.
        for (const auto& record : remaining) Delete(record->kind, record->id);
        if (current == m_state) current.reset();
        return invalidated;
    }
    ResourceLifetime::ResourceLifetime(ResourceKind kind) : m_kind(kind), m_state(current)
    {
        if (m_state && kind != ResourceKind::NONE)
        {
            m_record = std::make_shared<detail::DeletionRecord>(kind);
            std::lock_guard lock(m_state->mutex);
            if (!m_state->alive.load(std::memory_order_relaxed)) throw std::logic_error("GL resource domain is closed");
            m_state->records.push_back(m_record);
            m_record->position = std::prev(m_state->records.end());
        }
    }
    void ResourceLifetime::Adopt(uint32_t id)
    {
        if (m_state) RequireOwner(*m_state);
        if (m_id || !IsContextAlive()) throw std::logic_error("Cannot replace or revive a GL resource");
        m_id = id;
        if (m_record) m_record->id = id;
    }
    bool ResourceLifetime::IsContextAlive() const noexcept
    { return !m_state || m_state->alive.load(std::memory_order_acquire); }
    ResourceLifetime::~ResourceLifetime()
    {
        if (!m_state) { Delete(m_kind, m_id); return; }
        if (!m_record) return;
        const bool owner = std::this_thread::get_id() == m_state->owner;
        {
            std::lock_guard lock(m_state->mutex);
            if (!m_state->alive.load(std::memory_order_relaxed)) return;
            if (owner) m_state->records.erase(m_record->position);
            else m_state->retired.splice(m_state->retired.end(), m_state->records, m_record->position);
        }
        if (owner) Delete(m_kind, m_id);
        else if (m_state->wake) m_state->wake();
    }
}
