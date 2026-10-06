#pragma once
#include <cstdint>
#include <functional>
#include <memory>

namespace baregl
{
    enum class ResourceKind { NONE, TEXTURE, BUFFER, FRAMEBUFFER, RENDERBUFFER, PROGRAM, SHADER, VERTEX_ARRAY, QUERY };
    namespace detail { struct DeletionState; struct DeletionRecord; }

    // Bind once on the current GL owner before constructing GPU objects. CPU
    // holders may outlive Close, but their native IDs become invalid at Close.
    class DeletionQueue final
    {
        std::shared_ptr<detail::DeletionState> m_state;
    public:
        explicit DeletionQueue(std::function<void()> wake = {});
        ~DeletionQueue();
        DeletionQueue(const DeletionQueue&) = delete;
        DeletionQueue& operator=(const DeletionQueue&) = delete;
        void Drain(); // GL owner only; no waiting for GPU. Frames retain captures until fenced.
        size_t Close(); // GL owner, after GPU drain. Deletes and invalidates remaining handles.
        size_t GetPendingCount() const;
    };

    // Allocate the retirement record before GL creation. Destruction needs no
    // allocation and never waits for the GL owner. Without a queue, legacy GL
    // owner destruction is preserved. Adopt exactly once after successful create.
    class ResourceLifetime final
    {
        ResourceKind m_kind;
        uint32_t m_id = 0;
        std::shared_ptr<detail::DeletionState> m_state;
        std::shared_ptr<detail::DeletionRecord> m_record;
    public:
        explicit ResourceLifetime(ResourceKind kind = ResourceKind::NONE);
        ~ResourceLifetime();
        ResourceLifetime(const ResourceLifetime&) = delete;
        ResourceLifetime& operator=(const ResourceLifetime&) = delete;
        void Adopt(uint32_t id);
        bool IsContextAlive() const noexcept;
        bool HasOwnerQueue() const noexcept { return static_cast<bool>(m_state); }
        // GL owner only. A retained identity prevents reuse across deletion domains.
        static std::shared_ptr<const void> CurrentDomain();
    };
}
