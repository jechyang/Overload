#include <OvReflection/Reflection.h>
#include <algorithm>
#include <climits>
#include <set>

namespace OvReflection
{
    namespace
    {
        bool Matches(std::string_view query, std::string_view id, std::span<const std::string_view> aliases)
        { return query == id || std::find(aliases.begin(), aliases.end(), query) != aliases.end(); }
    }
    const void* TypeDesc::Cast(const void* object, TypeKey key) const
    {
        if (key == m_info.key) return object;
        for (const auto& base : m_info.bases)
        {
            const void* converted = base.m_upcast(object);
            if (key == base.m_key) return converted;
            if (const auto* reflected = base.Reflected())
                if (const auto* result = reflected->Cast(converted, key)) return result;
        }
        return nullptr;
    }
    const PropertyDesc* TypeDesc::FindProperty(std::string_view identity) const
    {
        for (const auto& property : m_info.properties)
            if (Matches(identity, property.Info().id, property.Info().aliases) || identity == property.Info().name) return &property;
        for (const auto& base : m_info.bases)
            if (const auto* reflected = base.Reflected())
                if (const auto* property = reflected->FindProperty(identity)) return property;
        return nullptr;
    }
    Error Registry::RegisterModule(std::string_view module, std::span<const TypeDesc* const> types)
    {
        if (!m_main.IsCurrent()) return Error::WrongThread;
        if (m_frozen) return Error::Frozen;
        if (module.empty()) return Error::InvalidDescriptor;
        if (std::find(m_modules.begin(), m_modules.end(), module) != m_modules.end()) return Error::Duplicate;
        // Validate the whole batch before mutation, including global property/method identities.
        std::set<std::string_view> identities;
        std::set<TypeKey> keys;
        auto claim = [&](std::string_view id, std::span<const std::string_view> aliases)
        {
            if (id.empty() || !identities.insert(id).second) return false;
            for (auto alias : aliases) if (alias.empty() || !identities.insert(alias).second) return false;
            return true;
        };
        auto validate = [&](const TypeDesc* type)
        {
            if (!type || !type->Info().key || !keys.insert(type->Info().key).second) return false;
            const auto& info = type->Info();
            if (!claim(info.id, info.aliases)) return false;
            for (const auto& property : info.properties)
                if (!claim(property.Info().id, property.Info().aliases)) return false;
            for (const auto& method : info.methods)
                if (!claim(method.id, method.aliases)) return false;
            return true;
        };
        for (auto* type : m_types) if (!validate(type)) return Error::InvalidDescriptor;
        for (auto* type : types)
        {
            if (!type || type->Info().module != module || type->Info().schemaVersion != 1) return Error::InvalidDescriptor;
            if (!validate(type)) return Error::Duplicate;
        }
        // Reserve before committing so allocation failure cannot partially append types.
        m_types.reserve(m_types.size() + types.size());
        m_modules.emplace_back(module);
        m_types.insert(m_types.end(), types.begin(), types.end());
        return Error::None;
    }
    Error Registry::Freeze()
    {
        if (!m_main.IsCurrent()) return Error::WrongThread;
        if (m_frozen) return Error::Frozen;
        m_frozen = true;
        return Error::None;
    }
    const TypeDesc* Registry::Find(std::string_view identity) const
    {
        for (auto* type : m_types)
            if (Matches(identity, type->Info().id, type->Info().aliases)) return type;
        return nullptr;
    }

#define OV_REFLECTION_BUILTIN(T, ID, CATEGORY, BITS) \
    const TypeDesc& Traits<T>::Get() \
    { \
        static const TypeDesc descriptor(TypeInfo{ID, #T, "OvReflection", Kind::Value, 1, Key<T>(), \
            {}, {}, Source{}, {}, {}, {}, ValueType{#T, #T, #T, CATEGORY, BITS, std::is_integral_v<T> && std::is_signed_v<T>, Key<T>()}, {}}); \
        return descriptor; \
    }
    OV_REFLECTION_BUILTIN(bool, "cpp.bool", "bool", sizeof(bool) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(signed char, "cpp.signed_char", "integer", sizeof(signed char) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(unsigned char, "cpp.unsigned_char", "integer", sizeof(unsigned char) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(short, "cpp.short", "integer", sizeof(short) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(unsigned short, "cpp.unsigned_short", "integer", sizeof(unsigned short) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(int, "cpp.int", "integer", sizeof(int) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(unsigned int, "cpp.unsigned_int", "integer", sizeof(unsigned int) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(long, "cpp.long", "integer", sizeof(long) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(unsigned long, "cpp.unsigned_long", "integer", sizeof(unsigned long) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(long long, "cpp.long_long", "integer", sizeof(long long) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(unsigned long long, "cpp.unsigned_long_long", "integer", sizeof(unsigned long long) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(float, "cpp.float", "float", sizeof(float) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(double, "cpp.double", "float", sizeof(double) * CHAR_BIT)
    OV_REFLECTION_BUILTIN(std::string, "cpp.std_string", "string", 0)
#undef OV_REFLECTION_BUILTIN
    Error RegisterBuiltinTypes(Registry& registry)
    {
        const std::array types{&TypeOf<bool>(), &TypeOf<signed char>(), &TypeOf<unsigned char>(),
            &TypeOf<short>(), &TypeOf<unsigned short>(), &TypeOf<int>(), &TypeOf<unsigned int>(),
            &TypeOf<long>(), &TypeOf<unsigned long>(), &TypeOf<long long>(), &TypeOf<unsigned long long>(),
            &TypeOf<float>(), &TypeOf<double>(), &TypeOf<std::string>()};
        return registry.RegisterModule("OvReflection", types);
    }
}
