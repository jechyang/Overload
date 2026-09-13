#pragma once

#include <map>
#include <string>
#include <vector>

namespace OvReflectGen
{
    struct Source { std::string path; unsigned line = 0, column = 0; };
    struct Metadata
    {
        std::map<std::string, std::string> values;
        std::vector<std::string> aliases;
        bool Has(const std::string& key) const { return values.count(key) != 0; }
        std::string Get(const std::string& key, const std::string& fallback = "") const
        { auto i = values.find(key); return i == values.end() ? fallback : i->second; }
    };
    struct TypeRef
    {
        std::string display, canonical, valueType, category;
        unsigned bits = 0;
        bool isSigned = false;
    };
    struct Parameter { std::string name; TypeRef type; };
    struct Method
    {
        std::string id, name, cppName, signature, access, refQualifier;
        Source source;
        Metadata metadata;
        TypeRef result;
        std::vector<Parameter> parameters;
        bool isConst = false, isNoexcept = false;
    };
    struct Property
    {
        std::string id, name, cppName, strategy, getter, setter, access;
        Source source;
        Metadata metadata;
        TypeRef type;
        bool readOnly = false;
    };
    struct Base { std::string cppName, access; };
    struct Enumerator { std::string name, value; Source source; };
    struct Type
    {
        std::string id, cppName, kind;
        Source source;
        Metadata metadata;
        std::vector<Base> bases;
        // All public single-inheritance ancestors, nearest first, including unannotated gaps.
        std::vector<Base> castBases;
        std::vector<Property> properties;
        std::vector<Method> methods;
        TypeRef underlying;
        std::vector<Enumerator> enumerators;
    };
    struct Dependency { std::string path, sha256; };
    struct Schema
    {
        std::string module, configuration;
        std::vector<Type> types;
        std::vector<Dependency> dependencies;
        bool failed = false;
    };
    struct Issue { Source source; std::string message; };
    // No Clang types below this boundary.
    bool ParseMetadata(const std::string& text, const std::string& kind,
                       Metadata& output, std::string& error);
    std::vector<Issue> ValidateSchema(Schema& schema);
}
