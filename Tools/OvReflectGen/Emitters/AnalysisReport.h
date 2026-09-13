#pragma once
#include <string>
#include "Schema/AnalysisReport.h"
namespace OvReflectGen
{
    bool WriteAnalysisReport(const AnalysisReport& report, const std::string& output);
}
