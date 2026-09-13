#include <OvReflection/ScriptBridge.h>
#include <atomic>
#include <mutex>
#include <cstring>
#include <climits>
namespace OvReflection::Script
{
    namespace {
        std::mutex contextsMutex;
        std::unordered_map<uint64_t, std::shared_ptr<State>> contexts;
        std::atomic<uint64_t> identity{1};
        bool Utf8(const uint8_t* bytes, size_t size)
        {
            for (size_t i = 0; i < size;) {
                uint32_t value = bytes[i++];
                if (value < 0x80) continue;
                unsigned count; uint32_t minimum;
                if (value >= 0xc2 && value <= 0xdf) { count = 1; minimum = 0x80; value &= 0x1f; }
                else if (value >= 0xe0 && value <= 0xef) { count = 2; minimum = 0x800; value &= 0xf; }
                else if (value >= 0xf0 && value <= 0xf4) { count = 3; minimum = 0x10000; value &= 7; }
                else return false;
                if (count > size - i) return false;
                while (count--) { auto next = bytes[i++]; if ((next & 0xc0) != 0x80) return false; value = (value << 6) | (next & 0x3f); }
                if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
            }
            return true;
        }
    }
    uint64_t NextIdentity()
    {
        auto next = identity.load(std::memory_order_relaxed);
        for (;;) {
            Require(next != UINT64_MAX, OV_SCRIPT_NATIVE_EXCEPTION);
            if (identity.compare_exchange_weak(next, next + 1, std::memory_order_relaxed)) return next;
        }
    }
    Context::Context(const MainThread& main) : m_id(NextIdentity()), m_state(std::make_shared<State>(main))
    {
        Require(main.IsCurrent(), OV_SCRIPT_WRONG_THREAD);
        std::lock_guard lock(contextsMutex); contexts.emplace(m_id, m_state);
    }
    Context::~Context() { std::lock_guard lock(contextsMutex); contexts.erase(m_id); }
    std::shared_ptr<State> Acquire(uint64_t context)
    {
        std::lock_guard lock(contextsMutex);
        auto it = contexts.find(context);
        Require(it != contexts.end(), OV_SCRIPT_INVALID_CONTEXT);
        return it->second;
    }
    std::string Decode(OvScriptString value)
    {
        Require(value.size <= INT_MAX && (value.data || !value.size));
        Require(Utf8(value.data, static_cast<size_t>(value.size)));
        return value.size ? std::string(reinterpret_cast<const char*>(value.data), static_cast<size_t>(value.size)) : std::string{};
    }
    OvScriptOwnedString Encode(const std::string& value)
    {
        Require(value.size() <= INT_MAX && Utf8(reinterpret_cast<const uint8_t*>(value.data()), value.size()));
        if (value.empty()) return {};
        auto data = std::make_unique<uint8_t[]>(value.size());
        std::memcpy(data.get(), value.data(), value.size());
        return {data.release(), static_cast<uint64_t>(value.size())};
    }
    void OV_SCRIPT_CALL ReleaseString(OvScriptOwnedString* value) noexcept
    { if (value) { delete[] value->data; *value = {}; } }
}
