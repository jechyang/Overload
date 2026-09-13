#pragma once
#include "Schema/Model.h"
namespace OvReflectGen
{
    bool WriteSchema(const Schema& schema, const std::string& output);
    bool WriteDependencies(const Schema& schema, const std::string& output);
}
