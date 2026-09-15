#pragma once
#include <map>
#include <optional>
#include <string>
#include <OvCore/Scripting/Common/ScriptPropertyValue.h>
#include <OvCore/Scripting/Common/ManagedField.h>

namespace OvCore::Scripting
{
    // Runtime-independent instance surface. Backend contexts never escape this interface.
    class Script
    {
    public:
        virtual ~Script() = default;
        virtual bool IsValid() const = 0;
        virtual bool IsFaulted() const { return false; }
        virtual std::map<std::string, ScriptPropertyValue> GetDefaultProperties() const = 0;
        virtual std::optional<ScriptPropertyValue> GetProperty(const std::string& key) const = 0;
        virtual void SetProperty(const std::string& key, const ScriptPropertyValue& value) = 0;
        virtual const std::vector<ManagedField>& ManagedFields() const { static const std::vector<ManagedField> empty; return empty; }
        virtual std::optional<ManagedFieldValue> ReadManagedField(uint64_t) const { return {}; }
        virtual bool WriteManagedField(uint64_t, const ManagedFieldValue&) { return false; }
    };
}
