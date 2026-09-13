#pragma once

#include <memory>
#include <set>
#include <string>

#include "Schema/AnalysisReport.h"
#include "Schema/Model.h"

namespace clang::tooling { class FrontendActionFactory; }

namespace OvReflectGen
{
    std::unique_ptr<clang::tooling::FrontendActionFactory> CreateHeaderAnalysis(
        AnalysisReport& report, const std::set<std::string>& expectedTypes,
        const std::string& sourceRoot, Schema& schema);
}
