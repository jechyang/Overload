#pragma once
#include <OvReflection/Reflection.h>
#include <OvReflection/ScriptABI.h>
#include <memory>
#include <optional>
#include <unordered_map>
#include <stdexcept>
#include <limits>

namespace OvReflection::Script
{
    struct Failure { OvScriptStatus status; };
    inline void Require(bool condition, OvScriptStatus status = OV_SCRIPT_INVALID_ARGUMENT)
    { if (!condition) throw Failure{status}; }
    inline void Check(Error error)
    {
        if (error == Error::None) return;
        throw Failure{error == Error::WrongThread ? OV_SCRIPT_WRONG_THREAD :
            error == Error::ReadOnly ? OV_SCRIPT_READ_ONLY : OV_SCRIPT_WRONG_TYPE};
    }
    struct State final
    {
        explicit State(const MainThread& authority) : main(authority) {}
        struct Entry { ObjectRef object; std::weak_ptr<const void> lifetime; };
        const MainThread main;
        std::unordered_map<uint64_t, Entry> objects;
        ObjectRef Resolve(uint64_t handle)
        {
            Require(main.IsCurrent(), OV_SCRIPT_WRONG_THREAD);
            auto it = objects.find(handle);
            Require(it != objects.end(), OV_SCRIPT_INVALID_HANDLE);
            if (it->second.lifetime.expired()) { objects.erase(it); throw Failure{OV_SCRIPT_INVALID_HANDLE}; }
            return it->second.object;
        }
        template<class T> T* Get(uint64_t handle, bool nullable = false)
        {
            if (!handle && nullable) return nullptr;
            auto ref = Resolve(handle);
            Require(ref.As<std::remove_const_t<T>>() != nullptr, OV_SCRIPT_WRONG_TYPE);
            if constexpr (std::is_const_v<T>) return ref.As<std::remove_const_t<T>>();
            else { Require(!ref.IsConst(), OV_SCRIPT_READ_ONLY); return ref.MutableAs<T>(); }
        }
        template<class T> uint64_t Find(T* object, bool nullable = false)
        {
            if (!object) { Require(nullable); return 0; }
            for (auto& [id, entry] : objects)
                if (!entry.lifetime.expired() && entry.object.As<std::remove_const_t<T>>() == object &&
                    (std::is_const_v<T> || !entry.object.IsConst())) return id;
            throw Failure{OV_SCRIPT_INVALID_HANDLE};
        }
    };
    std::shared_ptr<State> Acquire(uint64_t context);
    uint64_t NextIdentity();
    // Native host owns this session. Create on application Main, with its authority.
    // No managed delegate, assembly or GCHandle is retained. Destroy before unloading native code.
    class Context final
    {
    public:
        explicit Context(const MainThread& main);
        ~Context();
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        uint64_t Id() const { return m_id; }
        template<class T> uint64_t Publish(T& object, std::weak_ptr<const void> lifetime)
        {
            Require(m_state->main.IsCurrent(), OV_SCRIPT_WRONG_THREAD);
            Require(!lifetime.expired(), OV_SCRIPT_INVALID_HANDLE);
            // Reuse only the exact live object AND ownership epoch. Never recycle IDs.
            for (auto it = m_state->objects.begin(); it != m_state->objects.end();)
            {
                if (it->second.lifetime.expired()) it = m_state->objects.erase(it);
                else {
                    const auto& entryLifetime = it->second.lifetime;
                    if (!entryLifetime.owner_before(lifetime) && !lifetime.owner_before(entryLifetime) &&
                        it->second.object.As<std::remove_const_t<T>>() == &object &&
                        it->second.object.IsConst() == std::is_const_v<T>) return it->first;
                    ++it;
                }
            }
            const auto id = NextIdentity();
            m_state->objects.emplace(id, State::Entry{ObjectRef::From(object), lifetime});
            return id;
        }
        void Revoke(uint64_t handle)
        { Require(m_state->main.IsCurrent(), OV_SCRIPT_WRONG_THREAD); m_state->objects.erase(handle); }
    private:
        uint64_t m_id;
        std::shared_ptr<State> m_state;
    };
    template<class F> OvScriptStatus Guard(uint64_t context, F&& action) noexcept
    {
        try {
            auto state = Acquire(context);
            Require(state->main.IsCurrent(), OV_SCRIPT_WRONG_THREAD);
            action(*state);
            return OV_SCRIPT_OK;
        } catch (const Failure& e) { return e.status; }
          catch (...) { return OV_SCRIPT_NATIVE_EXCEPTION; }
    }
    std::string Decode(OvScriptString value);
    OvScriptOwnedString Encode(const std::string& value);
    void OV_SCRIPT_CALL ReleaseString(OvScriptOwnedString* value) noexcept;
}
