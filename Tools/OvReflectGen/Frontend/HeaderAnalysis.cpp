#include "Frontend/HeaderAnalysis.h"
#include "Frontend/SchemaExtraction.h"

#include <filesystem>
#include <utility>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>

namespace OvReflectGen
{
    namespace
    {
        class Visitor final : public clang::RecursiveASTVisitor<Visitor>
        {
        public:
            Visitor(AnalysisReport& report, const std::set<std::string>& expected,
                    const std::string& root, clang::ASTContext& context)
                : m_report(report), m_expected(expected), m_root(root), m_context(context) {}

            bool VisitCXXRecordDecl(clang::CXXRecordDecl* decl)
            {
                if (!decl->isThisDeclarationADefinition() || decl->isImplicit())
                    return true;
                const auto name = decl->getQualifiedNameAsString();
                if (!m_expected.count(name) || !m_seen.insert(name).second)
                    return true;
                const auto location = m_context.getSourceManager().getPresumedLoc(decl->getLocation());
                if (location.isInvalid())
                    return true;
                AnalyzedType type;
                type.cppName = name;
                type.kind = decl->isStruct() ? "struct" : decl->isUnion() ? "union" : "class";
                type.source = std::filesystem::path(location.getFilename()).lexically_normal()
                    .lexically_relative(std::filesystem::path(m_root).lexically_normal()).generic_string();
                type.line = location.getLine();
                type.column = location.getColumn();
                clang::PrintingPolicy policy(m_context.getLangOpts());
                policy.SuppressTagKeyword = true;
                policy.FullyQualifiedName = true;
                for (const auto& base : decl->bases())
                    type.bases.push_back(base.getType().getCanonicalType().getAsString(policy));
                m_report.types.push_back(std::move(type));
                return true;
            }

        private:
            AnalysisReport& m_report;
            const std::set<std::string>& m_expected;
            const std::string& m_root;
            clang::ASTContext& m_context;
            std::set<std::string> m_seen;
        };

        class Consumer final : public clang::ASTConsumer
        {
        public:
            Consumer(AnalysisReport& report, const std::set<std::string>& expected,
                     const std::string& root, Schema& schema)
                : m_report(report), m_expected(expected), m_root(root), m_schema(schema) {}
            void HandleTranslationUnit(clang::ASTContext& context) override
            {
                Visitor visitor(m_report, m_expected, m_root, context);
                visitor.TraverseDecl(context.getTranslationUnitDecl());
                if (!context.getDiagnostics().hasErrorOccurred()) ExtractSchema(context, m_root, m_schema);
            }
        private:
            Schema& m_schema;
            AnalysisReport& m_report;
            const std::set<std::string>& m_expected;
            const std::string& m_root;
        };

        class Action final : public clang::ASTFrontendAction
        {
        public:
            Action(AnalysisReport& report, const std::set<std::string>& expected,
                   const std::string& root, Schema& schema)
                : m_report(report), m_expected(expected), m_root(root), m_schema(schema) {}
            std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
                clang::CompilerInstance& compiler, llvm::StringRef) override
            {
                CollectDependencies(compiler, m_schema);
                return std::make_unique<Consumer>(m_report, m_expected, m_root, m_schema);
            }
        private:
            Schema& m_schema;
            AnalysisReport& m_report;
            const std::set<std::string>& m_expected;
            const std::string& m_root;
        };

        class Factory final : public clang::tooling::FrontendActionFactory
        {
        public:
            Factory(AnalysisReport& report, const std::set<std::string>& expected,
                    const std::string& root, Schema& schema)
                : m_report(report), m_expected(expected), m_root(root), m_schema(schema) {}
            std::unique_ptr<clang::FrontendAction> create() override
            {
                return std::make_unique<Action>(m_report, m_expected, m_root, m_schema);
            }
        private:
            Schema& m_schema;
            AnalysisReport& m_report;
            const std::set<std::string>& m_expected;
            const std::string& m_root;
        };
    }

    std::unique_ptr<clang::tooling::FrontendActionFactory> CreateHeaderAnalysis(
        AnalysisReport& report, const std::set<std::string>& expectedTypes,
        const std::string& sourceRoot, Schema& schema)
    {
        return std::make_unique<Factory>(report, expectedTypes, sourceRoot, schema);
    }
}
