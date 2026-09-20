#pragma once
#include <OvCore/UI/Layout.h>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

namespace OvCore::UI
{
    // Component-owned authored state. Tokens are opaque, never reused by the bridge.
    // No Actor, managed object, or process-global component registry is retained.
    class DrivenProperties final
    {
        struct Claim { uint32_t mask; uint64_t session; RectLayout saved; };
        std::unordered_map<uint64_t, Claim> m_claims;
        static void Copy(RectLayout& to, const RectLayout& from, uint32_t mask)
        {
            auto pair = [mask](OvMaths::FVector2& dst, const OvMaths::FVector2& src, int shift) {
                if (mask & (1u << shift)) dst.x = src.x;
                if (mask & (2u << shift)) dst.y = src.y;
            };
            pair(to.anchoredPosition, from.anchoredPosition, 0);
            pair(to.anchorMin, from.anchorMin, 2); pair(to.anchorMax, from.anchorMax, 4);
            pair(to.sizeDelta, from.sizeDelta, 6); pair(to.pivot, from.pivot, 8);
        }
    public:
        static constexpr uint32_t All = 1023;
        bool Owns(uint64_t token, uint64_t session) const { auto it = m_claims.find(token); return it != m_claims.end() && it->second.session == session; }
        uint32_t Mask() const { uint32_t mask = 0; for (const auto& [token, claim] : m_claims) mask |= claim.mask; return mask; }
        // token must be supplied by NextIdentity for a new claim, or be the existing lease.
        uint64_t Acquire(const RectLayout& layout, uint32_t mask, uint64_t session, uint64_t token)
        {
            if (!mask || (mask & ~All) || !token || !session) throw std::invalid_argument("Invalid driven claim");
            for (const auto& [id, claim] : m_claims)
                if ((id != token && (claim.mask & mask)) || (id == token && claim.session != session))
                    throw std::invalid_argument("Conflicting driven claim");
            auto [it, inserted] = m_claims.try_emplace(token, Claim{0, session, layout});
            Copy(it->second.saved, layout, mask & ~it->second.mask);
            it->second.mask |= mask;
            return token;
        }
        void Release(RectLayout& layout, uint64_t token, uint32_t mask, uint64_t session)
        {
            if (mask & ~All) throw std::invalid_argument("Invalid driven mask");
            auto it = m_claims.find(token);
            if (it == m_claims.end() || it->second.session != session) throw std::invalid_argument("Invalid driven token");
            Copy(layout, it->second.saved, mask & it->second.mask);
            it->second.mask &= ~mask;
            if (!it->second.mask) m_claims.erase(it);
        }
        RectLayout Authored(RectLayout layout) const
        {
            for (const auto& [token, claim] : m_claims) Copy(layout, claim.saved, claim.mask);
            return layout;
        }
    };
}
