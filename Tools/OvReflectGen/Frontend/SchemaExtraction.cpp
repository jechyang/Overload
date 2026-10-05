#include "Frontend/SchemaExtraction.h"

#include <filesystem>
#include <set>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/SHA256.h>

namespace OvReflectGen
{
    namespace
    {
        class Extractor final : public clang::RecursiveASTVisitor<Extractor>
        {
            clang::ASTContext& context;
            Schema& schema;
            std::string root;
            clang::PrintingPolicy policy;
            std::set<const clang::Decl*> seen;

            void Error(const clang::Decl* decl, const std::string& message)
            {
                schema.failed = true;
                auto& diagnostics = context.getDiagnostics();
                diagnostics.Report(decl->getLocation(), diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Error, "%0")) << message;
            }
            Source Location(const clang::Decl* decl)
            {
                const auto& sm = context.getSourceManager();
                const auto loc = sm.getExpansionLoc(decl->getLocation());
                return {std::filesystem::path(sm.getFilename(loc).str()).lexically_normal()
                    .lexically_relative(std::filesystem::path(root).lexically_normal()).generic_string(),
                    sm.getExpansionLineNumber(loc), sm.getExpansionColumnNumber(loc)};
            }
            const clang::AnnotateAttr* Annotation(const clang::Decl* decl)
            {
                const clang::AnnotateAttr* result = nullptr;
                for (const auto* attr : decl->specific_attrs<clang::AnnotateAttr>())
                    if (attr->getAnnotation().starts_with("ov."))
                    {
                        if (result) Error(decl, "multiple reflection annotations on one declaration");
                        result = attr;
                    }
                return result;
            }
            bool MetadataFor(const clang::Decl* decl, const std::string& kind, Metadata& metadata)
            {
                const auto* attr = Annotation(decl);
                if (!attr) return false;
                const std::string prefix = "ov." + kind + ":";
                if (!attr->getAnnotation().starts_with(prefix))
                { Error(decl, "invalid annotation target; expected OV_" + kind); return false; }
                std::string error;
                if (!ParseMetadata(attr->getAnnotation().drop_front(prefix.size()).str(), kind, metadata, error))
                { Error(decl, error); return false; }
                return true;
            }
            std::string Spelling(clang::QualType type) { return type.getAsString(policy); }
            bool ResourceReference(clang::QualType value)
            {
                if (value.isConstQualified() || value.isVolatileQualified()) return false;
                const auto* record = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(value->getAsCXXRecordDecl());
                if (!record || record->getSpecializedTemplate()->getQualifiedNameAsString() != "OvCore::Resources::ResourceReference" ||
                    record->getTemplateArgs().size() != 1 || record->getTemplateArgs()[0].getKind() != clang::TemplateArgument::Type) return false;
                const auto argument = record->getTemplateArgs()[0].getAsType();
                if (argument.isConstQualified() || argument.isVolatileQualified()) return false;
                const auto* resource = argument->getAsCXXRecordDecl();
                if (!resource) return false;
                const auto name = resource->getQualifiedNameAsString();
                return name == "OvRendering::Resources::Model" || name == "OvRendering::Resources::Texture" ||
                    name == "OvRendering::Resources::Shader" || name == "OvCore::Resources::Material" || name == "OvAudio::Resources::Sound";
            }
            bool ResourceArray(clang::QualType value)
            {
                const auto* record = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(value->getAsCXXRecordDecl());
                if (!record || record->getSpecializedTemplate()->getQualifiedNameAsString() != "std::array" || record->getTemplateArgs().size() != 2) return false;
                const auto& item = record->getTemplateArgs()[0]; const auto& count = record->getTemplateArgs()[1];
                return item.getKind() == clang::TemplateArgument::Type && ResourceReference(item.getAsType()) &&
                    count.getKind() == clang::TemplateArgument::Integral && count.getAsIntegral() > 0 && count.getAsIntegral() <= 4096;
            }
            void CheckResourceProperty(const Property& property, const clang::Decl* decl)
            {
                const bool resource = property.type.category == "resource-reference" || property.type.category == "resource-array";
                if (resource && (!property.metadata.Has("Serialize") || property.metadata.Has("Inspect") || property.metadata.Has("Script")))
                    Error(decl, "resource adapters currently require Serialize only; use the typed asset picker for Inspector");
                if (property.metadata.Has("ItemName") != (property.type.category == "resource-array"))
                    Error(decl, "ItemName is required exclusively on fixed resource arrays");
            }
            TypeRef Reference(clang::QualType type, const clang::Decl* decl, bool allowVoid = false, bool allowObject = false)
            {
                TypeRef out;
                out.display = Spelling(type);
                out.canonical = Spelling(type.getCanonicalType());
                auto value = type.getNonReferenceType().getUnqualifiedType().getCanonicalType();
                out.valueType = Spelling(value);
                if (type->isRValueReferenceType() || type.isVolatileQualified() || type.getNonReferenceType().isVolatileQualified())
                    Error(decl, "unsupported reflected type: " + out.display);
                if (value->isBooleanType()) out.category = "bool";
                else if (value->isIntegerType() && !value->isSpecificBuiltinType(clang::BuiltinType::Char_S) &&
                         !value->isSpecificBuiltinType(clang::BuiltinType::Char_U) && !value->isWideCharType() &&
                         (context.getTypeSize(value) == 8 || context.getTypeSize(value) == 16 ||
                          context.getTypeSize(value) == 32 || context.getTypeSize(value) == 64))
                { out.category = "integer"; out.isSigned = value->isSignedIntegerType(); }
                else if (value->isSpecificBuiltinType(clang::BuiltinType::Float) || value->isSpecificBuiltinType(clang::BuiltinType::Double))
                    out.category = "float";
                else if (value->isVoidType() && allowVoid) out.category = "void";
                else if (const auto* enumeration = value->getAs<clang::EnumType>())
                {
                    out.category = "enum";
                    const auto* definition = enumeration->getDecl()->getDefinition();
                    if (!definition || !Annotation(definition)) Error(decl, "enum value type requires a complete OV_TYPE declaration");
                }
                else if (out.valueType == "OvMaths::FVector3" || out.valueType == "OvMaths::FQuaternion") out.category = "value-adapter";
                else if (ResourceReference(value)) out.category = "resource-reference";
                else if (ResourceArray(value)) out.category = "resource-array";
                else if (const auto* record = value->getAsCXXRecordDecl(); record && record->getQualifiedNameAsString() == "std::basic_string")
                {
                    // Recognize the actual std::string specialization, not arbitrary containers or wide strings.
                    const auto* specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
                    if (specialization && specialization->getTemplateArgs().size() == 3 &&
                        specialization->getTemplateArgs()[0].getAsType()->isCharType() &&
                        Spelling(value) == "std::basic_string<char>") out.category = "string";
                }
                if (allowObject && value->isPointerType()) {
                    auto pointee = value->getPointeeType();
                    if (const auto* object = pointee->getAsCXXRecordDecl(); object && !pointee.isVolatileQualified() &&
                        object->getDefinition() && Annotation(object->getDefinition())) out.category = "object-handle";
                }
                if (out.category.empty()) Error(decl, "unsupported reflected type: " + out.display);
                if (value->isBuiltinType() && !value->isVoidType() && !value->isDependentType()) out.bits = static_cast<unsigned>(context.getTypeSize(value));
                return out;
            }
            std::string Access(const clang::Decl* decl)
            {
                return decl->getAccess() == clang::AS_private ? "private" : decl->getAccess() == clang::AS_protected ? "protected" : "public";
            }
            bool Authorized(const clang::CXXRecordDecl* owner)
            {
                for (const auto* friendDecl : owner->friends())
                {
                    const auto* info = friendDecl->getFriendType();
                    if (!info) continue;
                    const auto* target = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(info->getType()->getAsCXXRecordDecl());
                    if (target && target->getSpecializedTemplate()->getQualifiedNameAsString() == "OvReflection::Access" &&
                        target->getTemplateArgs().size() == 1 && target->getTemplateArgs()[0].getKind() == clang::TemplateArgument::Type &&
                        context.hasSameType(target->getTemplateArgs()[0].getAsType(), context.getRecordType(owner))) return true;
                }
                return false;
            }
            void CheckAccess(const clang::Decl* decl, const clang::CXXRecordDecl* owner)
            {
                if (decl->getAccess() != clang::AS_public && !Authorized(owner))
                    Error(decl, "non-public reflection access requires friend struct OvReflection::Access<ThisType>");
            }
            bool DerivesComponent(const clang::CXXRecordDecl* record)
            {
                if (record->getQualifiedNameAsString() == "OvCore::ECS::Components::AComponent") return true;
                for (const auto& base : record->bases())
                    if (auto* next = base.getType()->getAsCXXRecordDecl(); next && next->hasDefinition() && DerivesComponent(next->getDefinition())) return true;
                return false;
            }
            void CheckHierarchy(const clang::CXXRecordDecl* record, const clang::CXXRecordDecl* reflected)
            {
                if (record->getNumBases() > 1) Error(reflected, "multiple inheritance is unsupported (including unannotated ancestors)");
                for (const auto& base : record->bases())
                {
                    if (base.isVirtual() || base.getAccessSpecifier() != clang::AS_public)
                        Error(reflected, "only public non-virtual single inheritance is supported (including unannotated ancestors)");
                    if (const auto* parent = base.getType()->getAsCXXRecordDecl(); parent && parent->hasDefinition())
                        CheckHierarchy(parent->getDefinition(), reflected);
                }
            }
            void CheckMethod(const clang::CXXMethodDecl* method, const clang::CXXRecordDecl* owner)
            {
                CheckAccess(method, owner);
                if (method->isStatic() || method->isVariadic() || method->isDeleted() || method->isVolatile() ||
                    method->getDescribedFunctionTemplate() || method->getTemplatedKind() != clang::FunctionDecl::TK_NonTemplate ||
                    llvm::isa<clang::CXXConstructorDecl, clang::CXXDestructorDecl, clang::CXXConversionDecl>(method) || method->isOverloadedOperator())
                    Error(method, "unsupported reflected method declaration");
            }
            void CheckScript(const Metadata& metadata, clang::QualType result, const std::vector<clang::QualType>& arguments, const clang::Decl* decl)
            {
                if (!metadata.Has("Script")) return;
                if (result->isLValueReferenceType() && metadata.Get("Return") != "Copy")
                    Error(decl, "Script reference return requires Return = Copy");
                bool object = result.getNonReferenceType()->isPointerType();
                for (auto arg : arguments) {
                    object = object || arg.getNonReferenceType()->isPointerType();
                    if (arg->isReferenceType() && !arg.getNonReferenceType().isConstQualified())
                        Error(decl, "Script mutable reference parameters are unsupported; use values or const references");
                }
                if (object && (metadata.Get("Ownership") != "Borrowed" || !metadata.Has("Nullable")))
                    Error(decl, "Script object handles require Ownership = Borrowed and explicit Nullable");
                if (object && (metadata.Has("Serialize") || metadata.Has("Inspect")))
                    Error(decl, "Object handle adapters are Script-only");
                if (!object && (metadata.Has("Ownership") || metadata.Has("Nullable")))
                    Error(decl, "Ownership/Nullable require an object handle signature");
            }
            void Field(const clang::FieldDecl* field, const clang::CXXRecordDecl* owner, Type& type)
            {
                Property out;
                if (!MetadataFor(field, "field", out.metadata)) return;
                CheckAccess(field, owner);
                if (field->isBitField() || field->getType()->isReferenceType()) Error(field, "bitfields and reference fields are unsupported");
                out.cppName = field->getNameAsString();
                out.name = out.metadata.Get("Name", out.cppName);
                out.id = out.metadata.Get("Id", type.id + "." + out.name);
                out.strategy = "field";
                out.access = Access(field);
                out.readOnly = out.metadata.Has("ReadOnly") || field->getType().isConstQualified();
                out.source = Location(field);
                out.type = Reference(field->getType(), field, false, out.metadata.Has("Script"));
                CheckResourceProperty(out, field);
                CheckScript(out.metadata, field->getType(), {}, field);
                type.properties.push_back(std::move(out));
            }
            void PropertyGetter(const clang::CXXMethodDecl* getter, const clang::CXXRecordDecl* owner, Type& type)
            {
                Property out;
                if (!MetadataFor(getter, "property", out.metadata)) return;
                CheckMethod(getter, owner);
                if (getter->getNumParams() != 0 || getter->getReturnType()->isVoidType() || !getter->isConst() || getter->getRefQualifier() == clang::RQ_RValue)
                    Error(getter, "property getter must be a const, zero-argument value getter callable on an lvalue");
                out.cppName = getter->getNameAsString();
                out.name = out.metadata.Get("Name");
                if (out.name.empty())
                {
                    if (out.cppName.starts_with("Get")) out.name = out.cppName.substr(3);
                    else if (out.cppName.starts_with("Is")) out.name = out.cppName.substr(2);
                    if (out.name.empty()) Error(getter, "cannot infer property name; specify Name");
                }
                out.id = out.metadata.Get("Id", type.id + "." + out.name);
                out.strategy = "property";
                out.getter = getter->getQualifiedNameAsString();
                out.access = Access(getter);
                out.source = Location(getter);
                out.type = Reference(getter->getReturnType(), getter, false, out.metadata.Has("Script"));
                CheckResourceProperty(out, getter);
                CheckScript(out.metadata, getter->getReturnType(), {}, getter);
                out.readOnly = out.metadata.Has("ReadOnly");
                if (out.readOnly && out.metadata.Has("Setter")) Error(getter, "ReadOnly property cannot specify Setter");
                if (!out.readOnly && !out.metadata.Has("Setter")) Error(getter, "writable property requires Setter; otherwise specify ReadOnly");
                if (out.metadata.Has("Setter"))
                {
                    std::vector<const clang::CXXMethodDecl*> candidates;
                    unsigned named = 0;
                    for (const auto* member : owner->decls())
                    {
                        const auto* nd = llvm::dyn_cast<clang::NamedDecl>(member);
                        if (nd && nd->getNameAsString() == out.metadata.Get("Setter"))
                        {
                            ++named;
                            if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(member)) candidates.push_back(method);
                        }
                    }
                    if (named != 1 || candidates.size() != 1) Error(getter, "Setter must select one non-template, non-overloaded method in the declaring type");
                    else
                    {
                        const auto* setter = candidates.front();
                        CheckMethod(setter, owner);
                        if (setter->getNumParams() != 1 || !setter->getReturnType()->isVoidType() || setter->isConst() || setter->getRefQualifier() == clang::RQ_RValue)
                            Error(setter, "invalid Setter signature: expected void(T) or void(const T&) on an lvalue");
                        else
                        {
                            const auto argument = setter->getParamDecl(0)->getType();
                            Reference(argument, setter, false, out.metadata.Has("Script"));
                            if (!context.hasSameType(argument.getNonReferenceType().getUnqualifiedType(), getter->getReturnType().getNonReferenceType().getUnqualifiedType()) ||
                                (argument->isReferenceType() && (!argument->isLValueReferenceType() || !argument.getNonReferenceType().isConstQualified())))
                                Error(setter, "Setter value type does not match getter or uses mutable reference");
                        }
                        out.setter = setter->getQualifiedNameAsString();
                    }
                }
                type.properties.push_back(std::move(out));
            }
            void ExportMethod(const clang::CXXMethodDecl* method, const clang::CXXRecordDecl* owner, Type& type)
            {
                Method out;
                if (!MetadataFor(method, "method", out.metadata)) return;
                CheckMethod(method, owner);
                unsigned overloads = 0;
                for (const auto* member : owner->decls())
                    if (const auto* candidate = llvm::dyn_cast<clang::NamedDecl>(member);
                        candidate && candidate->getDeclName() == method->getDeclName()) ++overloads;
                if (overloads > 1 && (!out.metadata.Has("Script") || !out.metadata.Has("Overload")))
                    Error(method, "overloaded OV_METHOD requires Script and explicit Overload signature");
                if (out.metadata.Has("Overload") && out.metadata.Get("Overload") != Spelling(method->getType().getCanonicalType()))
                    Error(method, "Overload must exactly match canonical signature: " + Spelling(method->getType().getCanonicalType()));
                std::vector<clang::QualType> arguments;
                for (const auto* parameter : method->parameters()) arguments.push_back(parameter->getType());
                CheckScript(out.metadata, method->getReturnType(), arguments, method);
                if (out.metadata.Has("Script") && method->getRefQualifier() == clang::RQ_RValue)
                    Error(method, "Script methods must be callable on an lvalue");
                out.cppName = method->getQualifiedNameAsString();
                out.name = out.metadata.Get("Name", method->getNameAsString());
                out.id = out.metadata.Get("Id", type.id + "." + out.name);
                out.signature = Spelling(method->getType().getCanonicalType());
                out.access = Access(method);
                out.source = Location(method);
                out.result = Reference(method->getReturnType(), method, true, out.metadata.Has("Script"));
                out.isConst = method->isConst();
                out.refQualifier = method->getRefQualifier() == clang::RQ_LValue ? "&" : method->getRefQualifier() == clang::RQ_RValue ? "&&" : "";
                out.isNoexcept = method->getType()->getAs<clang::FunctionProtoType>()->isNothrow();
                for (const auto* parameter : method->parameters())
                    out.parameters.push_back({parameter->getNameAsString(), Reference(parameter->getType(), parameter, false, out.metadata.Has("Script"))});
                type.methods.push_back(std::move(out));
            }
        public:
            Extractor(clang::ASTContext& c, const std::string& r, Schema& s)
                : context(c), schema(s), root(r), policy(c.getLangOpts())
            { policy.SuppressTagKeyword = true; policy.FullyQualifiedName = true; }

            bool VisitDecl(clang::Decl* decl)
            {
                if (decl->isImplicit()) return true;
                const auto* annotation = Annotation(decl);
                if (!annotation) return true;
                if (const auto* tag = llvm::dyn_cast<clang::TagDecl>(decl))
                {
                    const auto* definition = tag->getDefinition();
                    if (!definition) { Error(decl, "OV_TYPE requires a complete definition in the scan"); return true; }
                    if (!seen.insert(tag->getCanonicalDecl()).second) return true;
                    Type type;
                    if (!MetadataFor(definition, "type", type.metadata))
                    {
                        // Clang may keep a forward annotation on its redeclaration only.
                        if (!MetadataFor(decl, "type", type.metadata)) return true;
                    }
                    type.cppName = definition->getQualifiedNameAsString();
                    type.id = type.metadata.Get("Id", type.cppName);
                    type.source = Location(definition);
                    if (definition->getName().empty() || definition->getDeclContext()->isFunctionOrMethod() || definition->isInAnonymousNamespace() || definition->isDependentContext())
                        Error(definition, "anonymous, local and template reflected types are unsupported");
                    for (auto* dc = definition->getDeclContext(); dc && !dc->isTranslationUnit(); dc = dc->getParent())
                        if (dc->isFunctionOrMethod()) Error(definition, "local reflected types are unsupported");
                    if (definition->getAccess() == clang::AS_private || definition->getAccess() == clang::AS_protected)
                        Error(definition, "non-public nested reflected types are unsupported");
                    if (type.metadata.Has("Name")) Error(definition, "type names are extracted from AST; use DisplayName or Id");
                    if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(definition))
                    {
                        type.kind = record->isStruct() ? "struct" : "class";
                        if (record->isUnion()) Error(record, "reflected union is unsupported");
                        if (record->getDescribedClassTemplate() || llvm::isa<clang::ClassTemplateSpecializationDecl>(record)) Error(record, "reflected templates are unsupported");
                        CheckHierarchy(record, record);
                        for (const auto& base : record->bases())
                        {
                            type.bases.push_back({Spelling(base.getType().getCanonicalType()), "public"});
                        }
                        for (const auto* ancestor = record; ancestor && ancestor->getNumBases() == 1;)
                        {
                            const auto& base = *ancestor->bases_begin();
                            type.castBases.push_back({Spelling(base.getType().getCanonicalType()), "public"});
                            ancestor = base.getType()->getAsCXXRecordDecl();
                        }
                        if (type.metadata.Has("Component"))
                        {
                            if (!DerivesComponent(record)) Error(record, "Component requires AComponent inheritance");
                            if (type.metadata.Has("Factory") == type.metadata.Has("Lookup")) Error(record, "Component requires exactly one Factory or Lookup strategy");
                            if (type.metadata.Has("Factory") && (record->isAbstract() || type.metadata.Get("Factory") != "AddComponent")) Error(record, "Component Factory must be AddComponent on a concrete type");
                            if (type.metadata.Has("Lookup") && type.metadata.Get("Lookup") != "Existing") Error(record, "Component Lookup must be Existing");
                        }
                        else if (type.metadata.Has("Factory") || type.metadata.Has("Lookup")) Error(record, "Factory/Lookup requires Component");
                        for (const auto* member : record->decls())
                        {
                            if (!Annotation(member)) continue;
                            if (const auto* field = llvm::dyn_cast<clang::FieldDecl>(member)) Field(field, record, type);
                            else if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(member))
                            {
                                if (Annotation(method)->getAnnotation().starts_with("ov.property:")) PropertyGetter(method, record, type);
                                else ExportMethod(method, record, type);
                            }
                        }
                        for (auto& property : type.properties)
                            if (type.metadata.Has("Component"))
                            {
                                if (property.metadata.Get("Thread", "Main") != "Main") Error(record, "component properties require Thread = Main");
                                property.metadata.values["Thread"] = "Main";
                            }
                    }
                    else if (const auto* enumeration = llvm::dyn_cast<clang::EnumDecl>(definition))
                    {
                        type.kind = enumeration->isScoped() ? "enum class" : "enum";
                        if (type.metadata.Has("Component") || type.metadata.Has("Factory") || type.metadata.Has("Lookup")) Error(enumeration, "enum cannot use Component/Factory/Lookup policies");
                        type.underlying = Reference(enumeration->getIntegerType(), enumeration);
                        for (const auto* value : enumeration->enumerators())
                        {
                            llvm::SmallString<32> number;
                            value->getInitVal().toString(number, 10);
                            type.enumerators.push_back({value->getNameAsString(), std::string(number), Location(value)});
                        }
                    }
                    schema.types.push_back(std::move(type));
                }
                else if (llvm::isa<clang::FieldDecl, clang::CXXMethodDecl>(decl))
                {
                    if (!annotation->isInherited() && !decl->getLexicalDeclContext()->isRecord())
                        Error(decl, "member annotations must be placed inside the defining class");
                    // Function templates sit in a FunctionTemplateDecl in the owner's
                    // member list; reject here as well so they cannot disappear silently.
                    if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(decl);
                        method && method->getTemplatedKind() != clang::FunctionDecl::TK_NonTemplate)
                        Error(decl, "unsupported reflected method template");
                    const auto* parent = llvm::dyn_cast<clang::CXXRecordDecl>(decl->getDeclContext());
                    bool reflected = false;
                    if (parent) for (const auto* redecl : parent->redecls()) if (Annotation(redecl)) reflected = true;
                    if (!reflected) Error(decl, "annotated member requires an OV_TYPE owner");
                }
                else Error(decl, "unsupported reflection annotation target");
                return true;
            }
        };

        class Dependencies final : public clang::PPCallbacks
        {
            clang::SourceManager& manager;
            Schema& schema;
            std::set<std::string> seen;
        public:
            Dependencies(clang::SourceManager& m, Schema& s) : manager(m), schema(s) {}
            void FileChanged(clang::SourceLocation loc, FileChangeReason reason, clang::SrcMgr::CharacteristicKind, clang::FileID) override
            {
                if (reason != EnterFile || loc.isInvalid()) return;
                const auto id = manager.getFileID(loc);
                const auto entry = manager.getFileEntryRefForID(id);
                if (!entry) return;
                const auto path = std::filesystem::absolute(entry->getName().str()).lexically_normal().generic_string();
                if (!seen.insert(path).second) return;
                bool invalid = false;
                const auto bytes = manager.getBufferData(id, &invalid);
                if (invalid) { schema.failed = true; return; }
                const auto hash = llvm::SHA256::hash(llvm::ArrayRef(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
                schema.dependencies.push_back({path, llvm::toHex(hash, true)});
            }
        };
    }
    void ExtractSchema(clang::ASTContext& context, const std::string& root, Schema& schema)
    {
        Extractor visitor(context, root, schema);
        visitor.TraverseDecl(context.getTranslationUnitDecl());
    }
    void CollectDependencies(clang::CompilerInstance& compiler, Schema& schema)
    {
        compiler.getPreprocessor().addPPCallbacks(std::make_unique<Dependencies>(compiler.getSourceManager(), schema));
    }
}
