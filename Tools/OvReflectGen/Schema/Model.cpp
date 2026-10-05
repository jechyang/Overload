#include "Schema/Model.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace OvReflectGen
{
    bool ParseMetadata(const std::string& text, const std::string& kind,
                       Metadata& output, std::string& error)
    {
        // Deliberately small grammar, not C++ expression evaluation.
        const std::set<std::string> flags = kind == "type" ? std::set<std::string>{"Component", "Record"} :
            kind == "method" ? std::set<std::string>{"Script"} :
            std::set<std::string>{"Serialize", "Inspect", "Script", "ReadOnly", "Flatten"};
        std::set<std::string> keys{"Id", "Alias", "Name"};
        if (kind == "type") keys.insert({"Factory", "Lookup", "DisplayName", "Category", "Order", "SerializeHook", "DeserializeHook"});
        else
        {
            keys.insert("Thread");
            keys.insert({"Return", "Ownership", "Nullable"});
            if (kind == "method") keys.insert("Overload");
            if (kind != "method") keys.insert({"DisplayName", "Category", "Min", "Max", "Step", "Drawer", "Order", "Missing", "Default", "ItemName", "Prefix", "Validate"});
            if (kind == "property") keys.insert("Setter");
        }
        size_t i = 0;
        auto space = [&] { while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i; };
        auto fail = [&](const std::string& message) { error = message; return false; };
        space();
        while (i < text.size())
        {
            const auto start = i;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) ++i;
            const auto key = text.substr(start, i - start);
            if (key.empty()) return fail("expected metadata key");
            space();
            std::string value;
            if (i < text.size() && text[i] == '=')
            {
                if (!keys.count(key)) return fail("unknown or flag-only metadata key: " + key);
                ++i; space();
                const bool quoted = i < text.size() && text[i] == '"';
                if (quoted)
                {
                    ++i;
                    while (i < text.size() && text[i] != '"')
                    {
                        char c = text[i++];
                        if (c == '\\')
                        {
                            if (i == text.size()) return fail("unterminated metadata escape");
                            c = text[i++];
                            if (c != '\\' && c != '"') return fail("only quote and backslash escapes are supported");
                        }
                        if (static_cast<unsigned char>(c) < 32) return fail("control character in metadata");
                        value += c;
                    }
                    if (i == text.size()) return fail("unterminated metadata string");
                    ++i;
                }
                else
                {
                    const auto begin = i;
                    while (i < text.size() && text[i] != ',' && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
                    value = text.substr(begin, i - begin);
                }
                if (value.empty() && key != "Default" && key != "Prefix") return fail("empty metadata value: " + key);
                if ((key == "Id" || key == "Alias" || key == "Name" || key == "DisplayName" || key == "Category" || key == "Drawer" || key == "SerializeHook" || key == "DeserializeHook" || key == "Default" || key == "Overload" || key == "ItemName" || key == "Prefix" || key == "Validate") && !quoted)
                    return fail(key + " requires a quoted string");
                if (key == "Setter" || key == "Factory" || key == "Lookup" || key == "Thread" || key == "Missing" || key == "Return" || key == "Ownership" || key == "Nullable")
                {
                    if (quoted || !(std::isalpha(static_cast<unsigned char>(value[0])) || value[0] == '_') ||
                        !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; }))
                        return fail(key + " requires an identifier");
                }
                if (key == "Order" || key == "Min" || key == "Max" || key == "Step")
                {
                    size_t consumed = 0;
                    try { (void)std::stod(value, &consumed); } catch (...) { return fail("invalid number: " + key); }
                    if (quoted || consumed != value.size() || value.find_first_of("nNiI") != std::string::npos)
                        return fail("invalid number: " + key);
                }
            }
            else
            {
                if (!flags.count(key)) return fail("unknown or value-required metadata key: " + key);
                value = "true";
            }
            if (key == "Alias") output.aliases.push_back(value);
            else if (!output.values.emplace(key, value).second) return fail("repeated metadata key: " + key);
            space();
            if (i == text.size()) break;
            if (text[i++] != ',') return fail("expected comma between metadata entries");
            space();
            if (i == text.size()) return fail("trailing metadata comma");
        }
        std::sort(output.aliases.begin(), output.aliases.end());
        if (output.Has("Thread") && output.Get("Thread") != "Main" && output.Get("Thread") != "Any")
            return fail("Thread must be Main or Any");
        if (output.Has("Missing") && output.Get("Missing") != "Keep" && output.Get("Missing") != "Default" && output.Get("Missing") != "Error")
            return fail("Missing must be Keep, Default or Error");
        if (output.Get("Missing") == "Default" && !output.Has("Default"))
            return fail("Missing = Default requires an explicit Default");
        if (output.Has("Default") && output.Get("Missing") != "Default")
            return fail("Default requires Missing = Default");
        for (const auto* key : {"Return", "Ownership", "Nullable", "Overload"})
            if (output.Has(key) && !output.Has("Script")) return fail(std::string(key) + " requires Script");
        if (output.Has("Script") && output.Get("Thread", "Main") != "Main") return fail("Script requires Main thread");
        if (output.Has("Return") && output.Get("Return") != "Copy") return fail("Return must be Copy");
        if (output.Has("Ownership") && output.Get("Ownership") != "Borrowed") return fail("Ownership must be Borrowed");
        if (output.Has("Nullable") && output.Get("Nullable") != "Allow" && output.Get("Nullable") != "Deny") return fail("Nullable must be Allow or Deny");
        return true;
    }

    static bool ValidDefault(const TypeRef& type, std::string_view text, const Schema& schema)
    {
        if (type.category == "string") return true;
        if (type.category == "resource-reference") return text.empty() || text == "?";
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == text.npos) return false;
        text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
        if (type.category == "enum")
        {
            for (const auto& t : schema.types) if (t.cppName == type.valueType) return ValidDefault(t.underlying, text, schema);
            return false;
        }
        if (type.category == "bool") return text == "true" || text == "false" || text == "0" || text == "1";
        if (type.category == "integer")
        {
            if (type.isSigned)
            {
                long long value{}; auto r = std::from_chars(text.data(), text.data() + text.size(), value);
                if (r.ec != std::errc{} || r.ptr != text.data() + text.size()) return false;
                const auto max = std::numeric_limits<long long>::max() >> (64 - type.bits);
                return value >= -max - 1 && value <= max;
            }
            unsigned long long value{}; auto r = std::from_chars(text.data(), text.data() + text.size(), value);
            return r.ec == std::errc{} && r.ptr == text.data() + text.size() && value <= (std::numeric_limits<unsigned long long>::max() >> (64 - type.bits));
        }
        if (type.category == "float")
        {
            double value{}; auto r = std::from_chars(text.data(), text.data() + text.size(), value);
            return r.ec == std::errc{} && r.ptr == text.data() + text.size() && std::isfinite(value) && (type.bits == 64 || std::abs(value) <= std::numeric_limits<float>::max());
        }
        const size_t count = type.valueType == "OvMaths::FVector2" ? 2 : type.valueType == "OvMaths::FVector3" ? 3 :
            (type.valueType == "OvMaths::FVector4" || type.valueType == "OvMaths::FQuaternion") ? 4 : 0;
        if (!count) return false;
        TypeRef scalar; scalar.category = "float"; scalar.bits = 32;
        for (size_t index = 0; index < count; ++index)
        {
            const auto comma = text.find(',');
            if (!ValidDefault(scalar, text.substr(0, comma), schema)) return false;
            if (index + 1 == count) return comma == text.npos;
            if (comma == text.npos) return false;
            text.remove_prefix(comma + 1);
        }
        return false;
    }

    std::vector<Issue> ValidateSchema(Schema& schema)
    {
        std::vector<Issue> issues;
        std::map<std::string, Source> identities;
        auto identity = [&](const std::string& id, const Source& source)
        {
            auto [it, inserted] = identities.emplace(id, source);
            if (!inserted) issues.push_back({source, "duplicate ID or alias '" + id + "'; first declared at " +
                it->second.path + ":" + std::to_string(it->second.line)});
        };
        auto aliases = [&](const auto& item)
        {
            identity(item.id, item.source);
            for (const auto& alias : item.metadata.aliases) identity(alias, item.source);
        };
        std::map<std::string, const Type*> byName;
        for (auto& type : schema.types)
        {
            aliases(type);
            byName.emplace(type.cppName, &type);
            if (type.metadata.Has("Record") && (type.metadata.Has("SerializeHook") || type.metadata.Has("DeserializeHook")))
                issues.push_back({type.source, "Record cannot specify component serialization hooks"});
            std::set<std::string> names;
            for (const auto& property : type.properties)
            {
                aliases(property);
                if (type.metadata.Has("Record") && property.readOnly)
                    issues.push_back({property.source, "Record fields require public writable OV_FIELD declarations"});
                if (property.metadata.Has("Flatten") && (property.type.category != "record-adapter" || !property.metadata.Has("Serialize")))
                    issues.push_back({property.source, "Flatten requires a serializable Record value"});
                if (property.metadata.Has("Flatten") && !property.metadata.aliases.empty())
                    issues.push_back({property.source, "Flatten aliases belong on the Record fields"});
                if (property.metadata.Has("Prefix") && !property.metadata.Has("Flatten"))
                    issues.push_back({property.source, "Prefix requires Flatten"});
                if (property.metadata.Has("Prefix"))
                {
                    const auto prefix = property.metadata.Get("Prefix");
                    if (!prefix.empty() && !(std::isalpha(static_cast<unsigned char>(prefix.front())) || prefix.front() == '_'))
                        issues.push_back({property.source, "Prefix must start with a letter or underscore"});
                    for (unsigned char c : property.metadata.Get("Prefix")) if (!(c < 128 && (std::isalnum(c) || c == '_')))
                    { issues.push_back({property.source, "Prefix must contain ASCII XML identifier characters"}); break; }
                }
                if (property.metadata.Has("Validate"))
                {
                    const auto rule = property.metadata.Get("Validate");
                    if (!((rule == "Positive" && property.type.category == "float") || (rule == "EnumChoice" && property.type.category == "enum")))
                        issues.push_back({property.source, "Validate must be Positive on a float or EnumChoice on an enum"});
                    if (rule == "Positive" && property.metadata.Has("Default") && std::strtod(property.metadata.Get("Default").c_str(), nullptr) <= 0)
                        issues.push_back({property.source, "Default violates Positive validation"});
                }
                if (property.metadata.Has("ItemName"))
                {
                    const auto name = property.metadata.Get("ItemName");
                    if (name.empty() || !std::isalpha(static_cast<unsigned char>(name.front())) ||
                        !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; }))
                        issues.push_back({property.source, "ItemName must be an ASCII XML identifier"});
                }
                if (property.metadata.Has("Default") && !ValidDefault(property.type, property.metadata.Get("Default"), schema))
                    issues.push_back({property.source, "Default does not match property value type/range"});
                TypeRef rangeType = property.type;
                if (rangeType.category == "value-adapter")
                { rangeType.category = "float"; rangeType.bits = 32; }
                if (rangeType.category == "integer" || rangeType.category == "float")
                    for (const auto* key : {"Min", "Max"})
                        if (property.metadata.Has(key) && !ValidDefault(rangeType, property.metadata.Get(key), schema))
                            issues.push_back({property.source, std::string(key) + " does not match property value type/range"});
                TypeRef stepType; stepType.category = "float"; stepType.bits = 32;
                if (property.metadata.Has("Step") && (!ValidDefault(stepType, property.metadata.Get("Step"), schema) || std::stod(property.metadata.Get("Step")) <= 0))
                    issues.push_back({property.source, "Step must be positive and representable as float"});
                if (property.metadata.Has("Min") && property.metadata.Has("Max") && std::stod(property.metadata.Get("Min")) > std::stod(property.metadata.Get("Max")))
                    issues.push_back({property.source, "Min must not exceed Max"});
                if (!names.insert(property.name).second)
                    issues.push_back({property.source, "duplicate property name: " + property.name});
            }
            std::set<std::string> methodNames;
            for (const auto& method : type.methods)
            {
                aliases(method);
                if (!methodNames.insert(method.name).second)
                    issues.push_back({method.source, "duplicate method export name: " + method.name});
            }
        }
        auto inherited = [&](const Type& type, auto& names)
        {
            for (const auto& base : type.castBases)
                if (auto it = byName.find(base.cppName); it != byName.end())
                    for (const auto& property : it->second->properties)
                        if (!names.insert(property.name).second)
                            issues.push_back({property.source, "inherited property conflict: " + property.name});
            for (const auto& property : type.properties)
                if (!names.insert(property.name).second)
                    issues.push_back({property.source, "inherited property conflict: " + property.name});
        };
        for (auto& type : schema.types)
        {
            std::set<std::string> names;
            inherited(type, names);
            std::map<std::string, const Property*> wireNames;
            std::function<void(const Type&, const std::string&)> collectWireNames;
            auto collectProperties = [&](const Type& owner, const std::string& prefix)
            {
                for (const auto& property : owner.properties)
                {
                    if (!property.metadata.Has("Serialize")) continue;
                    if (property.metadata.Has("Flatten") && property.type.category == "record-adapter")
                    {
                        if (auto record = byName.find(property.type.valueType); record != byName.end())
                            collectWireNames(*record->second, prefix + property.metadata.Get("Prefix"));
                        continue;
                    }
                    const auto insert = [&](const std::string& name) {
                        auto [entry, added] = wireNames.emplace(prefix + name, &property);
                        if (!added)
                            issues.push_back({property.source, "serialized key/alias conflict: " + prefix + name});
                    };
                    insert(property.name);
                    for (const auto& alias : property.metadata.aliases) insert(alias);
                }
            };
            collectWireNames = [&](const Type& owner, const std::string& prefix) {
                for (const auto& base : owner.castBases)
                    if (auto found = byName.find(base.cppName); found != byName.end()) collectProperties(*found->second, prefix);
                collectProperties(owner, prefix);
            };
            collectWireNames(type, "");
            auto byId = [](const auto& a, const auto& b) { return a.id < b.id; };
            std::sort(type.properties.begin(), type.properties.end(), byId);
            std::sort(type.methods.begin(), type.methods.end(), byId);
        }
        std::sort(schema.types.begin(), schema.types.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        return issues;
    }
}
