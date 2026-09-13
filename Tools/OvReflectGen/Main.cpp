#include <algorithm>
#include <filesystem>
#include <set>
#include <string_view>

#include <clang/Basic/Version.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

#include "Emitters/AnalysisReport.h"
#include "Emitters/Schema.h"
#include "Frontend/HeaderAnalysis.h"

static_assert(std::string_view(CLANG_VERSION_STRING) == "18.1.8",
              "OvReflectGen requires Clang 18.1.8 headers and libraries");

namespace
{
    llvm::cl::OptionCategory Category("OvReflectGen header analysis and reflection schema");
    llvm::cl::opt<std::string> Output("report", llvm::cl::Required, llvm::cl::cat(Category));
    llvm::cl::opt<std::string> Root("source-root", llvm::cl::Required, llvm::cl::cat(Category));
    llvm::cl::opt<std::string> Module("module", llvm::cl::Required, llvm::cl::cat(Category));
    llvm::cl::opt<std::string> Configuration("configuration", llvm::cl::Required, llvm::cl::cat(Category));
    llvm::cl::list<std::string> Expected("expect", llvm::cl::ZeroOrMore, llvm::cl::cat(Category));
    llvm::cl::opt<std::string> SchemaOutput("schema", llvm::cl::init(""), llvm::cl::cat(Category));
    llvm::cl::opt<std::string> DependenciesOutput("dependencies", llvm::cl::init(""), llvm::cl::cat(Category));
}

int main(int argc, const char** argv)
{
    auto options = clang::tooling::CommonOptionsParser::create(argc, argv, Category);
    if (!options)
    {
        llvm::errs() << llvm::toString(options.takeError()) << "\n";
        return 1;
    }
    std::error_code error;
    // Optional explicit paths preserve the R0 CLI. All success outputs invalidate together.
    const auto directory = std::filesystem::path(Output.getValue()).parent_path();
    const auto schemaPath = SchemaOutput.empty() ? (directory / "schema.json").string() : SchemaOutput.getValue();
    const auto dependenciesPath = DependenciesOutput.empty() ? (directory / "dependencies.json").string() : DependenciesOutput.getValue();
    const std::vector<std::string> outputs{Output.getValue(), schemaPath, dependenciesPath,
        (directory / "input-summary.json").string(),
        (directory / "Reflection.generated.h").string(),
        (directory / "Reflection.generated.cpp").string(),
        (directory / "Script.generated.h").string(),
        (directory / "Script.generated.inc").string(),
        (directory / "Script.generated.cs").string(),
        (directory / "script-abi.json").string(),
        (directory / "manifest.json").string()};
    if (std::set<std::string>(outputs.begin(), outputs.end()).size() != outputs.size())
    {
        llvm::errs() << "error: output paths must be distinct\n";
        return 1;
    }
    for (const auto& path : outputs)
    {
        std::filesystem::remove(path, error);
        if (error) { llvm::errs() << path << ": error: cannot invalidate old output: " << error.message() << "\n"; return 1; }
    }
    if (options->getSourcePathList().size() != 1)
    { llvm::errs() << "error: exactly one module scan translation unit is required\n"; return 1; }
    const std::set<std::string> expected(Expected.begin(), Expected.end());
    const std::string root = Root;
    OvReflectGen::AnalysisReport report{Module, Configuration, {}};
    OvReflectGen::Schema schema;
    schema.module = Module;
    schema.configuration = Configuration;
    clang::tooling::ClangTool tool(options->getCompilations(), options->getSourcePathList());
    const int result = tool.run(OvReflectGen::CreateHeaderAnalysis(report, expected, root, schema).get());
    if (result != 0 || schema.failed) return 1;
    const auto issues = OvReflectGen::ValidateSchema(schema);
    for (const auto& issue : issues)
        llvm::errs() << issue.source.path << ":" << issue.source.line << ":" << issue.source.column << ": error: " << issue.message << "\n";
    if (!issues.empty()) return 1;
    for (const auto& name : expected)
    {
        if (std::none_of(report.types.begin(), report.types.end(),
                        [&](const auto& type) { return type.cppName == name; }))
        {
            llvm::errs() << options->getSourcePathList().front()
                << ":1:1: error: expected complete type definition not found: " << name << "\n";
            return 1;
        }
    }
    std::sort(report.types.begin(), report.types.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.cppName < rhs.cppName; });
    if (OvReflectGen::WriteSchema(schema, schemaPath) && OvReflectGen::WriteDependencies(schema, dependenciesPath) &&
        OvReflectGen::WriteAnalysisReport(report, Output)) return 0;
    for (const auto& path : outputs) std::filesystem::remove(path, error);
    return 1;
}
