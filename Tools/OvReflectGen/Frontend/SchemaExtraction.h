#pragma once
#include <memory>
#include <string>
#include "Schema/Model.h"
namespace clang { class ASTContext; class CompilerInstance; }
namespace OvReflectGen
{
    void ExtractSchema(clang::ASTContext& context, const std::string& root, Schema& schema);
    void CollectDependencies(clang::CompilerInstance& compiler, Schema& schema);
}
