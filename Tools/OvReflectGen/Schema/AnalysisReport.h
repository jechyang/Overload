#pragma once

#include <string>
#include <vector>

namespace OvReflectGen
{
    inline constexpr int SchemaVersion = 1;
    inline constexpr const char* GeneratorVersion = "0.4.0";

    // R0 analysis evidence only; unannotated declarations are NOT public schema.
    struct AnalyzedType
    {
        std::string cppName;
        std::string kind;
        std::string source;
        unsigned line = 0;
        unsigned column = 0;
        std::vector<std::string> bases;
    };

    struct AnalysisReport
    {
        std::string module;
        std::string configuration;
        std::vector<AnalyzedType> types;
    };
}
