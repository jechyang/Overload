#pragma once
#include <OvReflection/Annotations.h>
#include <array>
#include <any>
#include <charconv>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace OvReflection
{
    enum class Error { None, WrongObjectType, WrongValueType, ReadOnly, WrongThread, Duplicate, Frozen, InvalidDescriptor };
    enum class Thread { Any, Main };
    enum class Kind { Class, Struct, Enum, EnumClass, Value };
    // Capture on the application's Main thread during initialization, before workers start.
    // Share this authority by reference; constructing one on a worker does not identify Main.
    class MainThread final
    {
    public:
        MainThread() : m_id(std::this_thread::get_id()) {}
        bool IsCurrent() const { return m_id == std::this_thread::get_id(); }
        Error Check(Thread thread) const { return thread == Thread::Main && !IsCurrent() ? Error::WrongThread : Error::None; }
    private:
        const std::thread::id m_id;
    };
    using TypeKey = const void*;
    namespace Detail { template<class T> inline unsigned char TypeToken = 0; }
    template<class T> TypeKey Key()
    {
        static_assert(!std::is_volatile_v<std::remove_reference_t<T>>);
        return &Detail::TypeToken<std::remove_cvref_t<T>>;
    }
    struct Metadata { std::string_view key, value; };
    struct Source { std::string_view path; unsigned line, column; };
    struct ValueType
    {
        std::string_view display, canonical, valueType, category;
        unsigned bits;
        bool isSigned;
        TypeKey key;
    };
    struct ParameterDesc { std::string_view name; ValueType type; };
    struct MethodDesc
    {
        std::string_view id, name, cppName, signature, access, refQualifier;
        bool isConst, isNoexcept;
        ValueType result;
        std::span<const ParameterDesc> parameters;
        std::span<const std::string_view> aliases;
        std::span<const Metadata> metadata;
        Source source;
    };
    struct EnumValue { std::string_view name, decimalValue; Source source; };
    class TypeDesc;
    template<class T> struct Traits;
    template<class T> const TypeDesc& TypeOf() { return Traits<std::remove_cvref_t<T>>::Get(); }

    // Borrowed, call-scoped references. No raw pointer/type pair construction.
    // Caller owns lifetime and must not retain these across frames or scene destruction.
    class ObjectRef final
    {
    public:
        template<class T> static ObjectRef From(T& object)
        {
            static_assert(!std::is_volatile_v<T>);
            return ObjectRef(&object, std::is_const_v<T>, TypeOf<T>());
        }
        template<class T> const T* As() const;
        template<class T> T* MutableAs() const { return m_const ? nullptr : const_cast<T*>(As<T>()); }
        bool IsConst() const { return m_const; }
    private:
        ObjectRef(const void* object, bool immutable, const TypeDesc& type) : m_object(object), m_const(immutable), m_type(&type) {}
        const void* m_object;
        bool m_const;
        const TypeDesc* m_type;
    };
    class ValueRef final
    {
    public:
        template<class T> static ValueRef From(T& value)
        {
            static_assert(!std::is_volatile_v<T>);
            return ValueRef(&value, Key<T>(), std::is_const_v<T>);
        }
        template<class T> const T* As() const { return m_key == Key<T>() ? static_cast<const T*>(m_value) : nullptr; }
        template<class T> T* MutableAs() const { return m_const ? nullptr : const_cast<T*>(As<T>()); }
    private:
        ValueRef(const void* value, TypeKey key, bool immutable) : m_value(value), m_key(key), m_const(immutable) {}
        const void* m_value;
        TypeKey m_key;
        bool m_const;
    };
    struct PropertyInfo
    {
        std::string_view id, name, cppName, strategy, access;
        ValueType type;
        bool readOnly;
        Thread thread;
        std::span<const std::string_view> aliases;
        std::span<const Metadata> metadata;
        Source source;
    };
    class PropertyDesc final
    {
    public:
        const PropertyInfo& Info() const { return m_info; }
        Error Read(const MainThread& main, ObjectRef object, ValueRef output) const { return m_read(main, object, output); }
        Error Write(const MainThread& main, ObjectRef object, ValueRef input) const
        { return m_info.readOnly ? Error::ReadOnly : m_write(main, object, input); }
        // Owned copies for adapters. Enums use exact decimal strings; other values
        // retain their C++ type. No borrowed ObjectRef is retained by these operations.
        Error ValidateValue(const std::any& input) const { return m_validateValue(input); }
        Error ReadValue(const MainThread& main, ObjectRef object, std::any& output) const
        { return m_readValue(main, object, output); }
        Error WriteValue(const MainThread& main, ObjectRef object, const std::any& input) const
        { return m_info.readOnly ? Error::ReadOnly : m_writeValue(main, object, input); }
        template<class Owner, class Value, auto Getter, auto Setter>
        static PropertyDesc Make(PropertyInfo info)
        {
            return PropertyDesc(info,
                [](const MainThread& main, ObjectRef object, ValueRef output)
                {
                    auto* owner = object.As<Owner>();
                    if (!owner) return Error::WrongObjectType;
                    auto* value = output.MutableAs<Value>();
                    if (!value) return Error::WrongValueType;
                    return Getter(main, *owner, *value);
                },
                []([[maybe_unused]] const MainThread& main, [[maybe_unused]] ObjectRef object, [[maybe_unused]] ValueRef input)
                {
                    if constexpr (std::is_same_v<decltype(Setter), std::nullptr_t>) return Error::ReadOnly;
                    else
                    {
                        if (!object.As<Owner>()) return Error::WrongObjectType;
                        if (object.IsConst()) return Error::ReadOnly;
                        auto* value = input.As<Value>();
                        if (!value) return Error::WrongValueType;
                        return Setter(main, *object.MutableAs<Owner>(), *value);
                    }
                },
                [](const MainThread& main, ObjectRef object, std::any& output)
                {
                    auto* owner = object.As<Owner>();
                    if (!owner) return Error::WrongObjectType;
                    Value value{};
                    if (auto error = Getter(main, *owner, value); error != Error::None) return error;
                    if constexpr (std::is_enum_v<Value>) output = std::to_string(static_cast<std::underlying_type_t<Value>>(value));
                    else output = std::move(value);
                    return Error::None;
                },
                []([[maybe_unused]] const MainThread& main, [[maybe_unused]] ObjectRef object, [[maybe_unused]] const std::any& input)
                {
                    if constexpr (std::is_same_v<decltype(Setter), std::nullptr_t>) return Error::ReadOnly;
                    else
                    {
                        auto* owner = object.MutableAs<Owner>();
                        if (!object.As<Owner>()) return Error::WrongObjectType;
                        if (!owner) return Error::ReadOnly;
                        if constexpr (std::is_enum_v<Value>)
                        {
                            auto* text = std::any_cast<std::string>(&input);
                            if (!text) return Error::WrongValueType;
                            std::underlying_type_t<Value> value{};
                            auto result = std::from_chars(text->data(), text->data() + text->size(), value);
                            if (result.ec != std::errc{} || result.ptr != text->data() + text->size()) return Error::WrongValueType;
                            return Setter(main, *owner, static_cast<Value>(value));
                        }
                        else
                        {
                            auto* value = std::any_cast<Value>(&input);
                            return value ? Setter(main, *owner, *value) : Error::WrongValueType;
                        }
                    }
                },
                [](const std::any& input)
                {
                    if constexpr (std::is_enum_v<Value>)
                    {
                        auto* text = std::any_cast<std::string>(&input);
                        if (!text) return Error::WrongValueType;
                        std::underlying_type_t<Value> value{};
                        auto parsed = std::from_chars(text->data(), text->data() + text->size(), value);
                        return parsed.ec == std::errc{} && parsed.ptr == text->data() + text->size() ? Error::None : Error::WrongValueType;
                    }
                    else return std::any_cast<Value>(&input) ? Error::None : Error::WrongValueType;
                });
        }
    private:
        using Operation = Error (*)(const MainThread&, ObjectRef, ValueRef);
        using ReadValueOperation = Error (*)(const MainThread&, ObjectRef, std::any&);
        using WriteValueOperation = Error (*)(const MainThread&, ObjectRef, const std::any&);
        PropertyDesc(PropertyInfo info, Operation read, Operation write, ReadValueOperation readValue, WriteValueOperation writeValue, Error (*validateValue)(const std::any&))
            : m_info(info), m_read(read), m_write(write), m_readValue(readValue), m_writeValue(writeValue), m_validateValue(validateValue) {}
        PropertyInfo m_info;
        Operation m_read, m_write;
        ReadValueOperation m_readValue;
        WriteValueOperation m_writeValue;
        Error (*m_validateValue)(const std::any&);
    };
    class BaseDesc final
    {
    public:
        std::string_view CppName() const { return m_cppName; }
        const TypeDesc* Reflected() const { return m_reflected ? m_reflected() : nullptr; }
        template<class Derived, class Base>
        static BaseDesc Make(std::string_view name, const TypeDesc* (*reflected)())
        {
            return BaseDesc(name, Key<Base>(), reflected, [](const void* object) -> const void*
            { return static_cast<const Base*>(static_cast<const Derived*>(object)); });
        }
    private:
        friend class TypeDesc;
        using Query = const TypeDesc* (*)();
        using Upcast = const void* (*)(const void*);
        BaseDesc(std::string_view name, TypeKey key, Query reflected, Upcast upcast)
            : m_cppName(name), m_key(key), m_reflected(reflected), m_upcast(upcast) {}
        std::string_view m_cppName;
        TypeKey m_key;
        Query m_reflected;
        Upcast m_upcast;
    };
    struct TypeInfo
    {
        std::string_view id, cppName, module;
        Kind kind;
        unsigned schemaVersion;
        TypeKey key;
        std::span<const std::string_view> aliases;
        std::span<const Metadata> metadata;
        Source source;
        std::span<const BaseDesc> bases;
        std::span<const PropertyDesc> properties;
        std::span<const MethodDesc> methods;
        ValueType underlying;
        std::span<const EnumValue> enumerators;
    };
    class TypeDesc final
    {
    public:
        explicit TypeDesc(TypeInfo info) : m_info(info) {}
        const TypeInfo& Info() const { return m_info; }
        const PropertyDesc* FindProperty(std::string_view identity) const;
    private:
        friend class ObjectRef;
        const void* Cast(const void* object, TypeKey key) const;
        const TypeInfo m_info;
    };
    template<class T> const T* ObjectRef::As() const
    { return static_cast<const T*>(m_type->Cast(m_object, Key<T>())); }

    // Initialize on Main, explicitly register modules, Freeze, then publish to readers.
    // Descriptor storage must be immutable and outlive the registry (generated static storage).
    class Registry final
    {
    public:
        Registry() = default;
        Registry(const Registry&) = delete;
        Registry& operator=(const Registry&) = delete;
        Error RegisterModule(std::string_view module, std::span<const TypeDesc* const> types);
        Error Freeze();
        const TypeDesc* Find(std::string_view identity) const;
        std::span<const TypeDesc* const> Types() const { return m_types; }
        const MainThread& Main() const { return m_main; }
        bool IsFrozen() const { return m_frozen; }
    private:
        MainThread m_main;
        bool m_frozen = false;
        std::vector<const TypeDesc*> m_types;
        std::vector<std::string> m_modules;
    };

    // Fixed C++ primitive identities; typedef aliases share the actual C++ type.
    // Their widths remain target facts in Info().underlying, not implied by the ID.
#define OV_REFLECTION_BUILTIN_DECL(T) template<> struct Traits<T> { static const TypeDesc& Get(); };
    OV_REFLECTION_BUILTIN_DECL(bool)
    OV_REFLECTION_BUILTIN_DECL(signed char)
    OV_REFLECTION_BUILTIN_DECL(unsigned char)
    OV_REFLECTION_BUILTIN_DECL(short)
    OV_REFLECTION_BUILTIN_DECL(unsigned short)
    OV_REFLECTION_BUILTIN_DECL(int)
    OV_REFLECTION_BUILTIN_DECL(unsigned int)
    OV_REFLECTION_BUILTIN_DECL(long)
    OV_REFLECTION_BUILTIN_DECL(unsigned long)
    OV_REFLECTION_BUILTIN_DECL(long long)
    OV_REFLECTION_BUILTIN_DECL(unsigned long long)
    OV_REFLECTION_BUILTIN_DECL(float)
    OV_REFLECTION_BUILTIN_DECL(double)
    OV_REFLECTION_BUILTIN_DECL(std::string)
#undef OV_REFLECTION_BUILTIN_DECL
    Error RegisterBuiltinTypes(Registry& registry);
}
