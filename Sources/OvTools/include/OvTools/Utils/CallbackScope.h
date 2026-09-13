#pragma once
#include <memory>

namespace OvTools::Utils
{
    // Main-thread callback epoch. Tickets never keep the target or scope alive.
    class CallbackScope final
    {
    public:
        CallbackScope() = default;
        CallbackScope(const CallbackScope&) = delete;
        CallbackScope& operator=(const CallbackScope&) = delete;
        std::weak_ptr<const bool> Ticket() const { return m_epoch; }
        void Invalidate() { m_epoch = std::make_shared<const bool>(true); }
    private:
        std::shared_ptr<const bool> m_epoch = std::make_shared<const bool>(true);
    };
}
