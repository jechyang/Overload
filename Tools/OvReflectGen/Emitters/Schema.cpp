#include "Emitters/Schema.h"
#include "Schema/AnalysisReport.h"
#include <algorithm>
#include <llvm/Support/JSON.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

namespace OvReflectGen
{
    namespace
    {
        llvm::json::Object SourceJson(const Source& s)
        { return llvm::json::Object{{"path", s.path}, {"line", s.line}, {"column", s.column}}; }
        llvm::json::Object ReferenceJson(const TypeRef& t)
        { return llvm::json::Object{{"display", t.display}, {"canonical", t.canonical}, {"value_type", t.valueType},
                  {"category", t.category}, {"bits", t.bits}, {"signed", t.isSigned}}; }
        llvm::json::Object MetadataJson(const Metadata& m)
        {
            llvm::json::Object object;
            for (const auto& [key, value] : m.values) object[key] = value;
            return object;
        }
        llvm::json::Array AliasesJson(const Metadata& m)
        {
            llvm::json::Array aliases;
            for (const auto& alias : m.aliases) aliases.push_back(alias);
            return aliases;
        }
        template<class T> llvm::json::Object Common(const T& item)
        { return llvm::json::Object{{"id", item.id}, {"cpp_name", item.cppName}, {"source", SourceJson(item.source)},
                  {"metadata", MetadataJson(item.metadata)}, {"aliases", AliasesJson(item.metadata)}}; }
        bool Write(llvm::json::Value value, const std::string& output)
        {
            std::error_code error;
            llvm::raw_fd_ostream stream(output, error, llvm::sys::fs::OF_None);
            if (error) { llvm::errs() << output << ": error: " << error.message() << "\n"; return false; }
            stream << llvm::formatv("{0:2}", value) << "\n";
            stream.close();
            if (stream.has_error())
            {
                llvm::errs() << output << ": error: failed to write reflection output\n";
                stream.clear_error();
                return false;
            }
            return true;
        }
    }
    bool WriteSchema(const Schema& schema, const std::string& output)
    {
        llvm::json::Array types;
        for (const auto& type : schema.types)
        {
            auto object = Common(type);
            object["kind"] = type.kind;
            llvm::json::Array bases, castBases, properties, methods, enumerators;
            for (const auto& base : type.bases) bases.push_back(llvm::json::Object{{"cpp_name", base.cppName}, {"access", base.access}});
            for (const auto& base : type.castBases) castBases.push_back(llvm::json::Object{{"cpp_name", base.cppName}, {"access", base.access}});
            for (const auto& property : type.properties)
            {
                auto item = Common(property);
                item["name"] = property.name;
                item["type"] = ReferenceJson(property.type);
                item["strategy"] = property.strategy;
                item["access"] = property.access;
                item["read_only"] = property.readOnly;
                item["getter"] = property.getter;
                item["setter"] = property.setter;
                properties.push_back(std::move(item));
            }
            for (const auto& method : type.methods)
            {
                auto item = Common(method);
                item["name"] = method.name;
                item["signature"] = method.signature;
                item["return_type"] = ReferenceJson(method.result);
                item["const"] = method.isConst;
                item["noexcept"] = method.isNoexcept;
                item["ref_qualifier"] = method.refQualifier;
                item["access"] = method.access;
                llvm::json::Array parameters;
                for (const auto& parameter : method.parameters)
                    parameters.push_back(llvm::json::Object{{"name", parameter.name}, {"type", ReferenceJson(parameter.type)}});
                item["parameters"] = std::move(parameters);
                methods.push_back(std::move(item));
            }
            for (const auto& enumerator : type.enumerators)
                enumerators.push_back(llvm::json::Object{{"name", enumerator.name}, {"value", enumerator.value}, {"source", SourceJson(enumerator.source)}});
            object["bases"] = std::move(bases);
            object["cast_bases"] = std::move(castBases);
            object["properties"] = std::move(properties);
            object["methods"] = std::move(methods);
            if (type.kind.starts_with("enum"))
            { object["underlying_type"] = ReferenceJson(type.underlying); object["enumerators"] = std::move(enumerators); }
            types.push_back(std::move(object));
        }
        return Write(llvm::json::Object{{"schema_version", SchemaVersion}, {"generator_version", GeneratorVersion},
            {"module", schema.module}, {"configuration", schema.configuration}, {"types", std::move(types)}}, output);
    }
    bool WriteDependencies(const Schema& schema, const std::string& output)
    {
        auto dependencies = schema.dependencies;
        std::sort(dependencies.begin(), dependencies.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        llvm::json::Array files;
        for (const auto& file : dependencies) files.push_back(llvm::json::Object{{"path", file.path}, {"sha256", file.sha256}});
        return Write(llvm::json::Object{{"schema_version", SchemaVersion}, {"algorithm", "sha256"}, {"files", std::move(files)}}, output);
    }
}
