#include "Emitters/AnalysisReport.h"

#include <clang/Basic/Version.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

namespace OvReflectGen
{
    bool WriteAnalysisReport(const AnalysisReport& report, const std::string& output)
    {
        llvm::json::Array types;
        for (const auto& type : report.types)
        {
            llvm::json::Array bases;
            for (const auto& base : type.bases)
                bases.push_back(base);
            types.push_back(llvm::json::Object{
                {"cpp_name", type.cppName}, {"kind", type.kind},
                {"source", type.source}, {"line", type.line}, {"column", type.column},
                {"bases", std::move(bases)}});
        }
        llvm::json::Value document = llvm::json::Object{
            {"schema_version", SchemaVersion}, {"generator_version", GeneratorVersion},
            {"clang_version", CLANG_VERSION_STRING}, {"stage", "R0"},
            {"mode", "libtooling"}, {"success", true},
            {"module", report.module}, {"configuration", report.configuration},
            {"types", std::move(types)}};
        std::error_code error;
        llvm::raw_fd_ostream stream(output, error, llvm::sys::fs::OF_Text);
        if (error)
        {
            llvm::errs() << output << ": error: " << error.message() << "\n";
            return false;
        }
        stream << llvm::formatv("{0:2}", document) << "\n";
        stream.close();
        if (stream.has_error())
        {
            llvm::errs() << output << ": error: failed to write analysis report\n";
            stream.clear_error();
            return false;
        }
        return true;
    }
}
