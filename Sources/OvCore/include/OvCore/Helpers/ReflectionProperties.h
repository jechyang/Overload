#pragma once
#include <OvReflection/Reflection.h>
#include <OvMaths/FVector3.h>
#include <OvMaths/FVector2.h>
#include <OvMaths/FVector4.h>
#include <OvMaths/FQuaternion.h>
#include <functional>
#include <optional>
#include <tinyxml2.h>

namespace OvCore::Helpers::ReflectionProperties
{
    using namespace OvReflection;
    enum class Status { Applied, Missing, Defaulted, ReadOnly, Invalid, Unsupported, AccessDenied, Expired };
    struct Result
    {
        std::string property;
        Status status = Status::Applied;
        Error access = Error::None;
        bool Ok() const { return status == Status::Applied || status == Status::Missing || status == Status::Defaulted || status == Status::ReadOnly; }
    };
    struct Report
    {
        std::vector<Result> fields;
        bool Ok() const;
        void Require() const; // Component's void serialization API propagates failure.
    };
    struct Pending { const PropertyDesc* property; std::any value; };
    struct CollectionOps
    {
        const PropertyDesc* element;
        bool dynamic;
        size_t (*size)(const std::any&);
        bool (*get)(const std::any&, size_t, std::any&);
        bool (*set)(std::any&, size_t, const std::any&);
        bool (*resize)(std::any&, size_t);
        bool (*erase)(std::any&, size_t);
        bool (*move)(std::any&, size_t, size_t);
    };
    struct ValueCodec
    {
        TypeKey key;
        bool (*parse)(std::string_view, std::any&);
        bool (*read)(const MainThread&, const PropertyDesc&, const tinyxml2::XMLNode&, std::any&, std::string_view);
        bool (*write)(const MainThread&, const PropertyDesc&, tinyxml2::XMLDocument&, tinyxml2::XMLNode&, const std::any&, std::string_view);
        bool (*resolve)(const MainThread&, std::any&);
        const TypeDesc* recordType = nullptr;
        std::optional<ObjectRef> (*borrowRecord)(std::any&) = nullptr;
        const CollectionOps* collection = nullptr;
    };
    // Generated from the actual annotated value types, with exact type keys.
    std::span<const ValueCodec> GetOvCoreValueCodecs();
    const ValueCodec* FindValueCodec(const PropertyDesc& property);
    std::string_view Meta(std::span<const Metadata> metadata, std::string_view key);
    std::vector<const PropertyDesc*> Properties(const TypeDesc& type, std::string_view capability);
    bool Parse(const PropertyDesc& property, std::string_view text, std::any& value);
    std::string Format(const std::any& value);
    bool ReadValueXML(const MainThread&, const PropertyDesc&, const tinyxml2::XMLNode&, std::any&);
    bool WriteValueXML(const MainThread&, const PropertyDesc&, tinyxml2::XMLDocument&, tinyxml2::XMLNode&, const std::any&);
    size_t CollectionLimit(const PropertyDesc&);
    // Stage performs all parsing before setters; absent fields default to Keep.
    Report Stage(const MainThread& main, ObjectRef object, const TypeDesc& type, const tinyxml2::XMLNode& node, std::vector<Pending>& pending, std::string_view prefix = {});
    bool HasSerializedFields(const TypeDesc& type, const tinyxml2::XMLNode& node, std::string_view prefix = {});
    Report Apply(const MainThread& main, ObjectRef object, std::vector<Pending>& pending, Report report);
    using SerializeHook = Report (*)(const MainThread&, ObjectRef, const TypeDesc&, tinyxml2::XMLDocument&, tinyxml2::XMLNode&);
    using DeserializeHook = Report (*)(const MainThread&, ObjectRef, const TypeDesc&, const tinyxml2::XMLNode&);
    struct Hooks { std::string_view id; SerializeHook serialize = nullptr; DeserializeHook deserialize = nullptr; };
    Report Serialize(const MainThread& main, ObjectRef object, const TypeDesc& type, tinyxml2::XMLDocument& doc, tinyxml2::XMLNode& node, std::span<const Hooks> hooks = {}, std::string_view prefix = {});
    Report Deserialize(const MainThread& main, ObjectRef object, const TypeDesc& type, const tinyxml2::XMLNode& node, std::span<const Hooks> hooks = {});
    std::span<const Hooks> ComponentHooks();

    // Adaptation uses exact C++ types. Enums travel as exact decimal strings.
    template<class F> bool Dispatch(const PropertyDesc& property, F&& f)
    {
        const auto& type = property.Info().type;
        if (type.category == "enum") { f.template operator()<std::string>(); return true; }
#define OV_VALUE(T) if (type.key == Key<T>()) { f.template operator()<T>(); return true; }
        OV_VALUE(bool) OV_VALUE(signed char) OV_VALUE(unsigned char)
        OV_VALUE(short) OV_VALUE(unsigned short) OV_VALUE(int) OV_VALUE(unsigned int)
        OV_VALUE(long) OV_VALUE(unsigned long) OV_VALUE(long long) OV_VALUE(unsigned long long)
        OV_VALUE(float) OV_VALUE(double) OV_VALUE(std::string)
        OV_VALUE(OvMaths::FVector2) OV_VALUE(OvMaths::FVector3) OV_VALUE(OvMaths::FVector4) OV_VALUE(OvMaths::FQuaternion)
#undef OV_VALUE
        return false;
    }
}
